subroutine qpc_sync(crcvd0,fsample,isync,fsync,ftol,f2,t2,snrsync)

  parameter(N9SEC=9*12000,NMAX=15*12000,NDOWN=16,NZ=N9SEC/NDOWN)
  complex crcvd0(NMAX)                   !Signal as received
  complex c0(0:N9SEC-1)                  !For long FFT
  complex c1(0:NZ-1)
  complex c1sum(0:NZ-1)
  complex z
  real s(N9SEC/4)
  real p(-1125:1125)
  integer ipk(1)
  integer isync(24)

  baud=12000.0/1024.0
  df2=fsample/N9SEC
  fac=1.0/N9SEC
  c0=fac*crcvd0(1:N9SEC)
  call four2a(c0,N9SEC,1,-1,1)                !Forward c2c FFT
  iz=N9SEC/4
  do i=1,iz
     s(i)=real(c0(i))**2 + aimag(c0(i))**2
  enddo

  do i=1,4                                    !Smooth the spectrum a bit
     call smo121(s,iz)
  enddo

  ia=nint((fsync-ftol)/df2)
  ib=nint((fsync+ftol)/df2)
  
  if (ia .lt. 1) ia=1;             ! Avoid a potential bounds error
  if (ib .ge. 27000) ib=27000;     ! Avoid a potential bounds error
  
  ipk=maxloc(s(ia:ib))
  i0=ipk(1) + ia - 1
  f2=df2*i0-750.0                  ! f2 is the offset from nominal 750 Hz.
  ia=nint(i0-baud/df2)
  ib=nint(i0+baud/df2)
  
  if (ia .lt. 1) ia=1;             ! Avoid a potential bounds error
  if (ib .ge. 27000) ib=27000;     ! Avoid a potential bounds error
  
  s1=0.0
  s0=0.0
  do i=ia,ib
    s0=s0+s(i)
    s1=s1+(i-i0)*s(i)
  enddo
  if(.not. (s0 .gt. 0.0)) then
     f2=fsync-750.0
     t2=0.0
     snrsync=0.0
     return
  endif
  delta=s1/s0
  i0=nint(i0+delta)
  f2=i0*df2-750.0

  c1=0.
  ia=nint(i0-baud/df2)
  ib=nint(i0+baud/df2)
  
  if (ia .lt. 0) ia=0;             ! Avoid a potential bounds error
  if (ib .ge. N9SEC-1) ib=N9SEC-1; ! Avoid a potential bounds error
  
  do i=ia,ib
     j=i-i0
     if(j.ge.0) c1(j)=c0(i)
     if(j.lt.0) c1(j+NZ)=c0(i)
  enddo
  call four2a(c1,NZ,1,1,1)                 !Reverse c2c FFT: back to time domain

  c1sum(0)=c1(0)
  do i=1,NZ-1
     c1sum(i)=c1sum(i-1) + c1(i)
  enddo

  nspsd=1024/NDOWN
  dt=NDOWN/12000.0
  lagmax=1.5/dt
  i0=nint(0.5*fsample/NDOWN)              !Nominal start time is 0.5 s
  pmax=0.
  lagpk=0
  do lag=-lagmax,lagmax
     sp=0.
     do j=1,24
        i1=i0 + (isync(j)-1)*nspsd + lag
        i2=i1 + nspsd
        if(i1.lt.0 .or. i1.gt.NZ-1) cycle
        if(i2.lt.0 .or. i2.gt.NZ-1) cycle
        z=c1sum(i2)-c1sum(i1)
        sp=sp + real(z)**2 + aimag(z)**2
     enddo
     if(sp.gt.pmax) then
        pmax=sp
        lagpk=lag
     endif
     p(lag)=sp
  enddo

  t2=lagpk*dt
  snrsync=0.
  sp=0.
  sq=0.
  nsum=0
  tsym=1024/12000.0
  do lag=-lagmax,lagmax
     t=(lag-lagpk)*dt
     if(abs(t).lt.tsym) cycle
     nsum=nsum+1
     sp=sp + p(lag)
     sq=sq + p(lag)*p(lag)
  enddo
  if(nsum .le. 0) then
     snrsync=0.0
     return
  endif
  ave=sp/nsum
  var=sq/nsum-ave*ave
  if(.not. (var .gt. 0.0)) then
     snrsync=0.0
     return
  endif
  rms=sqrt(var)
  if(rms .gt. 0.0) then
     snrsync=(pmax-ave)/rms
  else
     snrsync=0.0
  endif

  return
end subroutine qpc_sync
