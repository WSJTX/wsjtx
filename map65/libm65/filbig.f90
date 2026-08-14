module filbig_mod
  use iso_fortran_env, only: real32, real64
  use iso_c_binding, only:c_intptr_t
  use timer_module,   only: timer
  use debug_log
  use cacb_mod
  use npar_ptrs_mod,  only: nrate_active, nfft_big_active
  use fftw3_constants
  use fftw3_f77_interfaces
  implicit none
  
  integer, parameter :: MAXFFT1 = 56*192000
  integer, parameter :: MAXFFT2 = 2*77175

  complex, save :: c4a_buf(MAXFFT2), c4b_buf(MAXFFT2), cfilt_buf(MAXFFT2)
  integer(c_intptr_t), save :: plan1, plan2, plan3, plan4, plan5
  logical, save :: filbig_first = .true.

contains

  subroutine filbig(dd, nmax, f0, newdat, nfsample, xpol, c4a, c4b, n4)

  ! Filter and downsample complex data stored in array dd(4,nmax).
  ! Output is downsampled from nrate_active Hz to 1378.125 Hz (same decimation ratio as 96 kHz). 
    
    integer,   intent(in)    :: nmax
    real(real32),    intent(in)    :: dd(4, nmax)
    real(real64),    intent(in)    :: f0
    integer,   intent(inout) :: newdat
    integer,   intent(in)    :: nfsample
    logical,   intent(in)    :: xpol
    complex,   intent(out)   :: c4a(MAXFFT2), c4b(MAXFFT2)
    integer,   intent(out)   :: n4

    real(real64) :: df
    real halfpulse(8)                       !Impulse response (one sided)
    real base, fac, filtval
    integer i, i0, j, nfft1, nfft2, nflags, nh, npatience, nz
    real(real64) :: decim
    ! Filter in frequency domain; sized at max short-FFT length
    data npatience/1/
    data halfpulse/114.97547150, 36.57879257, -20.93789101, &
       5.89886379, 1.59355187, -2.49138308, 0.60910773, -0.04248129/

    save

    ! ca/cb storage is sized at MAXFFT1 in cacb_mod; the *active* big FFT
    ! length is nfft_big_active, set from C++ via set_runtime_params_().
    call init_cacb(MAXFFT1)

    if (nmax .lt. 0) go to 900

    ! Runtime big-FFT length: nfft_big_active is 56 * nrate_active, set
    ! from C++ at startup. Clamp to MAXFFT1 for safety.
    nfft1 = min(56*nrate_active, MAXFFT1)
    decim = real(nrate_active, kind=real64) / 1378.125_real64
    ! Preserve the original decimation ratio between nfft1 and nfft2.
    ! At 96 kHz:  nfft1 = 5376000, nfft2 = 77175  ? ratio � 69.64
    ! At 192 kHz: nfft1 doubles, so nfft2 should double to keep the
    ! same time span / bandwidth in the downsampled stream.
    !
    ! Use the 96 kHz baseline ratio to derive nfft2 for any rate:
    !   nfft2 � nfft1 * (77175 / 5376000)
    
    ! Choose nfft2 so that the time span in c4a is the same as in the 96 kHz case
    ! Baseline: at 96 kHz, nfft1=5,376,000, nfft2=77,175
    ! nfft2 = int( real(nfft1, kind=real64) * 77175.0_real64 / 5376000.0_real64 )
    nfft2 = int(MAXFFT2/2)
    nfft2 = min(nfft2, MAXFFT2)
    if (nfft2 .gt. MAXFFT2) nfft2 = MAXFFT2
    
    ! !  call dbg('FILBIG: nfft1=' // itoa(nfft1) // ' nfft2=' // itoa(nfft2))
!  call dbg('FILBIG: nfsample=' // itoa(nfsample) // &
    !     ' nrate_active=' // itoa(nrate_active) // &
    !     ' nmax=' // itoa(nmax) // &
    !     ' nfft1=' // itoa(nfft1) // &
    !     ' nfft2=' // itoa(nfft2))


    ! Preserve the legacy 95238 special case (Linrad odd rate); if you
    ! still use it, override the derived sizes with the historical ones.
    if (nfsample .eq. 95238) then
       nfft1 = min(5120000, MAXFFT1)
       nfft2 = min(74088,   MAXFFT2)
    endif

    if (filbig_first) then
       nflags = FFTW_ESTIMATE
       if (npatience .eq. 1) nflags = FFTW_ESTIMATE_PATIENT
       if (npatience .eq. 2) nflags = FFTW_MEASURE
       if (npatience .eq. 3) nflags = FFTW_PATIENT
       if (npatience .eq. 4) nflags = FFTW_EXHAUSTIVE

  ! Plan the FFTs just once
       call timer('FFTplans', 0)
       call sfftw_plan_dft_1d(plan1, nfft1, ca, ca, FFTW_BACKWARD, nflags)
       call sfftw_plan_dft_1d(plan2, nfft1, cb, cb, FFTW_BACKWARD, nflags)
       call sfftw_plan_dft_1d(plan3, nfft2, c4a_buf,  c4a_buf,  FFTW_FORWARD,  nflags)
       call sfftw_plan_dft_1d(plan4, nfft2, c4b_buf,  c4b_buf,  FFTW_FORWARD,  nflags)
       call sfftw_plan_dft_1d(plan5, nfft2, cfilt_buf,cfilt_buf,FFTW_BACKWARD, nflags)
       call timer('FFTplans', 1)

  ! Convert impulse response to filter function
       do i = 1, nfft2
          cfilt_buf(i) = 0.0
       enddo
       fac = 0.00625/nfft1
       cfilt_buf(1) = fac*halfpulse(1)
       do i = 2, 8
          cfilt_buf(i)             = fac*halfpulse(i)
          cfilt_buf(nfft2 + 2 - i) = fac*halfpulse(i)
       enddo
       call sfftw_execute(plan5)

       base = real(cfilt_buf(nfft2/2 + 1))

       ! Bin width of the big FFT: use the active runtime rate instead
       ! of a hardcoded 96000. The 95238 special case is preserved.
       df = dble(nrate_active)/dble(nfft1)
       if (nfsample .eq. 95238) df = 95238.1d0/dble(nfft1)
       filbig_first = .false.
    endif

  ! When new data comes along, we need to compute a new "big FFT"
  ! If we just have a new f0, continue with the existing ca and cb.

    if (newdat .ne. 0 .or. sum(abs(ca)) .eq. 0.0) then  !### Test on ca should be unnecessary?
       nz = min(nmax, nfft1)
       do i = 1, nz
          ca(i) = cmplx(dd(1, i), dd(2, i))
          if (xpol) cb(i) = cmplx(dd(3, i), dd(4, i))
       enddo

       if (nmax .lt. nfft1) then
          do i = nmax + 1, nfft1
             ca(i) = 0.0
             if (xpol) cb(i) = 0.0
          enddo
       endif
       call timer('FFTbig  ', 0)
       call sfftw_execute(plan1)
       if (xpol) call sfftw_execute(plan2)
       call timer('FFTbig  ', 1)
       newdat = 0
    endif

  ! NB: f0 is the frequency at which we want our filter centered.
  !     i0 is the bin number in ca and cb closest to f0.

    i0 = nint(f0/df) + 1
    nh = nfft2/2

    ! Lower half of filter
    do i = 1, nh
       j = i0 + i - 1
       if (j .ge. 1 .and. j .le. nfft1) then
          filtval = real(cfilt_buf(i)) - base
          c4a_buf(i) = filtval*ca(j)
          if (xpol) c4b_buf(i) = filtval*cb(j)
       else
          c4a_buf(i) = 0.0
          if (xpol) c4b_buf(i) = 0.0
       endif
    enddo

    ! Upper half of filter, with wrap-around and bounds check
    do i = nh + 1, nfft2
       j = i0 + i - 1 - nfft2
       if (j .lt. 1) j = j + nfft1

       if (j < 1 .or. j > size(ca)) then
          write (dbg_unit, *) 'FILBIG OOB: i=', i, ' j=', j, ' nh=', nh, &
             ' nfft1=', nfft1, ' nfft2=', nfft2, ' i0=', i0
          flush(dbg_unit)
          stop 'FILBIG index OOB'
       end if

       filtval = real(cfilt_buf(i)) - base
       c4a_buf(i) = filtval*ca(j)
       if (xpol) c4b_buf(i) = filtval*cb(j)
    enddo

  ! Do the short reverse transform, to go back to time domain.
    call timer('FFTsmall', 0)
    call sfftw_execute(plan3)
    if (xpol) call sfftw_execute(plan4)
    call timer('FFTsmall', 1)

    c4a(1:nfft2) = c4a_buf(1:nfft2)
    if (xpol) c4b(1:nfft2) = c4b_buf(1:nfft2)

    ! Number of valid output samples at ~1375 Hz
    n4 = min( int( real(nmax, kind=real64) / decim ), nfft2 )
    n4 = min( int( real(nmax, kind=real64) / decim ), nfft2 )
 
    !  call dbg('FILBIG: decim=' // rtoa(real(decim)) // &
    !     ' n4=' // itoa(n4))


!!  call dbg('FILBIG: nrate_active=' // itoa(nrate_active) // &
!         ' nfsample=' // itoa(nfsample) // &
!         ' nmax=' // itoa(nmax) // &
!         ' nfft1=' // itoa(nfft1) // &
!         ' nfft2=' // itoa(nfft2) // &
!         ' decim=' // rtoa(real(decim)) // &
!         ' n4=' // itoa(n4))

    
    ! !  call dbg('FILBIG: n4=' // itoa(n4) // ' nmax=' // itoa(nmax))
    
    go to 999

  900   call sfftw_destroy_plan(plan1)
    call sfftw_destroy_plan(plan2)
    call sfftw_destroy_plan(plan3)
    call sfftw_destroy_plan(plan4)
    call sfftw_destroy_plan(plan5)

  999   return
  end subroutine filbig

end module filbig_mod


