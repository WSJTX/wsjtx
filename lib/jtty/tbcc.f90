module tbcc
    ! Tail-biting rate-1/2 convolutional code: encode, WAVA/list decode,
    ! CRC-12 append/check, and generator-polynomial selection. Used by JTTY
    ! mode (lib/jtty/jtty_fec_mod.f90 and callers) and by the standalone
    ! research tools under lib/jtty/wava/.
    use, intrinsic :: iso_fortran_env, only: real32, int32, int16
    use omp_lib
    implicit none

    ! Fixed frame parameters, shared with every caller.
    integer(int32), parameter :: PAYLOAD_BITS = 34
    integer(int32), parameter :: CRC_BITS     = 12
    integer(int32), parameter :: TOTAL_K      = PAYLOAD_BITS + CRC_BITS ! 46 bits
    integer(int32), parameter :: CRC_POLY     = int(Z"80F", int32)

    ! Code parameters that depend on the chosen constraint length. These are
    ! plain module variables, not PARAMETERs, because memory_nu is a
    ! runtime choice (see select_generator_polynomials()): set once by
    ! tbcc_init(), after which every module procedure reads them but never
    ! modifies them.
    integer(int32) :: memory_nu, num_states, g0_poly, g1_poly, reg_mask

    type candidate_t
        real(real32) :: metric
        integer(int32) :: bits(TOTAL_K)
    end type candidate_t

    ! Per-thread RNG state (xorshift32), used only by the channel-simulation
    ! helpers below (box_muller/generate_random_bits/seed_random_generator).
    ! tbcc_encode/tbcc_wava_fsk_decode/encode_crc12 never touch it -- a
    ! production caller that never runs the simulator helpers pays nothing
    ! for this. The RANDOM_NUMBER intrinsic keeps a single shared generator
    ! state and is not safe to call concurrently from multiple OpenMP
    ! threads, so each thread gets its own independent stream instead.
    ! Threadprivate module variables retain that property for every program
    ! unit that USEs this module.
    integer(int32), save :: rng_state = 0
    !$omp threadprivate(rng_state)

