module smo_mod
  implicit none
contains
  subroutine smo(x,npts,y,nadd)
    implicit none
    integer, intent(in) :: npts, nadd
    real, intent(inout) :: x(npts)
    real, intent(out) :: y(npts)
    integer :: nh, i, j
    real :: sum
  
    nh=nadd/2
    do i=1+nh,npts-nh
       sum=0.
       do j=-nh,nh
          sum=sum + x(i+j)
       enddo
       y(i)=sum
    enddo
    x=y
    x(:nh)=0.
    x(npts-nh+1:)=0.
  
  end subroutine smo
end module smo_mod
