subroutine k2grid(k,grid)
  character grid*6

  nlong=2*mod((k-1)/5,90)-179
  if(k.gt.450) nlong=nlong+180
  nlat=mod(k-1,5)+ 85
  dlat=nlat
  dlong=nlong
  call deg2grid(dlong,dlat,grid)

  return
end subroutine k2grid
