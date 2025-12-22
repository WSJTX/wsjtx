real function fchisq65(cx,npts,fsample,nflip,a,ccfmax,dtmax)

  use timer_module, only: timer

  parameter (NMAX=60*12000)          !Samples per 60 s
  complex cx(npts)
  real a(5)
  complex w,wstep,z
  real ss(3000)
  complex csx(0:NMAX/8)
  data twopi/6.283185307/a1,a2,a3/99.,99.,99./
  save

  call timer('fchisq65',0)
  baud=11025.0/4096.0
  nsps=nint(fsample/baud)                  !Samples per symbol
  nsph=nsps/2                              !Samples per half-symbol
  ndiv=16                                  !Output ss() steps per symbol
  nout=ndiv*npts/nsps
  dtstep=1.0/(ndiv*baud)                   !Time per output step

 if(a(1).ne.a1 .or. a(2).ne.a2 .or. a(3).ne.a3) then
     a1=a(1)
     a2=a(2)
     a3=a(3)

! Mix and integrate the complex signal
     csx(0)=0.
     w=1.0
     x0=0.5*(npts+1)
     s=2.0/npts
     do i=1,npts
        x=s*(i-x0)
        if(mod(i,100).eq.1) then
           p2=1.5*x*x - 0.5
           dphi=(a(1) + x*a(2) + p2*a(3)) * (twopi/fsample)
          wstep=cmplx(cos(dphi),sin(dphi))
        endif
        w=w*wstep
        csx(i)=csx(i-1) + w*cx(i)
     enddo
  endif

! Compute whole-symbol powers at 1/16-symbol steps.
  fac=1.e-4
  do i=1,nout
     j=nsps+(i-1)*nsps/16 !steps by 8 samples (1/16 of a symbol)
     k=j-nsps
     ss(i)=0.
     if(k.ge.0 .and. j.le.npts) then
        z=csx(j)-csx(k) ! difference over span of 128 pts
        ss(i)=fac*(real(z)**2 + aimag(z)**2)
     endif
  enddo

  ccfmax=0.
  call timer('ccf2    ',0)
  call ccf2(ss,nout,nflip,ccf,xlagpk)
  call timer('ccf2    ',1)
  if(ccf.gt.ccfmax) then
     ccfmax=ccf
     dtmax=xlagpk*dtstep
  endif
  fchisq65=-ccfmax
  call timer('fchisq65',1)

  return
end function fchisq65
