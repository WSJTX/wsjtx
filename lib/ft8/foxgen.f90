subroutine foxgen()

  ! Called from MainWindow::foxTxSequencer() to generate the Tx waveform in
  ! FT8 Fox mode.  The Tx message can contain up to 5 "slots", each carrying
  ! its own FT8 signal.
  
  ! Encoded messages can be of the form "HoundCall FoxCall rpt" (a standard FT8
  ! message with i3bit=0) or "HoundCall_1 RR73; HoundCall_2 <FoxCall> rpt", 
  ! a new message type with i3bit=1.  The waveform is generated with
  ! fsample=48000 Hz; it is compressed to reduce the PEP-to-average power ratio,
  ! with (currently disabled) filtering afterware to reduce spectral growth.

  ! Input message information is provided in character array cmsg(5), in
  ! common/foxcom/.  The generated wave(NWAVE) is passed back in the same
  ! common block.
  
  parameter (NN=79,ND=58,NSPS=4*1920)
  parameter (NWAVE=(160+2)*134400*4) !the biggest waveform we generate (FST4-1800 at 48kHz)
  parameter (NFFT=614400,NH=NFFT/2)
  character*40 cmsg
  character*37 msg,msgsent
  integer itone(79)
  integer*1 msgbits(77),msgbits2
  integer*1, target:: mycall
  real x(NFFT)
  real*8 dt,twopi,f0,fstep,dfreq,phi,dphi
  complex cx(0:NH)
  common/foxcom/wave(NWAVE),nslots,nfreq,i3bit(5),cmsg(5),mycall(12)
  common/foxcom2/itone2(NN),msgbits2(77)
  equivalence (x,cx),(y,cy)

  fstep=60.d0
  dfreq=6.25d0
  dt=1.d0/48000.d0
  twopi=8.d0*atan(1.d0)
  irpt=0
  nplot=0
  wave=0.

  do n=1,nslots
     msg=cmsg(n)(1:37)
     call genft8(msg,i3,n3,msgsent,msgbits,itone)
! Make copies of itone() and msgbits() for ft8sim
     itone2=itone
     msgbits2=msgbits
     f0=nfreq + fstep*(n-1)
     phi=0.d0
     k=0
     do j=1,NN
        f=f0 + dfreq*itone(j)
        dphi=twopi*f*dt
        do ii=1,NSPS
           k=k+1
           phi=phi+dphi
           xphi=phi
           wave(k)=wave(k)+sin(xphi)
        enddo
     enddo
  enddo
  kz=k
  
  peak1=maxval(abs(wave))
  wave=wave/peak1
  width=50.0
  call foxfilt(nslots,nfreq,width,wave)
  peak3=maxval(abs(wave))
  wave=wave/peak3
  
  return
end subroutine foxgen

! include 'plotspec.f90'
