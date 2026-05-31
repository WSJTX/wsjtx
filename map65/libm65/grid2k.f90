module grid2k_mod
  implicit none
contains
  subroutine grid2k(grid,k)
    use grid2deg_mod
    character(len=6), intent(in) :: grid
    integer, intent(out) :: k
    real :: xlong, xlat
    integer :: nlong, nlat

    call grid2deg(grid,xlong,xlat)
    nlong=nint(xlong)
    nlat=nint(xlat)
    k=0
    if(nlat.ge.85) k=5*(nlong+179)/2 + nlat-84

  end subroutine grid2k
end module grid2k_mod
