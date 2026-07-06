program test_superfox_unpack_unsupported

  implicit none

  integer*1 xin(0:49), xdec(0:49)
  logical*1 more_cqs, send_msg
  character*120 line
  character*26 free_text
  character*10 ckey
  character*13 foxcall
  character*329 msgbits
  integer notp, pack_error

  line='K1JT W1AW'
  free_text=' '
  ckey='0000123456'
  more_cqs=.false.
  send_msg=.false.

  call sfox_pack(line,ckey,more_cqs,send_msg,free_text,xin,pack_error)
  if(pack_error.ne.0) error stop 'sfox_pack failed'

  xdec=xin(49:0:-1)
  write(msgbits,1000) xdec(0:46)
1000 format(47b7.7)
  msgbits(327:329)='001'
  read(msgbits,1001) xdec(0:46)
1001 format(47b7)

  notp=0
  foxcall=''
  call sfox_unpack(123456,xdec,0,1500.0,0.0,foxcall,notp)
  write(*,*) 'unsupported i3=1 ignored'

end program test_superfox_unpack_unsupported
