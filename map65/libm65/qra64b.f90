subroutine qra64b(nutc,nqd,fcenter,nfcal,nfsample,ikhz,mousedf,ntol,xpol,  &
     mycall_12,hiscall_12,hisgrid_6,mode64,nwrite_qra64)

  parameter (MAXFFT1=5376000)              !56*96000
  parameter (MAXFFT2=336000)               !56*6000 (downsampled by 1/16)
  complex ca(MAXFFT1),cb(MAXFFT1)            !FFTs of raw x,y data
  complex cx(0:MAXFFT2-1),cy(0:MAXFFT2-1)
  logical xpol
  real*8 fcenter
  character*12 mycall_12,hiscall_12
  character*6 hisgrid_6
  common/cacb/ca,cb
  data nzap/3/

  open(17,file='red.dat',status='unknown')

  nfft1=MAXFFT1
  nfft2=MAXFFT2
  df=96000.0/NFFT1
  if(nfsample.eq.95238) then
     nfft1=5120000
     nfft2=322560
     df=96000.0/nfft1
  endif
  nh=nfft2/2
  ikhz0=nint(1000.0*(fcenter-int(fcenter)))
  k0=((ikhz-ikhz0+48.0+1.27)*1000.0+nfcal)/df
  if(k0.lt.nh .or. k0.gt.nfft1-nh) go to 900

  fac=1.0/nfft2
  cx(0:nh)=ca(k0:k0+nh)
  cx(nh+1:nfft2-1)=ca(k0-nh+1:k0-1)
  cx=fac*cx
  if(xpol) then
     cy(0:nh)=cb(k0:k0+nh)
     cy(nh+1:nfft2-1)=cb(k0-nh+1:k0-1)
     cy=fac*cy
  endif

! Here cx and cy (if xpol) are frequency-domain data around the selected
! QSO frequency, taken from the full-length FFT computed in filbig().
! Values for fsample, nfft1, nfft2, df, and the downsampled data rate
! are as follows:

!  fSample  nfft1       df        nfft2  fDownSampled
!    (Hz)              (Hz)                 (Hz)
!----------------------------------------------------
!   96000  5376000  0.017857143  336000   6000.000
!   95238  5120000  0.018601172  322560   5999.994

!  write(60) cx,cy,nutc,nqd,ikhz,mousedf,ntol,xpol,mycall_12,           &
!       hiscall_12,hisgrid_6

  if(nzap.gt.0) call qra64zap(cx,cy,xpol,nzap)

! Transform back to time domain with sample rate 6000 Hz.
  call four2a(cx,nfft2,1,-1,1)
  call four2a(cy,nfft2,1,-1,1)

  call qra64c(cx,cy,nutc,nqd,ikhz,mousedf,ntol,xpol,mycall_12,    &
       hiscall_12,hisgrid_6,mode64,nwrite_qra64)
  close(17)
  
900 return
end subroutine qra64b
