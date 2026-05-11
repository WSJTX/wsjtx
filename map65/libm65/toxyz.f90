module toxyz_mod
  use iso_fortran_env, only: real64
  implicit none
contains
  subroutine toxyz(alpha,delta,r,vec)
    implicit none
    real(real64), intent(in) :: alpha, delta, r
    real(real64), intent(out) :: vec(3)
  
    vec(1)=r*cos(delta)*cos(alpha)
    vec(2)=r*cos(delta)*sin(alpha)
    vec(3)=r*sin(delta)
  
  end subroutine toxyz

  subroutine fromxyz(vec,alpha,delta,r)
    implicit none
    real(real64), intent(in) :: vec(3)
    real(real64), intent(out) :: alpha, delta, r
    real(real64) :: twopi
    data twopi/6.283185307d0/
  
    r=sqrt(vec(1)**2 + vec(2)**2 + vec(3)**2)
    alpha=atan2(vec(2),vec(1))
    if(alpha.lt.0.d0) alpha=alpha+twopi
    delta=asin(vec(3)/r)
  
  end subroutine fromxyz
end module toxyz_mod
