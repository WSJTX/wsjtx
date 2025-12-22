subroutine addit(itone,nfsample,nsym,nsps,ifreq,sig,dat)

  integer itone(nsym)
  real dat(60*12000)
  real*8 f,dt,twopi,phi,dphi,baud,fsample,freq,tsym,t

  tsym=nsps*1.d0/nfsample            !Symbol duration
  baud=1.d0/tsym
  fsample=12000.d0                   !Sample rate (Hz)
  dt=1.d0/fsample                    !Sample interval (s)
  twopi=8.d0*atan(1.d0)
  dphi=0.

  f=ifreq
  phi=0.
  ntot=nsym*tsym/dt
  k=12000                             !Start audio at t = 1.0 s
  t=0.
  isym0=-1
  do i=1,ntot
     t=t+dt
     isym=nint(t/tsym) + 1
     if(isym.gt.nsym) exit
     if(isym.ne.isym0) then
        freq=f + itone(isym)*baud
        dphi=twopi*freq*dt
        isym0=isym
     endif
     phi=phi + dphi
     if(phi.gt.twopi) phi=phi-twopi
     xphi=phi
     k=k+1
     dat(k)=dat(k) + sig*sin(xphi)
  enddo

  return
end subroutine addit

subroutine addcw(icw,ncw,ifreq,sig,dat)

  integer icw(ncw)
  real dat(60*12000)
  real s(60*12000)
  real x(60*12000)
  real y(60*12000)
  real*8 dt,twopi,phi,dphi,fsample,tdit,t

  wpm=25.0
  nspd=nint(1.2*12000.0/wpm)
  fsample=12000.d0                   !Sample rate (Hz)
  dt=1.d0/fsample                    !Sample interval (s)
  tdit=nspd*dt
  twopi=8.d0*atan(1.d0)
  dphi=twopi*ifreq*dt
  phi=0.
  k=12000                             !Start audio at t = 1.0 s
  t=0.
  npts=59*12000
  x=0.
  do i=1,npts
     t=t+dt
     j=nint(t/tdit) + 1
     j=mod(j-1,ncw) + 1
     phi=phi + dphi
     if(phi.gt.twopi) phi=phi-twopi
     xphi=phi
     k=k+1
     x(k)=icw(j)
     s(k)=sin(xphi)
     if(t.ge.59.5) exit
  enddo

  nadd=0.004/dt
  call smo(x,npts,y,nadd)
  y=y/nadd
  dat=dat + sig*y*s

  return
end subroutine addcw
