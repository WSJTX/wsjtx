subroutine grid2k(grid,k)

  character*6 grid

  call grid2deg(grid,xlong,xlat)
  nlong=nint(xlong)
  nlat=nint(xlat)
  k=0
  if(nlat.ge.85) k=5*(nlong+179)/2 + nlat-84

  return
end subroutine grid2k
