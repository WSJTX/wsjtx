program tastro

  implicit real*8 (a-h,o-z)

  character grid*6
  character*9 cauxra,cauxdec

  character*12 clock(3)
  integer nt(8)
  equivalence (nt(1),nyear)

  grid='FN20qi'
  nfreq=144
  cauxra='00:00:00'
  
10 call date_and_time(clock(1),clock(2),clock(3),nt)
  ih=ihour-ntz/60
  if(ih.le.0) then
     ih=ih+24
     nday=nday+1
  endif
  uth8=ih + imin/60.d0 + isec/3600.d0 + ims/3600000.d0 
  call astro0(nyear,month,nday,uth8,nfreq,grid,cauxra,cauxdec,       &
     AzSun8,ElSun8,AzMoon8,ElMoon8,AzMoonB8,ElMoonB8,ntsky,ndop,ndop00,  &
     dbMoon8,RAMoon8,DecMoon8,HA8,Dgrd8,sd8,poloffset8,xnr8,dfdt,dfdt0,  &
     RaAux8,DecAux8,AzAux8,ElAux8,width1,width2,w501,w502,xlst8)
  
  write(*,1010) nyear,month,nday,ih,imin,isec,AzMoon8,ElMoon8,          &
       AzSun8,ElSun8,ndop,dgrd8,ntsky
1010 format(i4,i3,i3,i4.2,':',i2.2,':',i2.2,4f8.1,i6,f6.1,i6)

  call system('sleep 1')
  go to 10

end program tastro
