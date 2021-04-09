subroutine moon2(y,m,Day,UT,lon,lat,RA,Dec,topRA,topDec,LST,HA,Az,El,dist)

  implicit none

  integer y                           !Year
  integer m                           !Month
  integer Day                         !Day
  real*8 UT                           !UTC in hours
  real*8 RA,Dec                       !RA and Dec of moon

! NB: Double caps are single caps in the writeup.

  real*8 NN                           !Longitude of ascending node
  real*8 i                            !Inclination to the ecliptic
  real*8 w                            !Argument of perigee
  real*8 a                            !Semi-major axis
  real*8 e                            !Eccentricity
  real*8 MM                           !Mean anomaly

  real*8 v                            !True anomaly
  real*8 EE                           !Eccentric anomaly
  real*8 ecl                          !Obliquity of the ecliptic

  real*8 d                            !Ephemeris time argument in days
  real*8 r                            !Distance to sun, AU
  real*8 xv,yv                        !x and y coords in ecliptic
  real*8 lonecl,latecl                !Ecliptic long and lat of moon
  real*8 xg,yg,zg                     !Ecliptic rectangular coords
  real*8 Ms                           !Mean anomaly of sun
  real*8 ws                           !Argument of perihelion of sun
  real*8 Ls                           !Mean longitude of sun (Ns=0)
  real*8 Lm                           !Mean longitude of moon
  real*8 DD                           !Mean elongation of moon
  real*8 FF                           !Argument of latitude for moon
  real*8 xe,ye,ze                     !Equatorial geocentric coords of moon
  real*8 mpar                         !Parallax of moon (r_E / d)
  real*8 lat,lon                      !Station coordinates on earth
  real*8 gclat                        !Geocentric latitude
  real*8 rho                          !Earth radius factor
  real*8 GMST0,LST,HA
  real*8 g
  real*8 topRA,topDec                 !Topocentric coordinates of Moon
  real*8 Az,El
  real*8 dist

  real*8 rad,twopi,pi,pio2
  data rad/57.2957795131d0/,twopi/6.283185307d0/

  d=367*y - 7*(y+(m+9)/12)/4 + 275*m/9 + Day - 730530 + UT/24.d0
  ecl = 23.4393d0 - 3.563d-7 * d

! Orbital elements for Moon:  
  NN = 125.1228d0 - 0.0529538083d0 * d
  i = 5.1454d0
  w = mod(318.0634d0 + 0.1643573223d0 * d + 360000.d0,360.d0)
  a = 60.2666d0
  e = 0.054900d0
  MM = mod(115.3654d0 + 13.0649929509d0 * d + 360000.d0,360.d0)

  EE = MM + e*rad*sin(MM/rad) * (1.d0 + e*cos(MM/rad))
  EE = EE - (EE - e*rad*sin(EE/rad)-MM) / (1.d0 - e*cos(EE/rad))
  EE = EE - (EE - e*rad*sin(EE/rad)-MM) / (1.d0 - e*cos(EE/rad))

  xv = a * (cos(EE/rad) - e)
  yv = a * (sqrt(1.d0-e*e) * sin(EE/rad))

  v = mod(rad*atan2(yv,xv)+720.d0,360.d0)
  r = sqrt(xv*xv + yv*yv)

! Get geocentric position in ecliptic rectangular coordinates:

  xg = r * (cos(NN/rad)*cos((v+w)/rad)-sin(NN/rad)*sin((v+w)/rad)*cos(i/rad))
  yg = r * (sin(NN/rad)*cos((v+w)/rad)+cos(NN/rad)*sin((v+w)/rad)*cos(i/rad))
  zg = r * (sin((v+w)/rad)*sin(i/rad))

! Ecliptic longitude and latitude of moon:
  lonecl = mod(rad*atan2(yg/rad,xg/rad)+720.d0,360.d0)
  latecl = rad*atan2(zg/rad,sqrt(xg*xg + yg*yg)/rad)

