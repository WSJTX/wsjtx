subroutine ft2_gfsk_iwave(msg37,f0,snrdb,iwave)

! Generate waveform for experimental "FT2" mode 

  use packjt77
  include 'ft2_params.f90'               !Set various constants
  parameter (NWAVE=(NN+2)*NSPS)
  character msg37*37,msgsent37*37
  real wave(NWAVE),xnoise(NWAVE)
  real dphi(NWAVE)
  real pulse(480)

  integer itone(NN)
  integer*2 iwave(NWAVE)                 !Generated full-length waveform
  logical first
  data first/.true./
  save pulse
 
  twopi=8.0*atan(1.0)
  fs=12000.0                             !Sample rate (Hz)
  dt=1.0/fs                              !Sample interval (s)
  hmod=0.8                               !Modulation index (MSK=0.5, FSK=1.0)
  tt=NSPS*dt                             !Duration of symbols (s)
  baud=1.0/tt                            !Keying rate (baud)
  bw=1.5*baud                            !Occupied bandwidth (Hz)
  txt=NZ*dt                              !Transmission length (s)
  bandwidth_ratio=2500.0/(fs/2.0)
!  sig=sqrt(2*bandwidth_ratio) * 10.0**(0.05*snrdb)
!  if(snrdb.gt.90.0) sig=1.0
  txt=NN*NSPS/12000.0

  if(first) then
! The filtered frequency pulse
     do i=1,480
        tt=(i-240.5)/160.0
        pulse(i)=gfsk_pulse(1.0,tt)
     enddo
     dphi_peak=twopi*(hmod/2.0)/real(NSPS)
     first=.false.
  endif

! Source-encode, then get itone():
  itype=1
  call genft2(msg37,0,msgsent37,itone,itype)

! Create the instantaneous frequency waveform
  dphi=0.0
  do j=1,NN
     ib=(j-1)*160+1
     ie=ib+480-1
     dphi(ib:ie)=dphi(ib:ie)+dphi_peak*pulse*(2*itone(j)-1)
  enddo

  phi=0.0
  wave=0.0
  sqrt2=sqrt(2.)
  dphi=dphi+twopi*f0*dt
  do j=1,NWAVE
     wave(j)=sqrt2*sin(phi)
     sqsig=sqsig + wave(j)**2
     phi=mod(phi+dphi(j),twopi)
  enddo
  wave(1:160)=wave(1:160)*(1.0-cos(twopi*(/(i,i=0,159)/)/320.0) )/2.0
  wave(145*160+1:146*160)=wave(145*160+1:146*160)*(1.0+cos(twopi*(/(i,i=0,159)/)/320.0 ))/2.0
  wave(146*160+1:)=0.

  if(snrdb.gt.90.0) then
     iwave=nint((32767.0/sqrt(2.0))*wave)
     return
  endif
  
  sqnoise=1.e-30
  if(snrdb.lt.90) then
     do i=1,NWAVE                   !Add gaussian noise at specified SNR
        xnoise(i)=gran()            !Noise has rms = 1.0
     enddo
  endif
  xnoise=xnoise*sqrt(0.5*fs/2500.0)
  fac=30.0
  snr_amplitude=10.0**(0.05*snrdb)
  wave=fac*(snr_amplitude*wave + xnoise)
  datpk=maxval(abs(wave))
  print*,'A',snr_amplitude,datpk

  iwave=nint((30000.0/datpk)*wave)
  
  return
end subroutine ft2_gfsk_iwave
