subroutine geocentric(alat,elev,hlt,erad)

  implicit real*8 (a-h,o-z)

! IAU 1976 flattening f, equatorial radius a
  f = 1.d0/298.257d0
  a = 6378140.d0
  c = 1.d0/sqrt(1.d0 + (-2.d0 + f)*f*sin(alat)*sin(alat))
  arcf = (a*c + elev)*cos(alat)
  arsf = (a*(1.d0 - f)*(1.d0 - f)*c + elev)*sin(alat)
  hlt = datan2(arsf,arcf)
  erad = sqrt(arcf*arcf + arsf*arsf)
  erad = 0.001d0*erad

  return
end subroutine geocentric

