module q65_tx
  use packjt77, only: pack77_state,pack77_legacy_truncating_fallback,       &
       pack77_legacy_truncating_fallback_for_state,unpack77,unpack77_for_state
  private
  public :: genq65_for_state

contains

! genq65, packing with knowledge's hash tables, or without it packjt77's own.
subroutine genq65_for_state(msg0,ichk,msgsent,itone,i3,n3,iflag,knowledge)

! Encodes a Q65 message to yield itone(1:85)

  use q65_codec, only: q65_enc
  type(pack77_state), pointer, intent(in) :: knowledge
  character*37 msg0               !Message to be generated
  character*37 msgsent            !Message as it will be received
  character*77 c77
  logical unpk77_success
  integer itone(85)               !QRA64 uses only 84
  integer dgen(13)
  integer sent(63)
  integer isync(22)
  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/
  save

  if(msg0(1:1).eq.'@') then
     read(msg0(2:5),*,end=1,err=1) nfreq
     go to 2
1    nfreq=1000
2    itone(1)=nfreq
     write(msgsent,1000) nfreq
1000 format(i5,' Hz')
     goto 999
  endif
  i3=-1
  n3=-1
  c77=' '
  unpk77_success=.false.
  if(associated(knowledge)) then
     call pack77_legacy_truncating_fallback_for_state(knowledge,msg0,i3,n3,c77)
  else
     call pack77_legacy_truncating_fallback(msg0,i3,n3,c77)
  endif
  if(i3.lt.0 .or. n3.lt.0) go to 998
  read(c77(60:74),'(b15)',err=998) ng15
  if(ng15.eq.32373) c77(60:74)='111111010010011'    !Message is RR73
  if(associated(knowledge)) then
     call unpack77_for_state(knowledge,c77,0,msgsent,unpk77_success)
  else
     call unpack77(c77,0,msgsent,unpk77_success)
  endif    !Unpack to get msgsent
  read(c77,1001,err=998) dgen
1001 format(12b6.6,b5.5)
  dgen(13)=2*dgen(13)+iflag     !Convert 77-bit to 78-bit payload; iflag is the spare 78th bit
  if(ichk.eq.1) go to 999       !Return if checking only
  call q65_enc(dgen,sent)       !Encode message, dgen(1:13) ==> sent(1:63)

  j=1
  k=0
  do i=1,85
     if(i.eq.isync(j)) then
        j=j+1                   !Index for next sync symbol
        itone(i)=0              !Insert sync symbol at tone 0
     else
        k=k+1
        itone(i)=sent(k) + 1    !Q65 symbol=0 is transmitted at tone 1, etc.
     endif
  enddo

  go to 999
998 dgen=0
  sent=0
  itone=0
  i3=-1
  n3=-1
  msgsent='*** bad message ***                  '
999 return
end subroutine genq65_for_state

end module q65_tx

! With the selected decoder's knowledge, if any (lib/qra/q65/q65.f90 q65_work).
subroutine genq65(msg0,ichk,msgsent,itone,i3,n3,iflag)
  use packjt77, only: pack77_state
  use q65, only: q65_work
  use q65_tx, only: genq65_for_state
  character*37 msg0,msgsent
  integer itone(85)
  type(pack77_state), pointer :: knowledge

  knowledge=>null()
  if(associated(q65_work)) knowledge=>q65_work%knowledge
  call genq65_for_state(msg0,ichk,msgsent,itone,i3,n3,iflag,knowledge)
end subroutine genq65
