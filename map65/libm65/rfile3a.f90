subroutine rfile3a(infile,ibuf,n,fcenter,ierr)

  character*(*) infile
  integer*8 ibuf(n)
  real*8 fcenter

  open(10,file=infile,access='stream',status='old',err=998)
  read(10,end=998) (ibuf(i),i=1,n/8),fcenter
  ierr=0
  go to 999
998 ierr=1002
999 close(10)
  return
end subroutine rfile3a
