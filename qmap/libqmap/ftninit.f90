subroutine ftninit

  use timer_impl, only: init_timer !,fini_timer, limtrace
  use, intrinsic :: iso_c_binding, only: C_NULL_CHAR
  use FFTW3
  character*1 appd
  character addpfx*8
  common/pfxcom/addpfx

  lu=8
  call init_timer('./timer.out')
  
  appd='.'
  addpfx='    '
  open(12,file=appd//'/all_qmap.txt',status='unknown',position='append')
  open(17,file=appd//'/red.dat',status='unknown')
  open(19,file=appd//'/livecq.txt',status='unknown')

  return
end subroutine ftninit
