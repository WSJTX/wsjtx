subroutine ftninit(appd)

  use timer_module, only: timer
  use, intrinsic :: iso_c_binding, only: C_NULL_CHAR
  use FFTW3
  character*(*) appd
  character addpfx*8
  character wisfile*256
  common/pfxcom/addpfx

  addpfx='    '
  call pfxdump(appd//'/prefixes.txt')
  open(13,file=appd//'/map65.log',status='unknown')
  open(19,file=appd//'/livecq.txt',status='unknown')
  open(21,file=appd//'/map65_rx.log',status='unknown',access='append',err=950)
  open(26,file=appd//'/tmp26.txt',status='unknown')

! Import FFTW wisdom, if available:
  iret=fftwf_init_threads()            !Initialize FFTW threading 
! Default to 1 thread, but use nthreads for the big ones
  call fftwf_plan_with_nthreads(1)
! Import FFTW wisdom, if available
  wisfile=trim(appd)//'/m65_wisdom.dat'// C_NULL_CHAR
  iret=fftwf_import_wisdom_from_filename(wisfile)
  return

950 write(0,*) '!Error opening ALL65.TXT'
  stop

end subroutine ftninit
