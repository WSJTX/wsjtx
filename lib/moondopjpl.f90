subroutine MoonDopJPL(nyear,month,nday,uth4,lon4,lat4,RAMoon4,        &
     DecMoon4,LST4,HA4,AzMoon4,ElMoon4,vr4,dist4)

  implicit real*8 (a-h,o-z)
  real*4 uth4                    !UT in hours
  real*4 lon4                    !East longitude, degrees
  real*4 lat4                    !Latitude, degrees
  real*4 RAMoon4                 !Topocentric RA of moon, hours
  real*4 DecMoon4                !Topocentric Dec of Moon, degrees
  real*4 LST4                    !Locat sidereal time, hours
  real*4 HA4                     !Local Hour angle, degrees
  real*4 AzMoon4                 !Topocentric Azimuth of moon, degrees
  real*4 ElMoon4                 !Topocentric Elevation of moon, degrees
  real*4 vr4                     !Radial velocity of moon wrt obs, km/s
  real*4 dist4                   !Echo time, seconds

  twopi=8.d0*atan(1.d0)          !Define some constants
  rad=360.d0/twopi
  clight=2.99792458d5

  call sla_CLDJ(nyear,month,nday,djutc,j)
  djutc=djutc + uth4/24.d0
  dut=-0.460d0

  east_long=lon4/rad
  geodetic_lat=lat4/rad
  height=40.
  nspecial=0

  call ephem(djutc,dut,east_long,geodetic_lat,height,nspecial,    &
       RA,Dec,Az,El,techo,dop,fspread_1GHz,vr)

  RAMoon4=RA
  DecMoon4=Dec
  LST4=0.                 !These two variables not presently used
  HA4=0.
  AzMoon4=Az*rad
  ElMoon4=El*rad
  vr4=vr
  dist4=techo

  return
end subroutine MoonDopJPL
