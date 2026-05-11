module dpol_mod
  implicit none
contains

real(8) function dpol(mygrid, hisgrid) result(r)
  use moondop_mod
  use grid2deg_mod, only: grid2deg
  implicit none

  !--------------------------------------------------------------------
  ! Arguments
  !--------------------------------------------------------------------
  character(len=6), intent(in) :: mygrid, hisgrid

  !--------------------------------------------------------------------
  ! Locals
  !--------------------------------------------------------------------
  character(len=8)  :: cdate
  character(len=10) :: ctime2
  character(len=5)  :: czone
  integer           :: it(8)
  integer           :: nyear, month, nday
  integer           :: nh, nm, ns

  real(4) :: lat, lon, LST, HA
  real(4) :: RAMoon, DecMoon, AzMoon, ElMoon
  real(4) :: vr, dist
  real(4) :: xx, yy
  real(4) :: poloffset1, poloffset2
  real(8), parameter :: rad = 57.2957795d0
  real(4) :: uth

  !--------------------------------------------------------------------
  ! Get current date/time
  !--------------------------------------------------------------------
  call date_and_time(cdate, ctime2, czone, it)
  nyear = it(1)
  month = it(2)
  nday  = it(3)
  nh    = it(5) - it(4)/60
  nm    = it(6)
  ns    = it(7)
  uth   = nh + nm/60.0d0 + ns/3600.0d0

  !--------------------------------------------------------------------
  ! First grid
  !--------------------------------------------------------------------
  call grid2deg(mygrid, lon, lat)
  call MoonDop(nyear, month, nday, uth, -lon, lat, RAMoon, DecMoon, &
               LST, HA, AzMoon, ElMoon, vr, dist)

  xx = sin(lat/rad)*cos(ElMoon/rad) - cos(lat/rad)*cos(AzMoon/rad)*sin(ElMoon/rad)
  yy = cos(lat/rad)*sin(AzMoon/rad)
  poloffset1 = rad * atan2(yy, xx)

  !--------------------------------------------------------------------
  ! Second grid
  !--------------------------------------------------------------------
  call grid2deg(hisgrid, lon, lat)
  call MoonDop(nyear, month, nday, uth, -lon, lat, RAMoon, DecMoon, &
               LST, HA, AzMoon, ElMoon, vr, dist)

  xx = sin(lat/rad)*cos(ElMoon/rad) - cos(lat/rad)*cos(AzMoon/rad)*sin(ElMoon/rad)
  yy = cos(lat/rad)*sin(AzMoon/rad)
  poloffset2 = rad * atan2(yy, xx)

  !--------------------------------------------------------------------
  ! Final polarization offset
  !--------------------------------------------------------------------
  r = mod(poloffset2 - poloffset1 + 720.0d0, 180.0d0)
  if (r > 90.0d0) r = r - 180.0d0

end function dpol

end module dpol_mod

