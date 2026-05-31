module twkfreq_xy_mod
  implicit none
contains
  subroutine twkfreq_xy(c4aa,c4bb,n5,a)
    implicit none
    integer, intent(in) :: n5
    complex, intent(inout) :: c4aa(n5), c4bb(n5)
    real, intent(in) :: a(5)
    
    complex :: w,wstep
    real :: twopi
    data twopi/6.283185307/
    real :: x0, s, x, p2, dphi
    integer :: i
  
  ! Apply AFC corrections to the c4aa and c4bb data
    w=1.0
    wstep=1.0
    x0=0.5*(n5+1)
    s=2.0/n5
    do i=1,n5
       x=s*(i-x0)
       if(mod(i,1000).eq.1) then
          p2=1.5*x*x - 0.5
  !            p3=2.5*(x**3) - 1.5*x
  !            p4=4.375*(x**4) - 3.75*(x**2) + 0.375
          dphi=(a(1) + x*a(2) + p2*a(3)) * (twopi/1378.125)
          wstep=cmplx(cos(dphi),sin(dphi))
       endif
       w=w*wstep
       c4aa(i)=w*c4aa(i)
       c4bb(i)=w*c4bb(i)
    enddo
  
  end subroutine twkfreq_xy
end module twkfreq_xy_mod
