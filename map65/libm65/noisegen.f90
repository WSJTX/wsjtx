subroutine noisegen(d4,nmax)
  use iso_c_binding, only: c_ptr, c_loc, c_float
  use gran_interface
  implicit none

  real*4 d4(4,nmax)
  integer :: nmax, i

  do i=1,nmax
     d4(1,i)=gran()
     d4(2,i)=gran()
     d4(3,i)=gran()
     d4(4,i)=gran()
  enddo

  return
end subroutine noisegen
