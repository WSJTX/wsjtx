subroutine gen_cw_wave(message,ifreq,wave)

! Generate audio waveform for a short CW message.

  parameter (NMAX=98304)                 !2.048*48000
  character*(*) message
  integer icw(200)                       !Encoded CW message bits
  complex cdat(NMAX)                     !Complex waveform
  complex z(NMAX)
  real*8 dt,twopi,phi,dphi,fsample,tdit,t
  real*4 wave(NMAX)                      !Generated waveform
  real x(NMAX)
  real y(NMAX)

  call morse(message,icw,ncw)

  i1=0
  i2=0
  do i=1,ncw
     if(i1.eq.0 .and. icw(i).eq.1) i1=i
     if(icw(i).eq.1) i2=i
  enddo
  if(i1.lt.1 .or. i2.gt.200) go to 999
  ncw=i2-i1+1
  icw(1:ncw)=icw(i1:i2)

  fsample=48000.d0
  nspd=2.048d0*fsample/ncw
  wpm=1.2d0*fsample/nspd
  dt=1.d0/fsample                    !Sample interval (s)
  tdit=nspd*dt
  twopi=8.d0*atan(1.d0)
  dphi=twopi*ifreq*dt
  phi=0.
  k=0                                !Start audio at t=0
  t=0.
  x=0.
  do i=1,NMAX
     t=t+dt
     j=t/tdit + 1
     phi=phi + dphi
     if(phi.gt.twopi) phi=phi-twopi
     xphi=phi
     k=k+1
     x(k)=icw(j)
     z(k)=cmplx(cos(xphi),sin(xphi))
  enddo

  nadd=0.001/dt
  call smo(x,NMAX,y,nadd)
  call smo(y,NMAX,x,nadd)
  call smo(x,NMAX,y,nadd)
  call smo(y,NMAX,x,nadd)
  call smo(x,NMAX,y,nadd)
  fac=0.99999/maxval(abs(y))
  cdat=fac*y*z
  wave=real(cdat)
  
999 return
end subroutine gen_cw_wave
