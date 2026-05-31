module deg2grid_mod
  implicit none
contains

subroutine deg2grid(dlong0, dlat, grid)
  implicit none

  !--------------------------------------------------------------------
  ! Arguments
  !--------------------------------------------------------------------
  real,    intent(in)    :: dlong0      ! West longitude (deg)
  real,    intent(in)    :: dlat        ! Latitude (deg)
  character(len=6), intent(out) :: grid ! Maidenhead grid

  !--------------------------------------------------------------------
  ! Locals
  !--------------------------------------------------------------------
  real :: dlong
  integer :: nlong, nlat
  integer :: n1, n2, n3

  !--------------------------------------------------------------------
  ! Normalize longitude to [-180, 180]
  !--------------------------------------------------------------------
  dlong = dlong0
  if (dlong < -180.0) dlong = dlong + 360.0
  if (dlong >  180.0) dlong = dlong - 360.0

  !--------------------------------------------------------------------
  ! Longitude → grid characters 1,3,5
  !--------------------------------------------------------------------
  nlong = 60.0 * (180.0 - dlong) / 5.0
  n1 = nlong / 240                     ! 20-degree field
  n2 = (nlong - 240*n1) / 24           ! 2-degree square
  n3 =  nlong - 240*n1 - 24*n2         ! 5-minute subsquare

  grid(1:1) = char(ichar('A') + n1)
  grid(3:3) = char(ichar('0') + n2)
  grid(5:5) = char(ichar('a') + n3)

  !--------------------------------------------------------------------
  ! Latitude → grid characters 2,4,6
  !--------------------------------------------------------------------
  nlat = 60.0 * (dlat + 90.0) / 2.5
  n1 = nlat / 240                      ! 10-degree field
  n2 = (nlat - 240*n1) / 24            ! 1-degree square
  n3 =  nlat - 240*n1 - 24*n2          ! 2.5-minute subsquare

  grid(2:2) = char(ichar('A') + n1)
  grid(4:4) = char(ichar('0') + n2)
  grid(6:6) = char(ichar('a') + n3)

end subroutine deg2grid

end module deg2grid_mod
