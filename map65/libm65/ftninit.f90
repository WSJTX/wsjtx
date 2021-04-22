! Fortran logical units used in WSJT6
!
!   10  binary input data, *.tf2 files
!   11  prefixes.txt
!   12  timer.out
!   13  map65.log
!   14  
!   15  
!   16
!   17  saved *.tf2 files
!   18  test file to be transmitted (wsjtgen.f90)
!   19  livecq.txt
!   20  
!   21  map65_rx.log
!   22  
!   23  CALL3.TXT
!   24  
!   25  
!   26  tmp26.txt
!   27  
!   28  fftw_wisdom.dat
!------------------------------------------------ ftn_init
subroutine ftninit(appd)

  character*(*) appd
  character firstline*30
  character addpfx*8
  common/pfxcom/addpfx

  addpfx='    '
  call pfxdump(appd//'/prefixes.txt')
  open(12,file=appd//'/timer_map65.out',status='unknown',err=920)
  open(13,file=appd//'/map65.log',status='unknown')
  open(19,file=appd//'/livecq.txt',status='unknown')
  open(21,file=appd//'/map65_rx.log',status='unknown',access='append',err=950)
  open(26,file=appd//'/tmp26.txt',status='unknown')

! Import FFTW wisdom, if available:
  open(28,file=appd//'/fftwf_wisdom.dat',status='old',err=30)
  read(28,1000,err=30,end=30) firstline
1000 format(a30)
  rewind 28
  call import_wisdom_from_file(isuccess,28)
  close(28)
  if(isuccess.ne.0) write(13,1010) firstline
1010 format('Imported FFTW wisdom: ',a30)

30 flush(13)
  return

920 write(0,*) '!Error opening timer.out'
  stop
950 write(0,*) '!Error opening ALL65.TXT'
  stop

end subroutine ftninit
