subroutine astro0(nyear,month,nday,uth8,freq8,mygrid,hisgrid,              &
     AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,    &
     dbMoon8,RAMoon8,DecMoon8,HA8,Dgrd8,sd8,poloffset8,xnr8,dfdt,dfdt0,    &
     width1,width2,xlst8,techo8)

  parameter (DEGS=57.2957795130823d0)
  character*6 mygrid,hisgrid
  real*8 AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8
  real*8 dbMoon8,RAMoon8,DecMoon8,HA8,Dgrd8,xnr8,dfdt,dfdt0,dt
  real*8 sd8,poloffset8,width1,width2,xlst8
  real*8 uth8,techo8,freq8
  real*8 xl,b
  common/librcom/xl(2),b(2)
  common/echocom2/fspread_self,fspread_dx
  data uth8z/0.d0/
  save

  uth=uth8
  call astro(nyear,month,nday,uth,freq8,hisgrid,2,1,                 &
       AzSun,ElSun,AzMoon,ElMoon,ntsky,doppler00,doppler,            &
       dbMoon,RAMoon,DecMoon,HA,Dgrd,sd,poloffset,xnr,               &
       day,xlon2,xlat2,xlst,techo)
  AzMoonB8=AzMoon
  ElMoonB8=ElMoon
  xl2=xl(1)
  xl2a=xl(2)
  b2=b(1)
  b2a=b(2)
  call astro(nyear,month,nday,uth,freq8,mygrid,1,1,                  &
       AzSun,ElSun,AzMoon,ElMoon,ntsky,doppler00,doppler,            &
       dbMoon,RAMoon,DecMoon,HA,Dgrd,sd,poloffset,xnr,               &
       day,xlon1,xlat1,xlst,techo)
  xl1=xl(1)
  xl1a=xl(2)
  b1=b(1)
  b1a=b(2)
  techo8=techo

  fghz=1.d-9*freq8
  dldt1=DEGS*(xl1a-xl1)
  dbdt1=DEGS*(b1a-b1)
  dldt2=DEGS*(xl2a-xl2)
  dbdt2=DEGS*(b2a-b2)
  rate1=2.0*sqrt(dldt1**2 + dbdt1**2)
  width1=0.5*6741*fghz*rate1
  rate2=sqrt((dldt1+dldt2)**2 + (dbdt1+dbdt2)**2)
  width2=0.5*6741*fghz*rate2
  if(hisgrid(1:4).eq.'    ') width2=width1       !No hisgrid, use self width
  fspread_self=width1                            !Save for avecho()
  fspread_dx=width2                              !Save for avecho()

  AzSun8=AzSun
  ElSun8=ElSun
  AzMoon8=AzMoon
  ElMoon8=ElMoon
  dbMoon8=dbMoon
  RAMoon8=RAMoon/15.0
  DecMoon8=DecMoon
  HA8=HA
  xlst8=xlst
  Dgrd8=Dgrd
  sd8=sd
  poloffset8=poloffset
  xnr8=xnr
  ndop=nint(doppler)
  ndop00=nint(doppler00)

  if(uth8z.eq.0.d0) then
     uth8z=uth8-1.d0/3600.d0
     dopplerz=doppler
     doppler00z=doppler00
  endif
     
  dt=60.0*(uth8-uth8z)
  if(dt.le.0) dt=1.d0/60.d0
  dfdt=(doppler-dopplerz)/dt
  dfdt0=(doppler00-doppler00z)/dt
  uth8z=uth8
  dopplerz=doppler
  doppler00z=doppler00

  return
end subroutine astro0
