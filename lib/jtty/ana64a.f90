subroutine ana64a(iwave,npts,c0)

  parameter (NMAX=20*12000)                 !Max length of data @12000 Hz
  parameter (NZ=20*6000)                    !Max length of data @6000 Hz
  parameter (NFFT1=262144)
  integer*2 iwave(NMAX)                     !Raw data at 12000 Hz
  complex c0(0:NFFT1-1)                     !Complex data at 6000 Hz
  save

  nfft2=NFFT1/2
  df1=12000.0/NFFT1
  fac=2.0/(32767.0*NFFT1)
  c0(0:npts-1)=fac*iwave(1:npts)
  c0(npts:)=0.
!  print*,'bb2',NFFT1,nfft2,npts,sum(dfloat(abs(iwave(1:npts)))),sum(abs(c0))
  call four2a(c0,NFFT1,1,-1,1)             !Forward c2c FFT
  c0(nfft2/2+1:nfft2-1)=0.                 !Set negative freqs to 0.
  c0(0)=0.5*c0(0)
  call four2a(c0,nfft2,1,1,1)              !Inverse c2c FFT; c0 is the analytic sig
  c0(npts:)=0.
!  print*,'bb3',NFFT1,nfft2,sum(abs(c0))

  return
end subroutine ana64a
