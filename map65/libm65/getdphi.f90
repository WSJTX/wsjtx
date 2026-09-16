module getdphi_mod
  implicit none
contains

subroutine getdphi(qphi)
  use stdout_channel_mod, only: write_stdout
  implicit none

  ! Arguments
  real, intent(in) :: qphi(12)

  ! Locals
  real :: c, dphi, s, th
  integer :: i
  character(len=32) :: line

  s = 0.0
  c = 0.0

  do i = 1, 12
     th = i * 30.0 / 57.2957795   ! convert degrees to radians
     s  = s + qphi(i) * sin(th)
     c  = c + qphi(i) * cos(th)
  end do

  dphi = 57.2957795 * atan2(s, c)
  write(line,1010) nint(dphi)
1010 format('!Best-fit Dphi =', i4, ' deg')
  ! Plain write(*,...) went to real process stdout, which nothing reads any
  ! more -- the GUI now gets decoder output through the write_stdout shared-
  ! memory channel instead, so this line never reached the Messages window.
  call write_stdout(trim(line)//new_line('a'))

end subroutine getdphi

end module getdphi_mod
