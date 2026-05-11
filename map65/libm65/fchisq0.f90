module fchisq0_mod
  implicit none
contains

real function fchisq0(y, npts, a) result(r)
  implicit none

  !--------------------------------------------------------------------
  ! Arguments
  !--------------------------------------------------------------------
  integer, intent(in) :: npts
  real,    intent(in) :: y(npts)
  real,    intent(in) :: a(4)

  !--------------------------------------------------------------------
  ! Locals
  !--------------------------------------------------------------------
  integer :: i
  real    :: x, z, yfit, d
  real    :: chisq

  !--------------------------------------------------------------------
  ! Compute chi-square
  !--------------------------------------------------------------------
  chisq = 0.0
  do i = 1, npts
     x = real(i)
     z = (x - a(3)) / (0.5 * a(4))
     yfit = a(1)

     if (abs(z) < 3.0) then
        d = 1.0 + z*z
        yfit = a(1) + a(2) * (1.0/d - 0.1)
     end if

     chisq = chisq + (y(i) - yfit)**2
  end do

  r = chisq

end function fchisq0

end module fchisq0_mod

