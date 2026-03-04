subroutine astrosub(nyear,month,nday,uth8,nfreq,mygrid,hisgrid,          &
     AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
     RAMoon8,DecMoon8,Dgrd8,poloffset8,xnr8)

  implicit real*8 (a-h,o-z)
  character*6 mygrid,hisgrid

  call astro0(nyear,month,nday,uth8,nfreq,mygrid,hisgrid,                &
     AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
     dbMoon8,RAMoon8,DecMoon8,HA8,Dgrd8,sd8,poloffset8,xnr8,dfdt,dfdt0,  &
     width1,width2,w501,w502,xlst8)

  return  
end subroutine astrosub

subroutine astrosub00(nyear,month,nday,uth8,nfreq,mygrid,ndop00)

  implicit real*8 (a-h,o-z)
  character*6 mygrid

  call astrosub(nyear,month,nday,uth8,nfreq,mygrid,mygrid,               &
     AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
     RAMoon8,DecMoon8,Dgrd8,poloffset8,xnr8)

  return
end subroutine astrosub00
