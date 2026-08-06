program tbcc_4fsk_simulation

    use, intrinsic :: iso_fortran_env, only: real32, int32, int16, real64
    use omp_lib
    implicit none

    ! Per-thread RNG state (xorshift32). The RANDOM_NUMBER intrinsic keeps a
    ! single shared generator state and is not safe to call concurrently from
    ! multiple OpenMP threads.

    integer(int32), save :: rng_state = 0
    !$omp threadprivate(rng_state)

    real(real64) :: t_wall_start, t_wall_end

    ! Fixed parameters of simulation
    integer(int32), parameter :: PAYLOAD_BITS = 34
    integer(int32), parameter :: CRC_BITS     = 12
    integer(int32), parameter :: TOTAL_K     = PAYLOAD_BITS + CRC_BITS ! 46 bits

    ! Rate-1/2 generator polynomials and the shift-register width they're
    ! applied over. Both depend on memory_nu (constraint length K =
    ! memory_nu+1) -- an optimal polynomial pair for one K is not, in
    ! general, optimal (or even valid past a truncated low-order subset)
    ! for another K, so these are set at runtime by
    ! select_generator_polynomials() once memory_nu is known, rather than
    ! being fixed PARAMETERs. REG_MASK = 2**(memory_nu+1)-1, the K-bit
    ! window the taps are applied over. 

    integer(int32) :: G0_HEX, G1_HEX, REG_MASK

    integer(int32), parameter :: CRC_POLY = int(Z"80F", int32)

    type candidate_t
        real(real32) :: metric
        integer(int32) :: bits(TOTAL_K)
    end type candidate_t

    character(len=8) :: channel_type
    character(len=8) :: arg
    real(real32) :: snr_2500_db, esno_linear
    
    
    integer(int32) :: tx_payload(PAYLOAD_BITS), tx_encoded(2, TOTAL_K), tx_symbols(TOTAL_K)
    real(real32)   :: rx_tone_energies(0:3, TOTAL_K) 
    integer(int32) :: rx_decoded(PAYLOAD_BITS)
    
    integer(int32) :: correct_frames, undetected_errors, total_detected_errors, total_frames
    integer(int32) :: num_states, num_frames, memory_nu, L_size, wava_iters, isnr1, isnr2
    real(real32)   :: p_correct, uer
    integer(int32) :: f, e, t, s, g0_out, g1_out, out_b0, out_b1, i, state, bit, nargs
    real(real32)   :: n_i, n_q, h_fade, ch_i, ch_q, xsnr
    logical        :: decode_success, payload_match

    nargs=iargc()
    if(nargs.ne.5) then
       print*,'Usage:   tbcc_sim  nu  L  iters  Nmax SNR'
       print*,'Example: tbcc_sim   9  4    2   10000  0'  
       stop
    endif

    call getarg(1,arg)
    read(arg,*) memory_nu
    call getarg(2,arg)
    read(arg,*) L_size        ! List size
    call getarg(3,arg)
    read(arg,*) wava_iters    ! Trellis wraps for tail-biting convergence
    call getarg(4,arg)
    read(arg,*) num_frames
    call getarg(5,arg)
    read(arg,*) xsnr

    call seed_random_generator()

    print *, "=========================================================================="
    print *, "Tail-Biting Convolutional Code (92,46) Noncoherent 4-FSK Simulation Suite"
    print *, "Symbol Rate = 31.25 Baud | Reference Bandwidth = 2500 Hz"
    print *, "OpenMP threads available: ", omp_get_max_threads()
    print *, "Writing data logs directly to simulation_results.csv..."
    print *, "=========================================================================="

    ! Open CSV data output buffer file
    open(unit=24, file="simulation_results.dat", status="unknown", action="write")
    open(unit=25, file="simulation_results.csv", status="unknown", action="write")
    write(24, '(A)') "snr,frames,good,uer"
    write(25, '(A)') "channel,snr_db,p_correct,uer_pct"

    t_wall_start = omp_get_wtime()
    num_states = 2**memory_nu
    call select_generator_polynomials(memory_nu, G0_HEX, G1_HEX)
    REG_MASK = 2**(memory_nu + 1) - 1
    total_frames = 0

    do e = 1, 2
        channel_type = merge("AWGN    ", "RAYLEIGH", e == 1)
        print *, ""
        print *, "--- Channel Profile: ", trim(channel_type), " ---"
        write(24,'(a,a,a)') "--- Channel Profile: ", trim(channel_type), " ---"

        write(*,1000)
        write(24,1000)
