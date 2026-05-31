module tm2_mod
  implicit none

  interface
     subroutine tmoonsub(day, glat, glong, el, rv, xl, b, pax)
       real(8), intent(in)  :: day, glat, glong
       real(8), intent(out) :: el, rv, xl, b, pax
     end subroutine tmoonsub
  end interface

contains

subroutine tm2(day, xlat4, xlon4, xl4, b4)
  implicit none

  ! Arguments
  real(8), intent(in)  :: day
  real(4), intent(in)  :: xlat4, xlon4
  real(4), intent(out) :: xl4, b4

  ! Locals
  real(8), parameter :: RADS = 0.0174532925199433d0
  real(8) :: glat, glong
  real(8) :: el, rv, xl, b, pax

  ! Convert to radians
  glat  = xlat4 * RADS
  glong = xlon4 * RADS

  ! Call the external routine
  call tmoonsub(day, glat, glong, el, rv, xl, b, pax)

  ! Convert back to real*4
  xl4 = real(xl, 4)
  b4  = real(b, 4)

end subroutine tm2

end module tm2_mod
