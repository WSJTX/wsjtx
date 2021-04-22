subroutine MoonDop(nyear,month,nday,uth4,lon4,lat4,RAMoon4,DecMoon4,   &
     LST4,HA4,AzMoon4,ElMoon4,vr4,dist4)

  implicit real*8 (a-h,o-z)
  real*4 uth4                    !UT in hours
  real*4 lon4                    !West longitude, degrees
  real*4 lat4                    !Latitude, degrees
  real*4 RAMoon4                 !Topocentric RA of moon, hours
  real*4 DecMoon4                !Topocentric Dec of Moon, degrees
  real*4 LST4                    !Locat sidereal time, hours
  real*4 HA4                     !Local Hour angle, degrees
  real*4 AzMoon4                 !Topocentric Azimuth of moon, degrees
  real*4 ElMoon4                 !Topocentric Elevation of moon, degrees
  real*4 vr4                     !Radial velocity of moon wrt obs, km/s
  real*4 dist4                   !Echo time, seconds

  real*8 LST
  real*8 RME(6)                  !Vector from Earth center to Moon
  real*8 RAE(6)                  !Vector from Earth center to Obs
  real*8 RMA(6)                  !Vector from Obs to Moon
  real*8 rme0(6)
  logical km

  data rad/57.2957795130823d0/,twopi/6.28310530717959d0/

  km=.true.
  dlat=lat4/rad
  dlong1=lon4/rad
  elev1=200.d0
  call geocentric(dlat,elev1,dlat1,erad1)

  dt=100.d0                       !For numerical derivative, in seconds
  UT=uth4

! NB: geodetic latitude used here, but geocentric latitude used when 
! determining Earth-rotation contribution to Doppler.

  call moon2(nyear,month,nDay,UT-dt/3600.d0,dlong1*rad,dlat*rad,     &
       RA,Dec,topRA,topDec,LST,HA,Az0,El0,dist)
  call toxyz(RA/rad,Dec/rad,dist,rme0)      !Convert to rectangular coords

  call moon2(nyear,month,nDay,UT,dlong1*rad,dlat*rad,                &
       RA,Dec,topRA,topDec,LST,HA,Az,El,dist)
  call toxyz(RA/rad,Dec/rad,dist,rme)       !Convert to rectangular coords

  phi=LST*twopi/24.d0
  call toxyz(phi,dlat1,erad1,rae)           !Gencentric numbers used here!
  radps=twopi/(86400.d0/1.002737909d0)
  rae(4)=-rae(2)*radps                      !Vel of Obs wrt Earth center
  rae(5)=rae(1)*radps
  rae(6)=0.d0

  do i=1,3
     rme(i+3)=(rme(i)-rme0(i))/dt
     rma(i)=rme(i)-rae(i)
     rma(i+3)=rme(i+3)-rae(i+3)
  enddo

  call fromxyz(rma,alpha1,delta1,dtopo0)     !Get topocentric coords
  vr=dot(rma(4),rma)/dtopo0

  RAMoon4=topRA
  DecMoon4=topDec
  LST4=LST
  HA4=HA
  AzMoon4=Az
  ElMoon4=El
  vr4=vr
  dist4=dist

  return
end subroutine MoonDop
