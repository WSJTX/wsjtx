module set_mod
  implicit none
contains
  
  subroutine move(x,y,n)
    implicit none
    integer, intent(in) :: n
    integer, intent(in) :: x(n)
    integer, intent(out) :: y(n)
    integer :: i
    do i=1,n
       y(i)=x(i)
    enddo
  end subroutine move
  
end module set_mod
