subroutine ana64(iwave,npts,c0)

  use timer_module, only: timer

  integer*2 iwave(npts)                      !Raw data at 12000 Hz
  complex c0(0:npts-1)                       !Complex data at 6000 Hz
  save

  nfft1=npts
  nfft2=nfft1/2
  df1=12000.0/nfft1
  fac=2.0/(32767.0*nfft1)
  c0(0:npts-1)=fac*iwave(1:npts)
  call four2a(c0,nfft1,1,-1,1)             !Forward c2c FFT
  c0(nfft2/2+1:nfft2-1)=0.
  c0(0)=0.5*c0(0)
  call four2a(c0,nfft2,1,1,1)              !Inverse c2c FFT; c0 is analytic sig

  return
end subroutine ana64
