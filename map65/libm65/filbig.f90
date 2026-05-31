module filbig_mod
  use iso_fortran_env, only: real32, real64
  use iso_c_binding, only:c_intptr_t
  use timer_module,   only: timer
  use debug_log
  use cacb_mod
  use fftw3_constants
  use fftw3_f77_interfaces
  implicit none
contains

  subroutine filbig(dd, nmax, f0, newdat, nfsample, xpol, c4a, c4b, n4)

  ! Filter and downsample complex data stored in array dd(4,nmax).
  ! Output is downsampled from 96000 Hz to 1375.125 Hz.
    
    integer, parameter :: MAXFFT1 = 5376000, MAXFFT2 = 77175
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
    complex cfilt(MAXFFT2)                     !Filter in frequency domain
    integer(c_intptr_t) plan1, plan2, plan3, plan4, plan5
    logical first
    data first/.true./, npatience/1/
    data halfpulse/114.97547150, 36.57879257, -20.93789101, &
       5.89886379, 1.59355187, -2.49138308, 0.60910773, -0.04248129/

    save

    call init_cacb(5376000)

    if (nmax .lt. 0) go to 900

    nfft1 = MAXFFT1
    nfft2 = MAXFFT2
    if (nfsample .eq. 95238) then
       nfft1 = 5120000
       nfft2 = 74088
    endif

    if (first) then
       nflags = FFTW_ESTIMATE
       if (npatience .eq. 1) nflags = FFTW_ESTIMATE_PATIENT
       if (npatience .eq. 2) nflags = FFTW_MEASURE
       if (npatience .eq. 3) nflags = FFTW_PATIENT
       if (npatience .eq. 4) nflags = FFTW_EXHAUSTIVE

  ! Plan the FFTs just once
       call timer('FFTplans ', 0)
       call sfftw_plan_dft_1d(plan1, nfft1, ca, ca, FFTW_BACKWARD, nflags)
       call sfftw_plan_dft_1d(plan2, nfft1, cb, cb, FFTW_BACKWARD, nflags)
       call sfftw_plan_dft_1d(plan3, nfft2, c4a, c4a, FFTW_FORWARD, nflags)
       call sfftw_plan_dft_1d(plan4, nfft2, c4b, c4b, FFTW_FORWARD, nflags)
       call sfftw_plan_dft_1d(plan5, nfft2, cfilt, cfilt, FFTW_BACKWARD, nflags)
       call timer('FFTplans ', 1)

  ! Convert impulse response to filter function
       do i = 1, nfft2
          cfilt(i) = 0.0
       enddo
       fac = 0.00625/nfft1
       cfilt(1) = fac*halfpulse(1)
       do i = 2, 8
          cfilt(i) = fac*halfpulse(i)
          cfilt(nfft2 + 2 - i) = fac*halfpulse(i)
       enddo
       call sfftw_execute(plan5)

       base = real(cfilt(nfft2/2 + 1))

       df = 96000.d0/nfft1
       if (nfsample .eq. 95238) df = 95238.1d0/nfft1
       first = .false.
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
          filtval = real(cfilt(i)) - base
          c4a(i) = filtval*ca(j)
          if (xpol) c4b(i) = filtval*cb(j)
       else
          c4a(i) = 0.0
          if (xpol) c4b(i) = 0.0
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

       filtval = real(cfilt(i)) - base
       c4a(i) = filtval*ca(j)
       if (xpol) c4b(i) = filtval*cb(j)
    enddo

  ! Do the short reverse transform, to go back to time domain.
    call timer('FFTsmall', 0)
    call sfftw_execute(plan3)
    if (xpol) call sfftw_execute(plan4)
    call timer('FFTsmall', 1)
    n4 = min(nmax/64, nfft2)
    go to 999

  900   call sfftw_destroy_plan(plan1)
    call sfftw_destroy_plan(plan2)
    call sfftw_destroy_plan(plan3)
    call sfftw_destroy_plan(plan4)
    call sfftw_destroy_plan(plan5)

  999   return
  end subroutine filbig

end module filbig_mod

