subroutine astro(nyear,month,nday,uth,freq8,Mygrid,                    &
          NStation,MoonDX,AzSun,ElSun,AzMoon0,ElMoon0,                 &
          ntsky,doppler00,doppler,dbMoon,RAMoon,DecMoon,HA,Dgrd,sd,    &
          poloffset,xnr,day,lon,lat,LST,techo)

! Computes astronomical quantities for display and tracking.
! NB: may want to smooth the Tsky map to 10 degrees or so.

  character*6 MyGrid,HisGrid
  real*8 freq8
  real LST
  real lat,lon
  integer*2 nt144(180)

!      common/echo/xdop(2),techo,AzMoon,ElMoon,mjd
  real xdop(2)

  data rad/57.2957795/
  data nt144/                                             &
       234, 246, 257, 267, 275, 280, 283, 286, 291, 298,  &
       305, 313, 322, 331, 341, 351, 361, 369, 376, 381,  &
       383, 382, 379, 374, 370, 366, 363, 361, 363, 368,  &
       376, 388, 401, 415, 428, 440, 453, 467, 487, 512,  &
       544, 579, 607, 618, 609, 588, 563, 539, 512, 482,  &
       450, 422, 398, 379, 363, 349, 334, 319, 302, 282,  &
       262, 242, 226, 213, 205, 200, 198, 197, 196, 197,  &
       200, 202, 204, 205, 204, 203, 202, 201, 203, 206,  &
       212, 218, 223, 227, 231, 236, 240, 243, 247, 257,  &
       276, 301, 324, 339, 346, 344, 339, 331, 323, 316,  &
       312, 310, 312, 317, 327, 341, 358, 375, 392, 407,  &
       422, 437, 451, 466, 480, 494, 511, 530, 552, 579,  &
       612, 653, 702, 768, 863,1008,1232,1557,1966,2385,  &
      2719,2924,3018,3038,2986,2836,2570,2213,1823,1461,  &
      1163, 939, 783, 677, 602, 543, 494, 452, 419, 392,  &
       373, 360, 353, 350, 350, 350, 350, 350, 350, 348,  &
       344, 337, 329, 319, 307, 295, 284, 276, 272, 272,  &
       273, 274, 274, 271, 266, 260, 252, 245, 238, 231/
  save

  call grid2deg(MyGrid,elon,lat)
  lon=-elon
  call sun(nyear,month,nday,uth,lon,lat,RASun,DecSun,LST,      &
       AzSun,ElSun,mjd,day)

  call MoonDopJPL(nyear,month,nday,uth,lon,lat,RAMoon,DecMoon,    &
       LST,HA,AzMoon,ElMoon,vr,techo)
  RAMoon=rad*RAMoon
  DecMoon=rad*DecMoon
  dist=2.99792458d5*techo/2.d0

! Compute spatial polarization offset
  xx=sin(lat/rad)*cos(ElMoon/rad) - cos(lat/rad)*              &
       cos(AzMoon/rad)*sin(ElMoon/rad)
  yy=cos(lat/rad)*sin(AzMoon/rad)
  if(NStation.eq.1) poloffset1=rad*atan2(yy,xx)
  if(NStation.eq.2) poloffset2=rad*atan2(yy,xx)

  doppler=-freq8*vr/2.99792458e5                 !One-way Doppler

  call coord(0.,0.,-1.570796,1.161639,RAMoon/rad,DecMoon/rad,el,eb)
  longecl_half=nint(rad*el/2.0)
  if(longecl_half.lt.1 .or. longecl_half.gt.180) longecl_half=180
  t144=nt144(longecl_half)
  tsky=(t144-2.7)*(144.0d6/freq8)**2.6 + 2.7      !Tsky for obs freq

  xdop(NStation)=doppler
  if(NStation.eq.2) then
     HisGrid=MyGrid
     go to 900
  endif

  doppler00=2.0*xdop(1)
  doppler=xdop(1)+xdop(2)
!      if(mode.eq.3) doppler=2.0*xdop(1)
  dBMoon=-40.0*log10(dist/356903.)
  sd=16.23*370152.0/dist

!      if(NStation.eq.1 .and. MoonDX.ne.0 .and. 
!     +    (mode.eq.2 .or. mode.eq.5)) then
  if(NStation.eq.1 .and. MoonDX.ne.0) then
     poloffset=mod(poloffset2-poloffset1+720.0,180.0)
     if(poloffset.gt.90.0) poloffset=poloffset-180.0
     x1=abs(cos(2*poloffset/rad))
     if(x1.lt.0.056234) x1=0.056234
     xnr=-20.0*log10(x1)
     if(HisGrid(1:1).lt.'A' .or. HisGrid(1:1).gt.'R') xnr=0
  endif

  tr=80.0                              !Good preamp
  tskymin=13.0*(408.0d6/freq8)**2.6      !Cold sky temperature
  tsysmin=tskymin+tr
  tsys=tsky+tr
  dgrd=-10.0*log10(tsys/tsysmin) + dbMoon
900 AzMoon0=Azmoon
  ElMoon0=Elmoon
  ntsky=nint(tsky)

!      auxHA = 15.0*(LST-auxra)                       !HA in degrees
!      pi=3.14159265
!      pio2=0.5*pi
!      call coord(pi,pio2-lat/rad,0.0,lat/rad,auxha*pi/180.0,
!     +  auxdec/rad,azaux,elaux)
!      AzAux=azaux*rad
!      ElAux=ElAux*rad

  return
end subroutine astro
