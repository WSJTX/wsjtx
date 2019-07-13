subroutine getcandidates2a(id,fa,fb,maxcand,savg,candidate,ncand)

! For now, hardwired to find the largest peak in the average spectrum

  include 'ft2_params.f90'
  real s(NH1,NHSYM)
  real savg(NH1),savsm(NH1)
  real x(NFFT1)
  complex cx(0:NH1)
  real candidate(3,100)
  integer*2 id(NMAX)
  integer*1 s8(8)
  integer indx(NH1)
  data s8/0,1,1,1,0,0,1,0/
  equivalence (x,cx)

! Compute symbol spectra, stepping by NSTEP steps.  
  savg=0.
  tstep=NSTEP/12000.0                         
  df=12000.0/NFFT1                            !3.125 Hz
  fac=1.0/300.0
  do j=1,NHSYM
     ia=(j-1)*NSTEP + 1
     ib=ia+NSPS-1
     x(1:NSPS)=fac*id(ia:ib)
     x(NSPS+1:)=0.
     call four2a(x,NFFT1,1,-1,0)              !r2c FFT
     do i=1,NH1
        s(i,j)=real(cx(i))**2 + aimag(cx(i))**2
     enddo
     savg=savg + s(1:NH1,j)                   !Average spectrum
  enddo
  savsm=0.
  do i=2,NH1-1
    savsm(i)=sum(savg(i-1:i+1))/3.
  enddo
  savsm(1)=savg(1)
  savsm(NH1)=savg(NH1)

  nfa=nint(fa/df)
  nfb=nint(fb/df)
  np=nfb-nfa+1
  indx=0
  call indexx(savsm(nfa:nfb),np,indx)
  xn=savsm(nfa+indx(nint(0.3*np)))
  if(xn.ne.0) savsm=savsm/xn
  imax=-1
  xmax=-99.
  do i=2,NH1-1
    if(savsm(i).gt.savsm(i-1).and.    &
       savsm(i).gt.savsm(i+1).and.    &
       savsm(i).gt.xmax) then
      xmax=savsm(i) 
      imax=i
    endif
  enddo
  f0=imax*df
  if(xmax.gt.1.2) then
     if(ncand.lt.maxcand) ncand=ncand+1
     candidate(1,ncand)=f0
  endif

  return
end subroutine getcandidates2a
