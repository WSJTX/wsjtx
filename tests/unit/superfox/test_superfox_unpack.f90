program test_superfox_unpack

  implicit none

  integer*1 xin(0:49), xdec(0:49)
  logical*1 more_cqs, send_msg
  character*120 line
  character*26 free_text
  character*10 ckey
  character*13 foxcall
  integer notp, pack_error

  line='CQ K1JT FN20'
  free_text='2S/IZFUZBRXJF             '
  ckey='0000123456'
  more_cqs=.false.
  send_msg=.true.

  call sfox_pack(line,ckey,more_cqs,send_msg,free_text,xin,pack_error)
  if(pack_error.ne.0) error stop 'sfox_pack failed'

  xdec=xin(49:0:-1)

  notp=0
  foxcall=''
  call sfox_unpack(123456,xdec,0,1500.0,0.0,foxcall,notp)

end program test_superfox_unpack
