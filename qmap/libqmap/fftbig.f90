subroutine fftbig(dd,nmax)

! Do the full length FFT of complex data stored in array dd(2,nmax).  

  use, intrinsic :: iso_c_binding
  
  use FFTW3
  use timer_module, only: timer
  use cacb_mod, only: ca, init_cacb, free_cacb
  parameter (MAXFFT1=5376000)
  real*4  dd(2,nmax)                         !Input data
  real*8 df
  type(C_PTR) :: plan1                       !Pointer to FFTW plan
  logical first
  equivalence (rfilt,cfilt)
  data first/.true./,npatience/0/
  save

  if(nmax.lt.0) go to 900

  nfft1=MAXFFT1
  call init_cacb(nfft1)
  if(first) then
     nflags=FFTW_ESTIMATE
     if(npatience.eq.1) nflags=FFTW_ESTIMATE_PATIENT
     if(npatience.eq.2) nflags=FFTW_MEASURE
     if(npatience.eq.3) nflags=FFTW_PATIENT
     if(npatience.eq.4) nflags=FFTW_EXHAUSTIVE
     
! Plan the big FFT just once
     call timer('FFTplan ',0)
     plan1=fftwf_plan_dft_1d(nfft1,ca,ca,+1,nflags)
     call timer('FFTplan ',1)
     df=96000.d0/nfft1
     first=.false.
  endif

  nz=min(nmax,nfft1)
  do i=1,nz
     ca(i)=cmplx(dd(1,i),dd(2,i))
  enddo

  if(nmax.lt.nfft1) then
     do i=nmax+1,nfft1
        ca(i)=0.
     enddo
  endif
  call timer('FFTbig  ',0)
  call fftwf_execute_dft(plan1,ca,ca)
  call timer('FFTbig  ',1)
  go to 999

900 if(.not.first) call fftwf_destroy_plan(plan1)
  call free_cacb()
  first=.true.

999 return
end subroutine fftbig
