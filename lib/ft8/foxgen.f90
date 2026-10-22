module fox_tx
  use packjt77, only: pack77_state
  use ft8_codec_context, only: genft8_for_state
  implicit none
  private
  public :: foxgen_for_state

  integer, parameter :: NN=79,NSPS=4*1920

contains

! foxgen's waveform for the Fox messages cmsg(1:nslots), packed with state's
! hash tables, into wave(1:NN*NSPS); msgsent, itone and msgbits are each
! slot's.
subroutine foxgen_for_state(state,nslots,cmsg,nfreq,wave,msgsent,itone,msgbits)
  type(pack77_state), target, intent(inout) :: state
  integer, intent(in) :: nslots,nfreq
  character*40, intent(in) :: cmsg(5)
  real, intent(inout) :: wave(NN*NSPS)
  character*37, intent(out) :: msgsent(5)
  integer, intent(out) :: itone(NN,5)
  integer*1, intent(out) :: msgbits(77,5)
  character*37 msg
  integer n,i3,n3,j,ii,k
  real f,xphi,peak1,peak3,width
  real*8 dt,twopi,f0,fstep,dfreq,phi,dphi

  fstep=60.d0
  dfreq=6.25d0
  dt=1.d0/48000.d0
  twopi=8.d0*atan(1.d0)
  wave=0.

  do n=1,nslots
     msg=cmsg(n)(1:37)
     call genft8_for_state(state,msg,i3,n3,msgsent(n),msgbits(:,n),itone(:,n))
     f0=nfreq + fstep*(n-1)
     phi=0.d0
     k=0
     do j=1,NN
        f=f0 + dfreq*itone(j,n)
        dphi=twopi*f*dt
        do ii=1,NSPS
           k=k+1
           phi=phi+dphi
           xphi=phi
           wave(k)=wave(k)+sin(xphi)
        enddo
     enddo
  enddo

  peak1=maxval(abs(wave))
  wave=wave/peak1
  width=50.0
  call foxfilt(nslots,nfreq,width,wave)
  peak3=maxval(abs(wave))
  wave=wave/peak3

end subroutine foxgen_for_state

end module fox_tx

subroutine foxgen(bSuperFox,fname)

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
  
  use fox_tx, only: foxgen_for_state
  use ft8_codec_context, only: get_ft8_codec_state
  use packjt77, only: pack77_state
  parameter (NN=79,ND=58,NSPS=4*1920)
  parameter (NWAVE=(160+2)*134400*4) !the biggest waveform we generate (FST4-1800 at 48kHz)
  logical*1 bSuperFox,bMoreCQs,bSendMsg
  character*(*) fname
  character*40 cmsg,cmsg2
  character*26 textMsg
  character*37 msgsent(5)
  integer itone(NN,5)
  integer*1 msgbits(77,5),msgbits2
  integer*1, target:: mycall
  type(pack77_state), pointer :: state
  common/foxcom/wave(NWAVE),nslots,nfreq,i3bit(5),cmsg(5),mycall(12),  &
       textMsg,bMoreCQs,bSendMsg
  common/foxcom2/itone2(NN),msgbits2(77)
  common/foxcom3/nslots2,cmsg2(5),itone3(151)

  if(bSuperFox) then
     n=nslots
     if(bMoreCQs) cmsg(1)(40:40)='1'               !Set flag to include a CQ
     if(bSendMsg) then
        n=min(nslots+1,5)
        cmsg(n)=textMsg
        cmsg(n)(39:39)='1'                         !Set flag for text message
        nslots=n
     endif
     nslots2=nslots
     cmsg2=cmsg
     go to 999
  endif

  wave=0.
  state => get_ft8_codec_state()
  call foxgen_for_state(state,nslots,cmsg,nfreq,wave,msgsent,itone,msgbits)
! Make copies of the last slot's itone() and msgbits() for ft8sim
  if(nslots.ge.1) then
     itone2=itone(:,nslots)
     msgbits2=msgbits(:,nslots)
  endif

999 return
end subroutine foxgen

! include 'plotspec.f90'
