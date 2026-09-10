module tbcc
    ! Tail-biting rate-1/2 convolutional code: encode, WAVA/list decode,
    ! CRC-12 append/check, and generator-polynomial selection. Used by JTTY
    ! mode (lib/jtty/jtty_fec_mod.f90 and callers) and by the standalone
    ! research tools under lib/jtty/wava/.
    use, intrinsic :: iso_fortran_env, only: real32, int32, int16
    use jtty_tbcc_code_profiles, only: jtty_tbcc_code_profile, &
         jtty_tbcc_code_profile_is_supported, &
         PROFILE_PAYLOAD_BITS => JTTY_TBCC_PAYLOAD_BITS, &
         PROFILE_OUTER_CHECK_BITS => JTTY_TBCC_OUTER_CHECK_BITS, &
         PROFILE_INFORMATION_BITS => JTTY_TBCC_INFORMATION_BITS
    !$ use omp_lib
    implicit none

    ! Fixed frame parameters, shared with every caller.
    integer(int32), parameter :: PAYLOAD_BITS = PROFILE_PAYLOAD_BITS
    integer(int32), parameter :: CRC_BITS     = PROFILE_OUTER_CHECK_BITS
    integer(int32), parameter :: TOTAL_K      = PROFILE_INFORMATION_BITS
    integer(int32), parameter :: TBCC_OUTER_POLYNOMIAL_80F = int(Z"80F", int32)

    ! Standalone simulation tools retain runtime nu selection through tbcc_init.
    ! JTTY production callers pass a complete selected profile instead.
    integer(int32) :: memory_nu, num_states, g0_poly, g1_poly, reg_mask

    integer(int32), allocatable, protected :: incoming_tones(:,:)

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
        if (allocated(incoming_tones)) deallocate(incoming_tones)
        allocate(incoming_tones(0:1, 0:num_states-1))
        call build_incoming_tones(memory_nu, num_states, g0_poly, g1_poly, incoming_tones)
    end subroutine tbcc_init

    subroutine tbcc_encode(payload, tone_symbols, code_profile, encoded_bits)
        ! Encodes a PAYLOAD_BITS-bit payload into a TOTAL_K-symbol 4-FSK
        ! tone sequence: CRC-12 append, then tail-biting rate-1/2
        ! convolutional encode with a Gray-coded 2-bit-per-symbol tone
        ! mapping (00/01/11/10 -> 0/1/2/3). A caller that omits code_profile
        ! must first initialize the standalone simulation code with tbcc_init().
        integer(int32), intent(in)  :: payload(PAYLOAD_BITS)
        integer(int32), intent(out) :: tone_symbols(TOTAL_K)
        type(jtty_tbcc_code_profile), intent(in), optional :: code_profile
        integer(int32), intent(out), optional :: encoded_bits(2, TOTAL_K)
        integer(int32) :: info_bits(2, TOTAL_K)
        integer(int32) :: state, t, bit, g0_out, out_b0, out_b1
        integer(int32) :: selected_nu, selected_state_count, selected_g0, selected_g1
        integer(int32) :: selected_register_mask, outer_polynomial, outer_check_bits
        integer(int32) :: outer_top_bit_mask, outer_register_mask

        call resolve_code_definition(code_profile, selected_nu, selected_state_count, &
             selected_g0, selected_g1, selected_register_mask, outer_polynomial, &
             outer_check_bits, outer_top_bit_mask, outer_register_mask)
        call encode_crc12(payload, info_bits, code_profile)

        ! Tail-biting initialization: preload the shift register with the
        ! message's own last nu bits, so the encoder's starting
        ! state equals what it would be after wrapping around the circular
        ! frame.
        state = 0
        do t = 0, selected_nu - 1
            bit = info_bits(1, TOTAL_K - (selected_nu - 1) + t)
            state = iand(ior(ishft(state, 1), bit), selected_state_count-1)
        end do

        do t = 1, TOTAL_K
            bit = info_bits(1, t)
            g0_out = iand(ior(ishft(state, 1), bit), selected_register_mask)
            out_b0 = parity(iand(g0_out, selected_g0))
            out_b1 = parity(iand(g0_out, selected_g1))
            state = iand(ior(ishft(state, 1), bit), selected_state_count-1)
            if (present(encoded_bits)) encoded_bits(:, t) = [out_b0, out_b1]

            if (out_b0 == 0 .and. out_b1 == 0) tone_symbols(t) = 0
            if (out_b0 == 0 .and. out_b1 == 1) tone_symbols(t) = 1
            if (out_b0 == 1 .and. out_b1 == 1) tone_symbols(t) = 2
            if (out_b0 == 1 .and. out_b1 == 0) tone_symbols(t) = 3
        end do
    end subroutine tbcc_encode

    subroutine tbcc_wava_fsk_decode(tone_energies, list_size, max_wava_iters,            &
        final_payload, success, reserved_zero_bit, code_profile)
        real(real32), intent(in)     :: tone_energies(0:3, TOTAL_K)
        integer(int32), intent(in)   :: list_size
        integer(int32), intent(in)   :: max_wava_iters
        integer(int32), intent(out)  :: final_payload(PAYLOAD_BITS)
        logical, intent(out)         :: success
        ! Failed decodes return an all-zero payload; success determines validity.
        ! Optional 1-based position (within PAYLOAD_BITS) of a payload bit
        ! that the caller guarantees is always transmitted as 0. When
        ! present, a list candidate is only accepted if it also satisfies
        ! this in addition to CRC==0, rather than accepting the first
        ! CRC-clean candidate regardless -- roughly halves the undetected-
        ! error rate, since a wrong candidate must now also hit this bit by
        ! chance. Absent (the default) reproduces the original CRC-only
        ! behavior exactly.
        integer(int32), intent(in), optional :: reserved_zero_bit
        type(jtty_tbcc_code_profile), intent(in), optional :: code_profile

        real(real32), allocatable    :: prev_m(:), curr_m(:)
        integer(int16), allocatable  :: traceback_table(:,:)
        type(candidate_t), allocatable :: sorted_list(:)
        integer(int32) :: iter, t, s, bit_in, prev_s, curr_s, l, crc_reg, i
        integer(int32), allocatable :: profile_tones(:,:)
        integer(int32) :: selected_nu, selected_state_count, selected_g0, selected_g1
        integer(int32) :: selected_register_mask, outer_polynomial, outer_check_bits
        integer(int32) :: outer_top_bit_mask, outer_register_mask
        real(real32)   :: m0, m1
        integer(int32) :: tmp_bits(TOTAL_K)

        final_payload = 0_int32
        success = .false.
        call resolve_code_definition(code_profile, selected_nu, selected_state_count, &
             selected_g0, selected_g1, selected_register_mask, outer_polynomial, &
             outer_check_bits, outer_top_bit_mask, outer_register_mask)
        ! Zero iterations means the WAVA loop below never runs, leaving
        ! curr_m/traceback_table unread-by-design -- but the traceback and
        ! list-selection code after the loop unconditionally reads them
        ! regardless. Bail out before allocating rather than let that read
        ! uninitialized memory (production always passes max_wava_iters=2;
        ! only reachable via the CLI simulator's --iters option, which
        ! accepts any caller-supplied value; found by David Christle, PR
        ! #337, who confirmed it otherwise returns success=.true. with an
        ! all-zero payload).
        if (max_wava_iters < 1) return
        allocate(prev_m(0:selected_state_count-1), curr_m(0:selected_state_count-1))
        allocate(traceback_table(0:selected_state_count-1, TOTAL_K), sorted_list(list_size))

        allocate(profile_tones(0:1,0:selected_state_count-1))
        if (present(code_profile)) then
            call build_incoming_tones(selected_nu, selected_state_count, &
                 selected_g0, selected_g1, profile_tones)
        else
            profile_tones = incoming_tones
        end if
        prev_m = 0.0_real32

        do l = 1, list_size
            sorted_list(l)%metric = -1.0e30_real32
        end do

        ! Forward Trellis WAVA loop
        do iter = 1, max_wava_iters
            do t = 1, TOTAL_K
                curr_m = -1.0e30_real32
                do s = 0, selected_state_count-1
                    ! For a destination state 's' at time 't' under a left-shift model,
                    ! the two possible predecessor states at time 't-1' are determined
                    ! by shifting 's' right and checking both options for the bit that
                    ! left the window.
                    prev_s = iand(ishft(s, -1), selected_state_count-1)

                    m0 = prev_m(prev_s) + tone_energies(profile_tones(0, s), t)
                    prev_s = ior(prev_s, ishft(1, selected_nu-1))
                    m1 = prev_m(prev_s) + tone_energies(profile_tones(1, s), t)

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
        do s = 0, selected_state_count-1
            curr_s = s
            do t = TOTAL_K, 1, -1
                ! The input bit that caused the transition into 'curr_s' is its LSB (bit 0)
                bit_in = iand(curr_s, 1)
                tmp_bits(t) = bit_in

                ! Recover the parent state index using the recorded history bit flag
                prev_s = iand(ishft(curr_s, -1), selected_state_count-1)
                if (traceback_table(curr_s, t) == 1_int16) then
                    prev_s = ior(prev_s, ishft(1, selected_nu-1))
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
                crc_reg = ieor(crc_reg, ishft(sorted_list(l)%bits(i), &
                     outer_check_bits - 1))
                if (iand(crc_reg, outer_top_bit_mask) /= 0) then
                    crc_reg = ieor(ishft(crc_reg, 1), outer_polynomial)
                else
                    crc_reg = ishft(crc_reg, 1)
                end if
                crc_reg = iand(crc_reg, outer_register_mask)
            end do

            if (crc_reg == 0) then
                final_payload = sorted_list(l)%bits(1:PAYLOAD_BITS)
                success = .true.
                exit
            end if
        end do

        deallocate(prev_m, curr_m, traceback_table, sorted_list)
    end subroutine tbcc_wava_fsk_decode

    subroutine encode_crc12(payload, out_buf, code_profile)
        integer(int32), intent(in)  :: payload(PAYLOAD_BITS)
        integer(int32), intent(out) :: out_buf(2, TOTAL_K)
        type(jtty_tbcc_code_profile), intent(in), optional :: code_profile
        integer(int32) :: crc_reg, i
        integer(int32) :: selected_nu, selected_state_count, selected_g0, selected_g1
        integer(int32) :: selected_register_mask, outer_polynomial, outer_check_bits
        integer(int32) :: outer_top_bit_mask, outer_register_mask

        call resolve_code_definition(code_profile, selected_nu, selected_state_count, &
             selected_g0, selected_g1, selected_register_mask, outer_polynomial, &
             outer_check_bits, outer_top_bit_mask, outer_register_mask)
        out_buf(1, 1:PAYLOAD_BITS) = payload
        crc_reg = 0
        do i = 1, PAYLOAD_BITS
            crc_reg = ieor(crc_reg, ishft(payload(i), outer_check_bits - 1))
            if (iand(crc_reg, outer_top_bit_mask) /= 0) then
                crc_reg = ieor(ishft(crc_reg, 1), outer_polynomial)
            else
                crc_reg = ishft(crc_reg, 1)
            end if
            crc_reg = iand(crc_reg, outer_register_mask)
        end do
        do i = 1, outer_check_bits
            out_buf(1, PAYLOAD_BITS + i) = &
                 iand(ishft(crc_reg, -(outer_check_bits - i)), 1)
        end do
    end subroutine encode_crc12

    subroutine resolve_code_definition(code_profile, selected_nu, selected_state_count, &
        selected_g0, selected_g1, selected_register_mask, outer_polynomial, &
        outer_check_bits, outer_top_bit_mask, outer_register_mask)
        type(jtty_tbcc_code_profile), intent(in), optional :: code_profile
        integer(int32), intent(out) :: selected_nu, selected_state_count
        integer(int32), intent(out) :: selected_g0, selected_g1, selected_register_mask
        integer(int32), intent(out) :: outer_polynomial, outer_check_bits
        integer(int32), intent(out) :: outer_top_bit_mask, outer_register_mask

        if (present(code_profile)) then
            if (.not.jtty_tbcc_code_profile_is_supported(code_profile)) &
                 error stop 'unsupported JTTY TBCC code profile'
            selected_nu = code_profile%memory_nu
            selected_state_count = code_profile%state_count
            selected_g0 = code_profile%generator_0
            selected_g1 = code_profile%generator_1
            selected_register_mask = code_profile%register_mask
            outer_polynomial = code_profile%outer_polynomial
            outer_check_bits = code_profile%outer_check_bits
            outer_top_bit_mask = code_profile%outer_top_bit_mask
            outer_register_mask = code_profile%outer_register_mask
        else
            selected_nu = memory_nu
            selected_state_count = num_states
            selected_g0 = g0_poly
            selected_g1 = g1_poly
            selected_register_mask = reg_mask
            outer_polynomial = TBCC_OUTER_POLYNOMIAL_80F
            outer_check_bits = CRC_BITS
            outer_top_bit_mask = shiftl(1_int32, CRC_BITS - 1_int32)
            outer_register_mask = shiftl(1_int32, CRC_BITS) - 1_int32
        end if
    end subroutine resolve_code_definition

    subroutine build_incoming_tones(nu, state_count, g0, g1, tones)
        integer(int32), intent(in) :: nu, state_count, g0, g1
        integer(int32), intent(out) :: tones(0:1,0:state_count-1)
        integer(int32) :: state, dropped_bit, register_value, b0, b1
        integer(int32), parameter :: gray_tones(0:3) = [0, 1, 3, 2]

        do state = 0, state_count-1
            do dropped_bit = 0, 1
                register_value = ior(state, ishft(dropped_bit, nu))
                b0 = parity(iand(register_value, g0))
                b1 = parity(iand(register_value, g1))
                tones(dropped_bit, state) = gray_tones(2*b0+b1)
            end do
        end do
    end subroutine build_incoming_tones

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
        ! In a non-OpenMP build, !$ lines and !$omp directives are comments,
        ! so use a deterministic single-thread seed without requiring libgomp.
        rng_state = 9999_int32
        !$omp parallel default(shared)
        !$ rng_state = 9999_int32 + 104729_int32 * int(omp_get_thread_num(), int32)
        if (rng_state == 0_int32) rng_state = 1_int32
        !$omp end parallel
    end subroutine seed_random_generator

end module tbcc
