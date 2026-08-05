program rcu_bound_4fsk
    ! Random Coding Union (RCU) achievability bound (Polyanskiy-Poor-Verdu
    ! 2010, eq. 2) for noncoherent 4-FSK over AWGN and Rayleigh-fading
    ! channels, matched to the exact channel model and SNR convention used
    ! in tbcc_4fsk_simulation.f90. This bounds the frame-error probability
    ! of the BEST POSSIBLE code at (n=46 symbols, M=2^k messages) -- a
    ! benchmark for how close an actual code+decoder gets to optimal, not a
    ! statement about any particular code.
    !
    ! Method: for a fixed received sequence r^n, each per-symbol term of the
    ! competitor information density i(X_bar_t; r_t) is an exact discrete
    ! 4-valued random variable (X_bar_t uniform over the 4 tones), so its
    ! mean/variance are closed-form -- no inner Monte Carlo. Summing 46 such
    ! terms and invoking the CLT gives the inner tail probability via erfc.
    ! Only the outer expectation (over the true (x^n, r^n) pair) needs Monte
    ! Carlo.
    use, intrinsic :: iso_fortran_env, only: real32, real64, int32
    use omp_lib
    implicit none

    integer(int32), save :: rng_state = 0
    !$omp threadprivate(rng_state)

    integer(int32), parameter :: N_SYM = 46   ! channel uses; matches TOTAL_K

    character(len=8)  :: channel_type
    character(len=16) :: arg
    integer(int32) :: nargs, num_trials, kbits, e, s
    real(real64)   :: Mminus1, rcu
    real(real32)   :: snr_2500_db, esno_linear
    real(real64)   :: t_wall_start, t_wall_end

    nargs = iargc()
    if (nargs /= 2) then
        print *, 'Usage: rcu_bound_4fsk <num_trials> <k_bits>'
        print *, '  k_bits: log2(number of equiprobable messages), e.g. 34'
        stop
    end if
    call getarg(1, arg); read(arg,*) num_trials
    call getarg(2, arg); read(arg,*) kbits
    Mminus1 = 2.0_real64**kbits - 1.0_real64

    call seed_random_generator()

    print *, "=========================================================================="
    print *, "RCU achievability bound: noncoherent 4-FSK, n =", N_SYM, " symbols"
    print *, "M = 2^", kbits, "equiprobable messages | OpenMP threads:", omp_get_max_threads()
    print *, "=========================================================================="

    open(unit=25, file="rcu_bound_results.csv", status="unknown", action="write")
    write(25,'(A)') "channel,snr_db,rcu_bound"

    t_wall_start = omp_get_wtime()

    do e = 1, 2
        channel_type = merge("AWGN    ", "RAYLEIGH", e == 1)
        print *, ""
        print *, "--- Channel: ", trim(channel_type), " ---"
        write(*,'(A10,A16)') "SNR_2500", "RCU bound"
        do s = -5, -20, -1
            snr_2500_db = real(s, real32)
            esno_linear = (10.0_real32**(snr_2500_db/10.0_real32)) * 80.0_real32
            call compute_rcu(channel_type, esno_linear, Mminus1, num_trials, rcu)
            write(*,'(F10.1,ES16.5)') snr_2500_db, rcu
            write(25,'(A,A,F6.1,A,ES14.6)') trim(channel_type), ",", snr_2500_db, ",", rcu
        end do
    end do

    close(25)
    t_wall_end = omp_get_wtime()
    print '(A,F8.2,A)', "Total wall-clock time: ", t_wall_end - t_wall_start, " s"

