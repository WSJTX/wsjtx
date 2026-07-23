program test_superfox_tx_handoff

  implicit none

  integer, parameter :: NN=79
  integer, parameter :: NWAVE=(160+2)*134400*4
  real :: wave(NWAVE)
  integer :: nslots, nfreq, i3bit(5), itone2(NN), itone3(151)
  integer :: nslots2, nDataSlots
  integer :: pack_error, i3
  integer(kind=1) :: mycall(12), msgbits2(77), xin(0:49), raw(0:49)
  logical(kind=1) :: bSuperFox, bMoreCQs, bSendMsg
  character(len=40) :: cmsg(5), cmsg2(5)
  character(len=26) :: textMsg, freeTextMsg
  character(len=120) :: line
  character(len=11) :: foxcall
  character(len=10) :: ckey
  character(len=329) :: msgbits
  character(len=13) :: got
  integer :: i

  common/foxcom/wave,nslots,nfreq,i3bit,cmsg,mycall,textMsg,bMoreCQs,bSendMsg
  common/foxcom2/itone2,msgbits2
  common/foxcom3/nslots2,cmsg2,itone3

  wave=0.0
  nslots=4
  nfreq=750
  i3bit=0
  cmsg=' '
  mycall=0
  textMsg='HELLO SUPERFOX'
  bMoreCQs=.false.
  bSendMsg=.true.

  cmsg(1)='K1RAA K1JT -01'
  cmsg(2)='K1RAB K1JT -02'
  cmsg(3)='K1RAC K1JT -03'
  cmsg(4)='K1RAD K1JT -04'

  bSuperFox=.true.
  call foxgen(bSuperFox,' ')

  call assert_int('foxgen SuperFox row count',5,nslots2)
  call assert_text('foxgen fourth data row','K1RAD K1JT -04',cmsg2(4)(1:14))
  call assert_text('foxgen free text row','HELLO SUPERFOX',cmsg2(5)(1:14))
  if(cmsg2(5)(39:39).ne.'1') error stop 'foxgen did not flag free text row'

  freeTextMsg=' '
  bMoreCQs=cmsg2(1)(40:40).eq.'1'
  bSendMsg=cmsg2(nslots2)(39:39).eq.'1'
  nDataSlots=nslots2
  if(bSendMsg) then
     freeTextMsg=cmsg2(nslots2)(1:26)
     nDataSlots=nslots2-1
     if(nDataSlots.gt.4) nDataSlots=4
  endif

  call foxgen2(nDataSlots,cmsg2,line,foxcall)
  ckey='0000000000'
  call sfox_pack(line,ckey,bMoreCQs,bSendMsg,freeTextMsg,xin,pack_error)
  call assert_int('SuperFox handoff pack status',0,pack_error)

  do i=0,49
     raw(i)=xin(49-i)
  enddo
  write(msgbits,1000) raw(0:46)
1000 format(47b7.7)
  read(msgbits(327:329),'(b3)') i3
  call assert_int('SuperFox handoff message type',2,i3)

  call decode_pack28_field(msgbits,29,got)
  call assert_text('first SuperFox report hound','K1RAA',got)
  call decode_pack28_field(msgbits,57,got)
  call assert_text('second SuperFox report hound','K1RAB',got)
  call decode_pack28_field(msgbits,85,got)
  call assert_text('third SuperFox report hound','K1RAC',got)
  call decode_pack28_field(msgbits,113,got)
  call assert_text('fourth SuperFox report hound','K1RAD',got)
  call assert_text('SuperFox handoff free text','HELLO SUPERFOX',freeTextMsg(1:14))

  write(*,*) 'superfox tx handoff test passed'

contains

  subroutine decode_pack28_field(bits,first_bit,call_out)
    use packjt77, only: unpack28
    character(len=*), intent(in) :: bits
    integer, intent(in) :: first_bit
    character(len=13), intent(out) :: call_out
    logical :: success
    integer :: n28

    call_out=' '
    read(bits(first_bit:first_bit+27),'(b28)') n28
    call unpack28(n28,call_out,success)
    if(.not.success) error stop 'pack28 decode failed'
  end subroutine decode_pack28_field

  subroutine assert_int(label,expected,got)
    character(len=*), intent(in) :: label
    integer, intent(in) :: expected, got

    if(got.ne.expected) then
       write(*,1010) trim(label), expected, got
1010   format(a,' failure; expected ',i0,' got ',i0)
       error stop 1
    endif
  end subroutine assert_int

  subroutine assert_text(label,expected,got)
    character(len=*), intent(in) :: label, expected, got

    if(trim(got).ne.expected) then
       write(*,1020) trim(label), trim(expected), trim(got)
1020   format(a,' failure; expected "',a,'" got "',a,'"')
       error stop 1
    endif
  end subroutine assert_text

end program test_superfox_tx_handoff