! Now include orbital perturbations:
  Ms = mod(356.0470d0 + 0.9856002585d0 * d + 3600000.d0,360.d0)
  ws = 282.9404d0 + 4.70935d-5*d
  Ls = mod(Ms + ws + 720.d0,360.d0)
  Lm = mod(MM + w + NN+720.d0,360.d0)
  DD = mod(Lm - Ls + 360.d0,360.d0)
  FF = mod(Lm - NN + 360.d0,360.d0)

  lonecl = lonecl                                &
       - 1.274d0 * sin((MM-2.d0*DD)/rad)         &
       + 0.658d0 * sin(2.d0*DD/rad)              &
       - 0.186d0 * sin(Ms/rad)                   &
       - 0.059d0 * sin((2.d0*MM-2.d0*DD)/rad)    &
       - 0.057d0 * sin((MM-2.d0*DD+Ms)/rad)      &
       + 0.053d0 * sin((MM+2.d0*DD)/rad)         &
       + 0.046d0 * sin((2.d0*DD-Ms)/rad)         &
       + 0.041d0 * sin((MM-Ms)/rad)              &
       - 0.035d0 * sin(DD/rad)                   &
       - 0.031d0 * sin((MM+Ms)/rad)              &
       - 0.015d0 * sin((2.d0*FF-2.d0*DD)/rad)    &
       + 0.011d0 * sin((MM-4.d0*DD)/rad)

  latecl = latecl                                &
       - 0.173d0 * sin((FF-2.d0*DD)/rad)         &
       - 0.055d0 * sin((MM-FF-2.d0*DD)/rad)      &
       - 0.046d0 * sin((MM+FF-2.d0*DD)/rad)      &
       + 0.033d0 * sin((FF+2.d0*DD)/rad)         &
       + 0.017d0 * sin((2.d0*MM+FF)/rad)

  r = 60.36298d0                                 &
       - 3.27746d0*cos(MM/rad)                   &
       - 0.57994d0*cos((MM-2.d0*DD)/rad)         &
       - 0.46357d0*cos(2.d0*DD/rad)              &
       - 0.08904d0*cos(2.d0*MM/rad)              &
       + 0.03865d0*cos((2.d0*MM-2.d0*DD)/rad)    &
       - 0.03237d0*cos((2.d0*DD-Ms)/rad)         &
       - 0.02688d0*cos((MM+2.d0*DD)/rad)         &
       - 0.02358d0*cos((MM-2.d0*DD+Ms)/rad)      &
       - 0.02030d0*cos((MM-Ms)/rad)              &
       + 0.01719d0*cos(DD/rad)                   &
       + 0.01671d0*cos((MM+Ms)/rad)
  
  dist=r*6378.140d0

! Geocentric coordinates:
! Rectangular ecliptic coordinates of the moon:

  xg = r * cos(lonecl/rad)*cos(latecl/rad)
  yg = r * sin(lonecl/rad)*cos(latecl/rad)
  zg = r *                 sin(latecl/rad)

! Rectangular equatorial coordinates of the moon:
  xe = xg
  ye = yg*cos(ecl/rad) - zg*sin(ecl/rad)
  ze = yg*sin(ecl/rad) + zg*cos(ecl/rad)
   
! Right Ascension, Declination:
  RA = mod(rad*atan2(ye,xe)+360.d0,360.d0)
  Dec = rad*atan2(ze,sqrt(xe*xe + ye*ye))

! Now convert to topocentric system:
  mpar=rad*asin(1.d0/r)
!     alt_topoc = alt_geoc - mpar*cos(alt_geoc)
  gclat = lat - 0.1924d0*sin(2.d0*lat/rad)
  rho = 0.99883d0 + 0.00167d0*cos(2.d0*lat/rad)
  GMST0 = (Ls + 180.d0)/15.d0
  LST = mod(GMST0+UT+lon/15.d0+48.d0,24.d0)    !LST in hours
  HA = 15.d0*LST - RA                          !HA in degrees
  g = rad*atan(tan(gclat/rad)/cos(HA/rad))
  topRA = RA - mpar*rho*cos(gclat/rad)*sin(HA/rad)/cos(Dec/rad)
  topDec = Dec - mpar*rho*sin(gclat/rad)*sin((g-Dec)/rad)/sin(g/rad)

  HA = 15.d0*LST - topRA                       !HA in degrees
  if(HA.gt.180.d0) HA=HA-360.d0
  if(HA.lt.-180.d0) HA=HA+360.d0

  pi=0.5d0*twopi
  pio2=0.5d0*pi
  call dcoord(pi,pio2-lat/rad,0.d0,lat/rad,ha*twopi/360,topDec/rad,az,el)
  Az=az*rad
  El=El*rad

  return
end subroutine moon2