contains

    subroutine compute_rcu(channel_type, esno, Mm1, ntrials, rcu_out)
        character(len=*), intent(in) :: channel_type
        real(real32), intent(in)     :: esno
        real(real64), intent(in)     :: Mm1
        integer(int32), intent(in)   :: ntrials
        real(real64), intent(out)    :: rcu_out

        integer(int32) :: trial, t, x_true, j
        real(real32)   :: r(0:3), n_i, n_q, hfade
        real(real64)   :: logL(0:3), S, i_true, mu_t, var_t, term
        real(real64)   :: T_sum, MU_sum, VAR_sum, tail, bound_val, acc

        acc = 0.0_real64

        !$omp parallel do default(shared) &
        !$omp   private(trial, t, x_true, j, r, n_i, n_q, hfade, logL, S, &
        !$omp           i_true, mu_t, var_t, term, T_sum, MU_sum, VAR_sum, &
        !$omp           tail, bound_val) &
        !$omp   reduction(+:acc) schedule(static)
        do trial = 1, ntrials
            T_sum  = 0.0_real64
            MU_sum = 0.0_real64
            VAR_sum = 0.0_real64

            do t = 1, N_SYM
                x_true = int(thread_uniform()*4.0_real32)
                if (x_true > 3) x_true = 3

                if (channel_type == "AWGN") then
                    hfade = 1.0_real32
                else
                    call box_muller(n_i); call box_muller(n_q)
                    hfade = sqrt(0.5_real32*(n_i**2 + n_q**2))
                end if

                do j = 0, 3
                    call box_muller(n_i); call box_muller(n_q)
                    n_i = n_i*sqrt(0.5_real32); n_q = n_q*sqrt(0.5_real32)
                    if (j == x_true) then
                        r(j) = (hfade*sqrt(esno) + n_i)**2 + n_q**2
                    else
                        r(j) = n_i**2 + n_q**2
                    end if
                end do

                ! log-likelihood-ratio log L(r) = log f_sig(r) - log f_noise(r)
                do j = 0, 3
                    if (channel_type == "AWGN") then
                        logL(j) = -real(esno,real64) &
                                  + log_bessel_i0(2.0_real64*sqrt(real(esno,real64)*real(r(j),real64)))
                    else
                        logL(j) = -log(1.0_real64+real(esno,real64)) &
                                  + real(r(j),real64)*real(esno,real64)/(1.0_real64+real(esno,real64))
                    end if
                end do

                S = logsumexp4(logL)               ! log( sum_j L(r_j) )
                i_true = log(4.0_real64) + logL(x_true) - S

                mu_t = 0.0_real64
                do j = 0, 3
                    mu_t = mu_t + (log(4.0_real64) + logL(j) - S)
                end do
                mu_t = mu_t/4.0_real64

                var_t = 0.0_real64
                do j = 0, 3
                    term = (log(4.0_real64) + logL(j) - S) - mu_t
                    var_t = var_t + term*term
                end do
                var_t = var_t/4.0_real64

                T_sum   = T_sum   + i_true
                MU_sum  = MU_sum  + mu_t
                VAR_sum = VAR_sum + var_t
            end do

            ! CLT approximation to Pr[ sum_t i(X_bar_t;r_t) >= T_sum ]
            tail = 0.5_real64*erfc( (T_sum-MU_sum) / sqrt(2.0_real64*VAR_sum) )
            bound_val = min(1.0_real64, Mm1*tail)
            acc = acc + bound_val
        end do
        !$omp end parallel do

        rcu_out = acc/real(ntrials, real64)
    end subroutine compute_rcu

    pure function logsumexp4(v) result(s)
        real(real64), intent(in) :: v(0:3)
        real(real64) :: s, m
        m = maxval(v)
        s = m + log(sum(exp(v-m)))
    end function logsumexp4

    ! log(I0(x)), x>=0, via the standard Abramowitz & Stegun 9.8.1/9.8.2
    ! rational approximations (~1.6e-7 relative accuracy), evaluated in log
    ! space for large x to avoid overflowing exp(x).
    pure function log_bessel_i0(x) result(logi0)
        real(real64), intent(in) :: x
        real(real64) :: logi0, ax, y, bessi0

        ax = abs(x)
        if (ax < 3.75_real64) then
            y = (x/3.75_real64)**2
            bessi0 = 1.0_real64 + y*(3.5156229_real64 + y*(3.0899424_real64 + &
                     y*(1.2067492_real64 + y*(0.2659732_real64 + &
                     y*(0.0360768_real64 + y*0.0045813_real64)))))
            logi0 = log(bessi0)
        else
            y = 3.75_real64/ax
            logi0 = ax - 0.5_real64*log(ax) + &
                    log(0.39894228_real64 + y*(0.01328592_real64 + &
                    y*(0.00225319_real64 + y*(-0.00157565_real64 + &
                    y*(0.00916281_real64 + y*(-0.02057706_real64 + &
                    y*(0.02635537_real64 + y*(-0.01647633_real64 + &
                    y*0.00392377_real64))))))))
        end if
    end function log_bessel_i0

    subroutine box_muller(rand_normal)
        real(real32), intent(out) :: rand_normal
        real(real32) :: u1, u2
        u1 = thread_uniform(); u2 = thread_uniform()
        if (u1 < 1.0e-30_real32) u1 = 1.0e-30_real32
        rand_normal = sqrt(-2.0_real32*log(u1)) * cos(6.28318530718_real32*u2)
    end subroutine box_muller

    function thread_uniform() result(u)
        real(real32) :: u
        rng_state = ieor(rng_state, ishft(rng_state, 13))
        rng_state = ieor(rng_state, ishft(rng_state, -17))
        rng_state = ieor(rng_state, ishft(rng_state, 5))
        u = real(iand(rng_state, huge(rng_state)), real32) / real(huge(rng_state), real32)
    end function thread_uniform

    subroutine seed_random_generator()
        !$omp parallel default(shared)
        rng_state = 9999_int32 + 104729_int32*int(omp_get_thread_num(), int32)
        if (rng_state == 0_int32) rng_state = 1_int32
        !$omp end parallel
    end subroutine seed_random_generator

end program rcu_bound_4fsk