1000    format(' SNR_2500   Frames    Good       UER   '/39('-'))

        isnr1 = -5
        isnr2 = -20
        do s = isnr1, isnr2, -1
            snr_2500_db = s
            if(xsnr.ne.0.0) snr_2500_db = xsnr
            esno_linear = (10.0_real32 ** (snr_2500_db / 10.0_real32)) * 80.0_real32
            correct_frames = 0
            undetected_errors = 0
            total_detected_errors = 0
            
            ! Each frame (generate -> encode -> channel -> WAVA decode) is
            ! completely independent of every other frame, so the frame loop
            ! is embarrassingly parallel. Every array/scalar that a single
            ! frame writes must be private -- otherwise threads clobber each
            ! other's tx_payload/tx_encoded/etc. The three tallies are
            ! combined across threads via REDUCTION.
            
            !$omp parallel do default(shared) &
            !$omp   private(f, t, i, state, bit, g0_out, g1_out, out_b0, out_b1, &
            !$omp           n_i, n_q, h_fade, ch_i, ch_q, decode_success, payload_match, &
            !$omp           tx_payload, tx_encoded, tx_symbols, rx_tone_energies, rx_decoded) &
            !$omp   reduction(+:correct_frames, undetected_errors, total_detected_errors) &
            !$omp   schedule(static)
            do f = 1, num_frames
                call generate_random_bits(tx_payload, PAYLOAD_BITS)
                call encode_crc12(tx_payload, tx_encoded)
                
                state = 0
                do t = 0, memory_nu - 1
                    bit = tx_encoded(1, TOTAL_K - (memory_nu - 1) + t)
                    state = iand(ior(ishft(state, 1), bit), NUM_STATES-1)
                end do
                
                do t = 1, TOTAL_K
                    bit = tx_encoded(1, t)
                    g0_out = iand(ior(ishft(state, 1), bit), REG_MASK)
                    out_b0 = parity(iand(g0_out, G0_HEX))
                    out_b1 = parity(iand(g0_out, G1_HEX))
                    
                    tx_encoded(1, t) = out_b0
                    tx_encoded(2, t) = out_b1
                    state = iand(ior(ishft(state, 1), bit), NUM_STATES-1)
                    
                    if (out_b0 == 0 .and. out_b1 == 0) tx_symbols(t) = 0
                    if (out_b0 == 0 .and. out_b1 == 1) tx_symbols(t) = 1
                    if (out_b0 == 1 .and. out_b1 == 1) tx_symbols(t) = 2
                    if (out_b0 == 1 .and. out_b1 == 0) tx_symbols(t) = 3
                end do

                ! Channel Simulation
                do t = 1, TOTAL_K
                    if (channel_type == "AWGN") then
                        h_fade = 1.0_real32
                    else
                        call box_muller(n_i)
                        call box_muller(n_q)
                        h_fade = sqrt(0.5_real32 * (n_i**2 + n_q**2))
                    end if

                    do i = 0, 3
                        call box_muller(n_i)
                        call box_muller(n_q)
                        n_i = n_i * sqrt(0.5_real32)
                        n_q = n_q * sqrt(0.5_real32)
                        
                        if (i == tx_symbols(t)) then
                            ch_i = (h_fade * sqrt(esno_linear)) + n_i
                            ch_q = n_q
                        else
                            ch_i = n_i
                            ch_q = n_q
                        end if
                        rx_tone_energies(i, t) = ch_i**2 + ch_q**2
                    end do
                end do

                ! Decoding Execution
                call tbcc_wava_fsk_decode(rx_tone_energies, L_size, wava_iters, &
                     rx_decoded, decode_success)

                if (decode_success) then
                    payload_match = all(tx_payload == rx_decoded)
                    if (payload_match) then
                        correct_frames = correct_frames + 1
                    else
                        undetected_errors = undetected_errors + 1
                    end if
                else
                    total_detected_errors = total_detected_errors + 1
                end if
            end do
            !$omp end parallel do

            p_correct = real(correct_frames, real32) / real(num_frames, real32)
            uer       = real(undetected_errors, real32) / real(num_frames, real32)
            total_frames = total_frames + num_frames

            write(*,1001) snr_2500_db, num_frames, p_correct, uer
            write(24,1001) snr_2500_db, num_frames, p_correct, uer
