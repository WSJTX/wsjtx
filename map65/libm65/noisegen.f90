subroutine noisegen(d4,nmax)

  real*4 d4(4,nmax)

  do i=1,nmax
     d4(1,i)=gran()
     d4(2,i)=gran()
     d4(3,i)=gran()
     d4(4,i)=gran()
  enddo

  return
end subroutine noisegen
