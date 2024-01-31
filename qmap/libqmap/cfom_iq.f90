program cfom_iq

  parameter(NMAX=60*96000)
  integer*2 id2(2,NMAX)
  complex*16 c,w,wstep
  real*8 fcenter,uth8,twopi,dphi
  character*6 mygrid

  twopi=8.d0*atan(1.d0)
  open(10,file='231028_0131.iq',status='old',access='stream')
  open(12,file='231028_0131.cfom',status='unknown',access='stream')

  mygrid='FN20OG'
  nyear=2023
  month=10
  nday=28
  uth8=01 + 31.d0/60
  nfreq=1296
  call astrosub00(nyear,month,nday,uth8,nfreq,mygrid,ndop0)
  call astrosub00(nyear,month,nday,uth8+1.d0/60.d0,nfreq,mygrid,ndop1)

  print*,ndop0,ndop1
  
  read(10) fcenter,id2(1:2,1:56*96000)
  id2(1:2,56*96000+1:NMAX)=0

  dop0=0.5*ndop0
  dop1=0.5*ndop1
  j=0
  w=1.0
  do isec=1,60
     dop=dop0 + (isec-0.5)*(dop1-dop0)/60.
     dphi=dop*twopi/96000.0
     wstep=cmplx(cos(dphi),sin(dphi))
     do n=1,96000
        j=j+1
        x=id2(1,j)
        y=id2(2,j)
        w=w*wstep
        c=100.d0*w*cmplx(x,y)
        id2(1,j)=0.01d0*real(c)
        id2(2,j)=0.01d0*aimag(c)
     enddo
  enddo

  write(12) fcenter,id2
        
end program cfom_iq
