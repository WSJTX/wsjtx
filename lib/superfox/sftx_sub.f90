module sfox_tx
  use packjt77, only: pack77_state
  private
  public :: sftx_for_state, sfox_wave_for_tones

contains

! sftx_sub for the old-style Fox messages cmsg(1:nslots), packing with
! knowledge's hash tables: it parses up to five messages for MyCall, up to 9
! Hound calls and the report or RR73 each gets, and encodes them as one
! SuperFox message, the channel symbols itone(1:151), or sets pack_error.
subroutine sftx_for_state(knowledge,ckey0,nslots,cmsg,itone,pack_error)

  use qpc_mod
  use sfox_mod
  use sfox_pack_module, only: sfox_pack_for_state
  type(pack77_state), intent(inout) :: knowledge
  character*120 line                  !List of SuperFox message pieces
  character*40 cmsg(5)                !Old-style Fox messages
  character*26 freeTextMsg
  character*(*) ckey0
  character*10 ckey
  character*11 foxcall
  logical*1 bMoreCQs,bSendMsg
  integer pack_error
  integer*1 xin(0:49)                 !Packed message as 7-bit symbols
  integer*1 y(0:127)                  !Encoded symbols as i*1 integers
  integer chansym0(127)               !Transmitted symbols, data only
  integer isync(24)                   !Symbol numbers for sync tones
  integer itone(151)
  data isync/1,2,4,7,11,16,22,29,37,39,42,43,45,48,52,57,63,70,78,80,  &
             83,84,86,89/

  ckey=ckey0

  fsample=12000.0
  call sfox_init(7,127,50,'no',fspread,delay,fsample,24)
  freeTextMsg='                          '
  bMoreCQs=cmsg(1)(40:40).eq.'1'
  bSendMsg=cmsg(nslots)(39:39).eq.'1'
  nDataSlots=nslots
  if(bSendMsg) then
     freeTextMsg=cmsg(nslots)(1:26)
     nDataSlots=nslots-1
     if(nDataSlots.gt.4) nDataSlots=4
  endif

  call foxgen2(nDataSlots,cmsg,line,foxcall)    !Parse old-style Fox messages

! Pack message information and CRC into xin(0:49)
  call sfox_pack_for_state(knowledge,line,ckey,bMoreCQs,bSendMsg,freeTextMsg, &
       xin,pack_error)
  if(pack_error.ne.0) then
     itone=-99
     return
  endif
  call qpc_encode(y,xin)                    !Encode the message to 128 symbols
  y=cshift(y,1)                             !Puncture the code by removing y(0)
  y(127)=0
  chansym0=y(0:126)

! Create the full itone sequence containing both data and sync symbols
  j=1
  k=0
  do i=1,NDS
     if(j.le.NS .and. i.eq.isync(j)) then
        if(j.lt.NS) j=j+1       !Index for next sync symbol
        itone(i)=0              !Insert sync symbol at tone 0
     else
        k=k+1
        itone(i)=chansym0(k) + 1    !Symbol value 0 transmitted as tone 1, etc.
     endif
  enddo

end subroutine sftx_for_state

! sfox_wave_gfsk's waveform for the SuperFox tones itone(1:151), into
! wave(1:(151+2)*4096), which is left zero for tones it does not send: GFSK
! with short ramp-up and ramp-down symbols of duration NSPS/BT at the
! beginning and end of the waveform.
subroutine sfox_wave_for_tones(itone,wave)
  use iso_fortran_env, only: real32,real64
  use waveform_kernels, only: waveform_workspace,render_gfsk,apply_waveform_ramp
  implicit none
  integer, parameter :: nsym=151,nsps=4096,npts=(nsym+2)*nsps,nramp=nsps/8
  real(real64), parameter :: twopi=8.0_real64*atan(1.0_real64)
  real(real64), parameter :: carrier=twopi*750.0_real64*(1.0_real64/48000)
  integer, intent(in) :: itone(nsym)
  real(real32), intent(out) :: wave(npts)
  type(waveform_workspace) :: workspace
  integer :: k1

  if(itone(1).lt.0.or.itone(1).gt.128) then
     wave=0
     return
  endif

  ! SuperFox advances from source sample 1 after emitting its initial zero phase.
  call render_gfsk(workspace,itone,nsps,8.0_real32,1,carrier, &
       1,npts-1,1,.true.,.false.,wave=wave)
  wave(1:nsps-nramp)=0
  call apply_waveform_ramp(wave,1,nsps-nramp,nramp,real(2*nramp,real64),-1.0_real64)
  k1=(nsym+1)*nsps+1
  call apply_waveform_ramp(wave,1,k1-1,nramp,real(2*nramp,real64),1.0_real64)
  wave(k1+nramp:npts)=0
end subroutine sfox_wave_for_tones

end module sfox_tx

subroutine sftx_sub(ckey0,pack_error)
  use sfox_tx, only: sftx_for_state
  use ft8_codec_context, only: get_ft8_codec_state
  use packjt77, only: pack77_state
  character*(*) ckey0
  character*40 cmsg
  integer pack_error
  type(pack77_state), pointer :: knowledge
  common/foxcom3/nslots,cmsg(5),itone(151)

  knowledge => get_ft8_codec_state()
  call sftx_for_state(knowledge,ckey0,nslots,cmsg,itone,pack_error)

end subroutine sftx_sub
