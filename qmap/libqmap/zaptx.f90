subroutine zaptx(dd,k0,k)

  parameter(NMAX=60*96000)
  real dd(2,NMAX)

  dd(1:2,k0+1:k)=0.

  return
end subroutine zaptx
