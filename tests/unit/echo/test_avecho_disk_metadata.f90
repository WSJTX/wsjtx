program test_avecho_disk_metadata

  implicit none

  integer, parameter :: ntx=6*4096
  integer*2 id2(ntx+4096)
  integer*4 itone(6)
  integer idir,nauto,navg,ndf,ndop,ndop_total,nfrit,nqual
  integer nclearave,nsum
  real db_err,dfreq,f1,fspread_self,fspread_dx,snrdb,width,xlevel
  real blue(4096),red(4096)
  logical*1 disk_data,echo_call
  character*6 rxcall,txcall
  common/echocom/nclearave,nsum,blue,red
  common/echocom2/fspread_self,fspread_dx

  id2=0
  id2(16:ntx)=1
  itone=0
  ndop_total=0
  ndop=0
  nfrit=10000
  f1=1500.
  width=1.
  ndf=-999
  idir=1
  call save_echo_params(ndop_total,ndop,nfrit,f1,width,ndf,itone,id2,idir)

  ndf=0
  idir=-1
  call save_echo_params(ndop_total,ndop,nfrit,f1,width,ndf,itone,id2,idir)
  if(ndf.ne.-999) error stop 1

  nauto=0
  navg=1
  nqual=0
  nclearave=1
  nsum=0
  fspread_self=1.
  fspread_dx=1.
  disk_data=.true.
  echo_call=.false.
  txcall='      '
  rxcall='BADBAD'

  call avecho(id2,ndop,nfrit,nauto,navg,nqual,f1,xlevel,snrdb,db_err,dfreq, &
       width,disk_data,echo_call,txcall,rxcall)
  if(rxcall.ne.'      ') error stop 2

  print*,'Echo disk metadata test passed'

end program test_avecho_disk_metadata