contains

    subroutine tbcc_init(nu)
        ! Sets memory_nu, num_states, g0_poly, g1_poly, and reg_mask for the
        ! chosen constraint length K = nu+1. Call once, before any
        ! encode/decode.
        integer(int32), intent(in) :: nu
        memory_nu  = nu
        num_states = 2**nu
        call select_generator_polynomials(nu, g0_poly, g1_poly)
        reg_mask = 2**(nu + 1) - 1
    end subroutine tbcc_init

    subroutine tbcc_encode(payload, tone_symbols)
        ! Encodes a PAYLOAD_BITS-bit payload into a TOTAL_K-symbol 4-FSK
        ! tone sequence: CRC-12 append, then tail-biting rate-1/2
        ! convolutional encode with a Gray-coded 2-bit-per-symbol tone
        ! mapping (00/01/11/10 -> 0/1/2/3). Requires tbcc_init() to have
        ! been called first.
        integer(int32), intent(in)  :: payload(PAYLOAD_BITS)
        integer(int32), intent(out) :: tone_symbols(TOTAL_K)
        integer(int32) :: info_bits(2, TOTAL_K)
        integer(int32) :: state, t, bit, g0_out, out_b0, out_b1

        call encode_crc12(payload, info_bits)  ! info_bits(1,1:TOTAL_K) = payload+CRC

        ! Tail-biting initialization: preload the shift register with the
        ! message's own last memory_nu bits, so the encoder's starting
        ! state equals what it would be after wrapping around the circular
        ! frame.
        state = 0
        do t = 0, memory_nu - 1
            bit = info_bits(1, TOTAL_K - (memory_nu - 1) + t)
            state = iand(ior(ishft(state, 1), bit), num_states-1)
        end do

        do t = 1, TOTAL_K
            bit = info_bits(1, t)
            g0_out = iand(ior(ishft(state, 1), bit), reg_mask)
            out_b0 = parity(iand(g0_out, g0_poly))
            out_b1 = parity(iand(g0_out, g1_poly))
            state = iand(ior(ishft(state, 1), bit), num_states-1)

            if (out_b0 == 0 .and. out_b1 == 0) tone_symbols(t) = 0
            if (out_b0 == 0 .and. out_b1 == 1) tone_symbols(t) = 1
            if (out_b0 == 1 .and. out_b1 == 1) tone_symbols(t) = 2
            if (out_b0 == 1 .and. out_b1 == 0) tone_symbols(t) = 3
        end do
    end subroutine tbcc_encode

    subroutine tbcc_wava_fsk_decode(tone_energies, list_size, max_wava_iters,            &
        final_payload, success, reserved_zero_bit)
        real(real32), intent(in)     :: tone_energies(0:3, TOTAL_K)
        integer(int32), intent(in)   :: list_size
        integer(int32), intent(in)   :: max_wava_iters
        integer(int32), intent(out)  :: final_payload(PAYLOAD_BITS)
        logical, intent(out)         :: success
        ! Optional 1-based position (within PAYLOAD_BITS) of a payload bit
        ! that the caller guarantees is always transmitted as 0. When
        ! present, a list candidate is only accepted if it also satisfies
        ! this in addition to CRC==0, rather than accepting the first
        ! CRC-clean candidate regardless -- roughly halves the undetected-
        ! error rate, since a wrong candidate must now also hit this bit by
        ! chance. Absent (the default) reproduces the original CRC-only
        ! behavior exactly.
        integer(int32), intent(in), optional :: reserved_zero_bit

        real(real32), allocatable    :: prev_m(:), curr_m(:)
        integer(int16), allocatable  :: traceback_table(:,:)
        type(candidate_t), allocatable :: sorted_list(:)
        integer(int32) :: iter, t, s, bit_in, prev_s, curr_s, l, crc_reg, i
        integer(int32) :: g0_out, g1_out, out_b0, out_b1, tone_idx
        real(real32)   :: m0, m1
        integer(int32) :: tmp_bits(TOTAL_K)

        success = .false.
        allocate(prev_m(0:num_states-1), curr_m(0:num_states-1))
        allocate(traceback_table(0:num_states-1, TOTAL_K), sorted_list(list_size))

        prev_m = 0.0_real32

        do l = 1, list_size
            sorted_list(l)%metric = -1.0e30_real32
        end do

        ! Forward Trellis WAVA loop
        do iter = 1, max_wava_iters
            do t = 1, TOTAL_K
                curr_m = -1.0e30_real32
                do s = 0, num_states-1
                    ! For a destination state 's' at time 't' under a left-shift model,
                    ! the two possible predecessor states at time 't-1' are determined
                    ! by shifting 's' right and checking both options for the bit that
                    ! left the window.
                    prev_s = iand(ishft(s, -1), num_states-1)

                    ! Option A: The oldest bit dropped from the register was 0
                    g0_out = iand(ior(ishft(prev_s, 1), iand(s, 1)), reg_mask)
                    out_b0 = parity(iand(g0_out, g0_poly))
                    out_b1 = parity(iand(g0_out, g1_poly))
                    if (out_b0 == 0 .and. out_b1 == 0) tone_idx = 0
                    if (out_b0 == 0 .and. out_b1 == 1) tone_idx = 1
                    if (out_b0 == 1 .and. out_b1 == 1) tone_idx = 2
                    if (out_b0 == 1 .and. out_b1 == 0) tone_idx = 3
                    m0 = prev_m(prev_s) + tone_energies(tone_idx, t)

                    ! Option B: The oldest bit dropped from the register was 1
                    prev_s = ior(prev_s, ishft(1, memory_nu-1))
                    g0_out = iand(ior(ishft(prev_s, 1), iand(s, 1)), reg_mask)
                    out_b0 = parity(iand(g0_out, g0_poly))
                    out_b1 = parity(iand(g0_out, g1_poly))
                    if (out_b0 == 0 .and. out_b1 == 0) tone_idx = 0
                    if (out_b0 == 0 .and. out_b1 == 1) tone_idx = 1
                    if (out_b0 == 1 .and. out_b1 == 1) tone_idx = 2
                    if (out_b0 == 1 .and. out_b1 == 0) tone_idx = 3
                    m1 = prev_m(prev_s) + tone_energies(tone_idx, t)

                    ! Select and record maximum likelihood trajectory decision
                    if (m0 > m1) then
                        curr_m(s) = m0
                        ! Picked path from state with dropped bit 0
                        traceback_table(s, t) = 0_int16
                    else
                        curr_m(s) = m1
                        ! Picked path from state with dropped bit 1
                        traceback_table(s, t) = 1_int16
                    end if
                end do
                prev_m = curr_m
            end do
        end do

        ! REVISED TRACEBACK ALIGNMENT
        do s = 0, num_states-1
            curr_s = s
            do t = TOTAL_K, 1, -1
                ! The input bit that caused the transition into 'curr_s' is its LSB (bit 0)
                bit_in = iand(curr_s, 1)
                tmp_bits(t) = bit_in

                ! Recover the parent state index using the recorded history bit flag
                prev_s = iand(ishft(curr_s, -1), num_states-1)
                if (traceback_table(curr_s, t) == 1_int16) then
                    prev_s = ior(prev_s, ishft(1, memory_nu-1))
                end if
                curr_s = prev_s
            end do

            ! Check if circular tail-biting constraint condition holds valid
            if (curr_s == s) then
                do l = 1, list_size
                    if (curr_m(s) > sorted_list(l)%metric) then
                        do i = list_size, l+1, -1
                            sorted_list(i) = sorted_list(i-1)
                        end do
                        sorted_list(l)%metric = curr_m(s)
                        sorted_list(l)%bits = tmp_bits
                        exit
                    end if
                end do
            end if
        end do

        ! Serial CRC (and, if requested, reserved-bit) filtering pass
        do l = 1, list_size
            if (sorted_list(l)%metric <= -1.0e29_real32) cycle
            if (present(reserved_zero_bit)) then
                if (sorted_list(l)%bits(reserved_zero_bit) /= 0) cycle
            end if
            crc_reg = 0
            do i = 1, TOTAL_K
                crc_reg = ieor(crc_reg, ishft(sorted_list(l)%bits(i), 11))
                if (iand(crc_reg, Z"800") /= 0) then
                    crc_reg = ieor(ishft(crc_reg, 1), CRC_POLY)
                else
                    crc_reg = ishft(crc_reg, 1)
                end if
                crc_reg = iand(crc_reg, Z"FFF")
            end do

            if (crc_reg == 0) then
                final_payload = sorted_list(l)%bits(1:PAYLOAD_BITS)
                success = .true.
                exit
            end if
        end do

        deallocate(prev_m, curr_m, traceback_table, sorted_list)
    end subroutine tbcc_wava_fsk_decode

    subroutine encode_crc12(payload, out_buf)
        integer(int32), intent(in)  :: payload(PAYLOAD_BITS)
        integer(int32), intent(out) :: out_buf(2, TOTAL_K)
        integer(int32) :: crc_reg, i
        out_buf(1, 1:PAYLOAD_BITS) = payload
        crc_reg = 0
        do i = 1, PAYLOAD_BITS
            crc_reg = ieor(crc_reg, ishft(payload(i), 11))
            if (iand(crc_reg, Z"800") /= 0) then
                crc_reg = ieor(ishft(crc_reg, 1), CRC_POLY)
            else
                crc_reg = ishft(crc_reg, 1)
            end if
            crc_reg = iand(crc_reg, Z"FFF")
        end do
        do i = 1, 12
            out_buf(1, PAYLOAD_BITS + i) = iand(ishft(crc_reg, -(12 - i)), 1)
        end do
    end subroutine encode_crc12

    subroutine box_muller(rand_normal)
        real(real32), intent(out) :: rand_normal
        real(real32) :: u1, u2
        u1 = thread_uniform(); u2 = thread_uniform()
        if (u1 < 1.0e-30_real32) u1 = 1.0e-30_real32
        rand_normal = sqrt(-2.0_real32 * log(u1)) * cos(6.28318530718_real32 * u2)
    end subroutine box_muller

    subroutine generate_random_bits(vec, n)
        integer(int32), intent(out) :: vec(n)
        integer(int32) :: n, i
        do i = 1, n
            vec(i) = merge(1, 0, thread_uniform() >= 0.5_real32)
        end do
    end subroutine generate_random_bits

    function thread_uniform() result(u)
        ! Fast per-thread xorshift32 generator returning a uniform value on
        ! [0,1]. rng_state is !$omp threadprivate, so this advances each
        ! thread's own independent stream with no shared mutable state and
        ! no locking -- the replacement for RANDOM_NUMBER, which is not
        ! safe to call from a parallel region.
        real(real32) :: u
        rng_state = ieor(rng_state, ishft(rng_state, 13))
        rng_state = ieor(rng_state, ishft(rng_state, -17))
        rng_state = ieor(rng_state, ishft(rng_state, 5))
        u = real(iand(rng_state, huge(rng_state)), real32) / real(huge(rng_state), real32)
    end function thread_uniform

    subroutine select_generator_polynomials(nu, g0, g1)
        ! Optimal (maximum free distance, noncatastrophic) rate-1/2
        ! generator polynomials, indexed by memory order nu (constraint
        ! length K = nu+1). K=12 (nu=11) keeps this file's original pair
        ! (octal 7173,5261), unchanged, so results already gathered at
        ! that setting stay reproducible. K=10, 11, 13 are the standard
        ! maximum-dfree pairs from the Larsen/Odenwalder rate-1/2 tables
        ! (as tabulated in e.g. Proakis "Digital Communications" and Lin &
        ! Costello "Error Control Coding"):
        !   K=10 (nu= 9): octal 1167,1545  dfree=12
        !   K=11 (nu=10): octal 2335,3661  dfree=14
        !   K=12 (nu=11): octal 7173,5261  (this file's existing pair)
        !   K=13 (nu=12): octal 10533,17661 dfree=16
        integer(int32), intent(in)  :: nu
        integer(int32), intent(out) :: g0, g1
        select case (nu)
        case (9)   ! K = 10, dfree = 12
            g0 = int(Z"277", int32);  g1 = int(Z"365", int32)
        case (10)  ! K = 11, dfree = 14
            g0 = int(Z"4DD", int32);  g1 = int(Z"7B1", int32)
        case (11)  ! K = 12 -- this file's original constants
            g0 = int(Z"E7B", int32);  g1 = int(Z"AB1", int32)
        case (12)  ! K = 13, dfree = 16
            g0 = int(Z"115B", int32); g1 = int(Z"1FB1", int32)
        case default
            print *, "No optimal generator polynomial pair defined for memory_nu =", nu
            print *, "Supported values: 9, 10, 11, 12 (K = 10, 11, 12, 13)"
            stop 1
        end select
    end subroutine select_generator_polynomials

    pure integer(int32) function parity(val)
        integer(int32), intent(in) :: val
        integer(int32) :: temp, p
        temp = val; p = 0
        do while (temp > 0)
            if (iand(temp, 1) /= 0) p = ieor(p, 1)
            temp = ishft(temp, -1)
        end do
        parity = p
    end function parity

    subroutine seed_random_generator()
        ! Give every OpenMP thread its own distinct, deterministic, nonzero
        ! xorshift32 seed -- rng_state is threadprivate, so this assignment
        ! runs once per thread. Reproducible for a fixed thread count;
        ! changing OMP_NUM_THREADS changes how frames are partitioned across
        ! streams, so per-frame outcomes differ (though aggregate statistics
        ! over num_frames should not, within Monte Carlo noise).
        !$omp parallel default(shared)
        rng_state = 9999_int32 + 104729_int32 * int(omp_get_thread_num(), int32)
        if (rng_state == 0_int32) rng_state = 1_int32
        !$omp end parallel
    end subroutine seed_random_generator

end module tbcc
