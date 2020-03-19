subroutine get_spectrum_baseline(dd,nfa,nfb,sbase)

  include 'ft8_params.f90'
  parameter(NST=NFFT1/2,NF=93)              !NF=NMAX/NST-1
  real s(NH1,NF)
  real savg(NH1)
  real sbase(NH1)
  real x(NFFT1)
  real window(NFFT1)
  complex cx(0:NH1)
  real dd(NMAX)
  equivalence (x,cx)
  logical first
  data first/.true./
  save first,window

  if(first) then
    first=.false.
    pi=4.0*atan(1.)
    window=0.
    call nuttal_window(window,NFFT1)
    window=window/sum(window)*NSPS*2/300.0
  endif

! Compute symbol spectra, stepping by NSTEP steps.  
  savg=0.
  df=12000.0/NFFT1  
  do j=1,NF
     ia=(j-1)*NST + 1
     ib=ia+NFFT1-1
     if(ib.gt.NMAX) exit
     x=dd(ia:ib)*window
     call four2a(x,NFFT1,1,-1,0)              !r2c FFT
     s(1:NH1,j)=abs(cx(1:NH1))**2
     savg=savg + s(1:NH1,j)                   !Average spectrum
  enddo

  if(nfa.lt.100) nfa=100
  if(nfb.gt.4910) nfb=4910
  call baseline(savg,nfa,nfb,sbase)

return
end subroutine get_spectrum_baseline
