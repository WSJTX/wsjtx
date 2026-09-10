module map65_polarization_score_mod
  use iso_fortran_env, only: real64
  implicit none
  private

  real(real64), parameter :: pi=acos(-1.0_real64)
  real(real64), parameter :: correlation_margin=1.0e-6_real64

  public :: generalized_polarization_score
  public :: polarization_matrix

contains

  pure subroutine polarization_matrix(projections,g00,g11,g01)
    real(real64), intent(in) :: projections(4)
    real(real64), intent(out) :: g00,g11,g01

    g00=projections(1)
    g11=projections(3)
    g01=0.5_real64*(projections(2)-projections(4))
  end subroutine polarization_matrix

  pure subroutine generalized_polarization_score(g00,g11,g01,n00,n11,n01,score, &
       physical_angle_degrees,combine_angle_degrees,valid)
    real(real64), intent(in) :: g00,g11,g01,n00,n11,n01
    real(real64), intent(out) :: score,physical_angle_degrees,combine_angle_degrees
    logical, intent(out) :: valid
    real(real64) :: alpha,ell,h00,h01,h11,m00,m01,m11,rho,sx,sy
    real(real64) :: ux,uy,vx,vy,w0,w1

    score=0.0_real64
    physical_angle_degrees=0.0_real64
    combine_angle_degrees=0.0_real64
    valid=.false.
    if (n00 <= tiny(1.0_real64) .or. n11 <= tiny(1.0_real64)) return

    sx=sqrt(n00)
    sy=sqrt(n11)
    rho=n01/(sx*sy)
    if (.not.(abs(rho) < huge(1.0_real64))) return
    if (abs(rho) > 1.0_real64+correlation_margin) return
    if (abs(rho) >= 1.0_real64-correlation_margin) then
       rho=sign(1.0_real64-correlation_margin,rho)
    endif
    ell=sqrt(max(correlation_margin,1.0_real64-rho*rho))

    m00=g00/n00
    m01=g01/(sx*sy)
    m11=g11/n11
    h00=m00
    h01=(m01-rho*m00)/ell
    h11=(m11-2.0_real64*rho*m01+rho*rho*m00)/(ell*ell)
    score=max(0.0_real64,0.5_real64*(h00+h11)+ &
         hypot(0.5_real64*(h00-h11),h01))

    alpha=0.5_real64*atan2(2.0_real64*h01,h00-h11)
    w0=cos(alpha)
    w1=sin(alpha)
    vx=(w0-rho*w1/ell)/sx
    vy=(w1/ell)/sy
    ux=sx*w0
    uy=sy*(rho*w0+ell*w1)
    combine_angle_degrees=half_circle_angle(vx,vy)
    physical_angle_degrees=half_circle_angle(ux,uy)
    valid=.true.
  end subroutine generalized_polarization_score

  pure real(real64) function half_circle_angle(x,y)
    real(real64), intent(in) :: x,y

    half_circle_angle=modulo(atan2(y,x)*180.0_real64/pi,180.0_real64)
  end function half_circle_angle

end module map65_polarization_score_mod