1001        format(f8.1,i10,2f10.6)
            
            ! Write tracking vectors to the spreadsheet data table (Multiplying UER by
            ! 100 for percentage scale)
            write(25, '(A,A,F6.1,A,F7.5,A,F7.5)') trim(channel_type), ",", snr_2500_db, &
                 ",", p_correct, ",", uer * 100.0_real32
            if(xsnr.ne.0.0) exit
        end do  !Loop over SNRs
    end do  !Loop over channel types (AWGN, then Rayleigh)

    close(24)
    close(25)

    t_wall_end = omp_get_wtime()
    print '(A,F8.2,A)', "Total wall-clock time: ", t_wall_end - t_wall_start, " s"
    print '(a,f7.3,a)' ,"Average time per frame: ",                                      &
         1000.0*(t_wall_end - t_wall_start)/total_frames, " ms"

contains

    subroutine tbcc_wava_fsk_decode(tone_energies, list_size, max_wava_iters,            &
        final_payload, success)
        use, intrinsic :: iso_fortran_env, only: int16
        real(real32), intent(in)     :: tone_energies(0:3, TOTAL_K)
        integer(int32), intent(in)   :: list_size
        integer(int32), intent(in)   :: max_wava_iters
        integer(int32), intent(out)  :: final_payload(PAYLOAD_BITS)
        logical, intent(out)         :: success

        real(real32), allocatable    :: prev_m(:), curr_m(:)
        integer(int16), allocatable  :: traceback_table(:,:)
        type(candidate_t), allocatable :: sorted_list(:)
        integer(int32) :: iter, t, s, bit_in, prev_s, curr_s, l, crc_reg, i
        integer(int32) :: g0_out, g1_out, out_b0, out_b1, tone_idx
        real(real32)   :: m0, m1
        integer(int32) :: tmp_bits(TOTAL_K)

        success = .false.
        allocate(prev_m(0:NUM_STATES-1), curr_m(0:NUM_STATES-1))
        allocate(traceback_table(0:NUM_STATES-1, TOTAL_K), sorted_list(list_size))

        prev_m = 0.0_real32 
        
        do l = 1, list_size
            sorted_list(l)%metric = -1.0e30_real32
        end do

        ! Forward Trellis WAVA loop
        do iter = 1, max_wava_iters
            do t = 1, TOTAL_K
                curr_m = -1.0e30_real32
                do s = 0, NUM_STATES-1
                    ! For a destination state 's' at time 't' under a left-shift model,
                    ! the two possible predecessor states at time 't-1' are determined 
                    ! by shifting 's' right and checking both options for the bit that
                    ! left the window.
                    prev_s = iand(ishft(s, -1), NUM_STATES-1)
                    
                    ! Option A: The oldest bit dropped from the register was 0
                    g0_out = iand(ior(ishft(prev_s, 1), iand(s, 1)), REG_MASK)
                    out_b0 = parity(iand(g0_out, G0_HEX))
                    out_b1 = parity(iand(g0_out, G1_HEX))
                    if (out_b0 == 0 .and. out_b1 == 0) tone_idx = 0
                    if (out_b0 == 0 .and. out_b1 == 1) tone_idx = 1
                    if (out_b0 == 1 .and. out_b1 == 1) tone_idx = 2
                    if (out_b0 == 1 .and. out_b1 == 0) tone_idx = 3
                    m0 = prev_m(prev_s) + tone_energies(tone_idx, t)

                    ! Option B: The oldest bit dropped from the register was 1
                    prev_s = ior(prev_s, ishft(1, memory_nu-1))
                    g0_out = iand(ior(ishft(prev_s, 1), iand(s, 1)), REG_MASK)
                    out_b0 = parity(iand(g0_out, G0_HEX))
                    out_b1 = parity(iand(g0_out, G1_HEX))
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
        do s = 0, NUM_STATES-1
            curr_s = s
            do t = TOTAL_K, 1, -1
                ! The input bit that caused the transition into 'curr_s' is its LSB (bit 0)
                bit_in = iand(curr_s, 1)
                tmp_bits(t) = bit_in
                
                ! Recover the parent state index using the recorded history bit flag
                prev_s = iand(ishft(curr_s, -1), NUM_STATES-1)
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

        ! Serial CRC Filtering Pass
        do l = 1, list_size
            if (sorted_list(l)%metric <= -1.0e29_real32) cycle
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
        ! [0,1]. rng_state is !$omp threadprivate (declared at the top of the
        ! program), so this advances each thread's own independent stream with
        ! no shared mutable state and no locking -- the replacement for
        ! RANDOM_NUMBER, which is not safe to call from a parallel region.
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

end program tbcc_4fsk_simulation
