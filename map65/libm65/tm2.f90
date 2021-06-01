subroutine tm2(day,xlat4,xlon4,xl4,b4)

  implicit real*8 (a-h,o-z)
  parameter (RADS=0.0174532925199433d0)

  real*4 xlat4,xlon4,xl4,b4

  glat=xlat4*RADS
  glong=xlon4*RADS
  call tmoonsub(day,glat,glong,el,rv,xl,b,pax)
  xl4=xl
  b4=b

end subroutine tm2
