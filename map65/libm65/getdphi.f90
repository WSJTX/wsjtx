module getdphi_mod
  implicit none
contains

subroutine getdphi(qphi)
  implicit none

  ! Arguments
  real, intent(in) :: qphi(12)

  ! Locals
  real :: c, dphi, s, th
  integer :: i

  s = 0.0
  c = 0.0

  do i = 1, 12
     th = i * 30.0 / 57.2957795   ! convert degrees to radians
     s  = s + qphi(i) * sin(th)
     c  = c + qphi(i) * cos(th)
  end do

  dphi = 57.2957795 * atan2(s, c)
  write(*,1010) nint(dphi)
1010 format('!Best-fit Dphi =', i4, ' deg')

end subroutine getdphi

end module getdphi_mod
