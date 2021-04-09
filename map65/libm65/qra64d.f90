program qra64d

  use packjt
  parameter (NFFT2=336000)               !56*6000 (downsampled by 1/16)
  parameter (NMAX=60*12000,LN=1152*63)

  character decoded*22
  character*12 mycall_12,hiscall_12
  character*6 mycall,hiscall,hisgrid_6
  character*4 hisgrid
  character*1 cp
  logical ltext
  complex cx(0:NFFT2-1),cy(0:NFFT2-1)
  complex c00(0:720000)                      !Complex spectrum of dd()
  complex c0(0:720000)                       !Complex data for dd()
  real a(3)
  real s3(LN)                                !Symbol spectra
  real s3a(LN)                               !Symbol spectra
  integer dat4(12)                           !Decoded message (as 12 integers)
  integer dat4x(12)
  integer nap(0:11)
  data nap/0,2,3,2,3,4,2,3,6,4,6,6/
  data nc1z/-1/,nc2z/-1/,ng2z/-1/,maxaptypez/-1/
  common/tracer/ limtrace,lu

  limtrace=0
  lu=12
  open(12,file='timer.out',status='unknown')
  call timer('qra64d  ',0)
  nzap=1

1 read(60,end=900) cx,cy,nutc,nqd,ikhz,mousedf,ntol,xpol,mycall_12,    &
       hiscall_12,hisgrid_6

! Eliminate birdies:  
  if(nzap.gt.0) call qra64zap(cx,cy,nzap)

! Transform back to time domain with sample rate 6000 Hz.
  call four2a(cx,NFFT2,1,-1,1)
  call four2a(cy,NFFT2,1,-1,1)

  call qra64c(cx,cy,nutc,nqd,ikhz,mousedf,ntol,xpol,mycall_12,         &
       hiscall_12,hisgrid_6,nwrite_qra64)
  goto 1
900 call timer('qra64d  ',1)
  call timer('qra64d  ',101)

end program qra64d
