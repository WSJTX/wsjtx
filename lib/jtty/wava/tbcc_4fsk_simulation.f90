program tbcc_4fsk_simulation

    use, intrinsic :: iso_fortran_env, only: real32, int32, real64
    use omp_lib
    use tbcc
    implicit none

    real(real64) :: t_wall_start, t_wall_end

    character(len=8) :: channel_type
    character(len=8) :: arg
    real(real32) :: snr_2500_db, esno_linear
    
    
    integer(int32) :: tx_payload(PAYLOAD_BITS), tx_symbols(TOTAL_K)
    real(real32)   :: rx_tone_energies(0:3, TOTAL_K)
    integer(int32) :: rx_decoded(PAYLOAD_BITS)

    integer(int32) :: correct_frames, undetected_errors, total_detected_errors, total_frames
    integer(int32) :: num_frames, L_size, wava_iters, isnr1, isnr2
    real(real32)   :: p_correct, uer
    integer(int32) :: f, e, t, s, i, nargs
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
    call tbcc_init(memory_nu)
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
            !$omp   private(f, t, i, n_i, n_q, h_fade, ch_i, ch_q, decode_success, payload_match, &
            !$omp           tx_payload, tx_symbols, rx_tone_energies, rx_decoded) &
            !$omp   reduction(+:correct_frames, undetected_errors, total_detected_errors) &
            !$omp   schedule(static)
            do f = 1, num_frames
                call generate_random_bits(tx_payload, PAYLOAD_BITS)
                call tbcc_encode(tx_payload, tx_symbols)

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

end program tbcc_4fsk_simulation
