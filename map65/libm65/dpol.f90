real function dpol(mygrid,hisgrid)

! Compute spatial polartzation offset in degrees for the present 
! time, between two specified grid locators.

  character*6 MyGrid,HisGrid
  real lat,lon,LST
  character cdate*8,ctime2*10,czone*5,fnamedate*6
  integer  it(8)
  data rad/57.2957795/

  call date_and_time(cdate,ctime2,czone,it)
  nyear=it(1)
  month=it(2)
  nday=it(3)
  nh=it(5)-it(4)/60
  nm=it(6)
  ns=it(7)
  uth=nh + nm/60.0 + ns/3600.0

  call grid2deg(MyGrid,lon,lat)
  call MoonDop(nyear,month,nday,uth,-lon,lat,RAMoon,DecMoon,         &
       LST,HA,AzMoon,ElMoon,vr,dist)
  xx=sin(lat/rad)*cos(ElMoon/rad) - cos(lat/rad)*                    &
       cos(AzMoon/rad)*sin(ElMoon/rad)
  yy=cos(lat/rad)*sin(AzMoon/rad)
  poloffset1=rad*atan2(yy,xx)

  call grid2deg(hisGrid,lon,lat)
  call MoonDop(nyear,month,nday,uth,-lon,lat,RAMoon,DecMoon,         &
       LST,HA,AzMoon,ElMoon,vr,dist)
  xx=sin(lat/rad)*cos(ElMoon/rad) - cos(lat/rad)*                    &
       cos(AzMoon/rad)*sin(ElMoon/rad)
  yy=cos(lat/rad)*sin(AzMoon/rad)
  poloffset2=rad*atan2(yy,xx)

  dpol=mod(poloffset2-poloffset1+720.0,180.0)
  if(dpol.gt.90.0) dpol=dpol-180.0

  return
end function dpol
