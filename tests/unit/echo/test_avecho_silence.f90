program test_avecho_silence

  use, intrinsic :: ieee_arithmetic
  implicit none

  integer, parameter :: ntx=6*4096
  integer*2 id2(ntx+4096)
  integer nauto,navg,ndop,nfrit,nqual
  integer nclearave,nsum
  real blue(4096),db_err,dfreq,f1,fspread_dx,fspread_self
  real red(4096),snrdb,width,xlevel
  logical*1 disk_data,echo_call
  character*6 rxcall,txcall
  common/echocom/nclearave,nsum,blue,red
  common/echocom2/fspread_self,fspread_dx

  id2=0
  ndop=0
  nfrit=0
  nauto=0
  navg=1
  nqual=-1
  nclearave=1
  nsum=0
  f1=1500.
  fspread_self=1.
  fspread_dx=1.
  width=1.
  disk_data=.false.
  echo_call=.false.
  txcall='      '
  rxcall='STALE!'
  blue=1.
  red=1.

  call avecho(id2,ndop,nfrit,nauto,navg,nqual,f1,xlevel,snrdb,db_err,dfreq, &
       width,disk_data,echo_call,txcall,rxcall)

  if(.not.ieee_is_finite(xlevel)) error stop 1
  if(.not.ieee_is_finite(snrdb)) error stop 1
  if(.not.ieee_is_finite(db_err)) error stop 1
  if(.not.ieee_is_finite(dfreq)) error stop 1
  if(abs(xlevel+99.).gt.0.01) error stop 1
  if(abs(snrdb+99.).gt.0.01) error stop 1
  if(abs(dfreq).gt.0.01) error stop 1
  if(nqual.ne.0) error stop 1
  if(any(.not.ieee_is_finite(blue))) error stop 1
  if(any(.not.ieee_is_finite(red))) error stop 1
  if(any(blue.ne.0.) .or. any(red.ne.0.)) error stop 1

  print*,'Silent Echo input test passed'

end program test_avecho_silence
