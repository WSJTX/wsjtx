module k2grid_mod
  implicit none
contains
  subroutine k2grid(k,grid)
    use deg2grid_mod, only: deg2grid
    character(len=6), intent(out) :: grid
    integer, intent(in) :: k
    
    integer :: nlong, nlat
    real :: dlong, dlat
  
    nlong=2*mod((k-1)/5,90)-179
    if(k.gt.450) nlong=nlong+180
    nlat=mod(k-1,5)+ 85
    dlat=nlat
    dlong=nlong
    call deg2grid(dlong,dlat,grid)
  
  end subroutine k2grid
end module k2grid_mod
