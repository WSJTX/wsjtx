subroutine symspec(shared_data,k,ntrperiod,nsps,ingain,bLowSidelobes,    &
     nminw,pxdb,s,df3,ihsym,npts8,pxdbmax)

! Input:
!  k              pointer to the most recent new data
!  ntrperiod      T/R sequence length, minutes
!  nsps           samples per symbol, at 12000 Hz
!  bLowSidelobes  true to use windowed FFTs
!  ndiskdat       0/1 to indicate if data from disk
!  nb             0/1 status of noise blanker (off/on)
!  nbslider       NB setting, 0-100

! Output:
!  pxdb      raw power (0-90 dB)
!  s()       current spectrum for waterfall display
!  ihsym     index number of this half-symbol (1-184) for 60 s modes

! jt9com
!  ss()      JT9 symbol spectra at half-symbol steps
!  savg()    average spectra for waterfall display

  use, intrinsic :: iso_c_binding, only: c_int, c_short, c_float, c_char
  include 'jt9com.f90'

  type(dec_data) :: shared_data
  real*4 w3(MAXFFT3)
  real*4 s(NSMAX)
  real*4 ssum(NSMAX)
  real*4 xc(0:MAXFFT3-1)
  real*4 tmp(NSMAX)
  complex cx(0:MAXFFT3/2)
  integer nch(7)
  logical*1 bLowSidelobes

  common/spectra/syellow(NSMAX),ref(0:3456),filter(0:3456)
  data k0/99999999/,nfft3z/0/
  data nch/1,2,4,9,18,36,72/
  equivalence (xc,cx)
  save

  if(ntrperiod.eq.-999) stop                   !Silence compiler warning
  nfft3=16384                                  !df=12000.0/16384 = 0.732422
  jstep=nsps/2                                 !Step size = half-symbol in id2()
  if(k.gt.NMAX) go to 900
  if(k.lt.2048) then                !(2048 was nfft3)  (Any need for this ???)
     ihsym=0
     go to 900                                 !Wait for enough samples to start
  endif

  if(nfft3.ne.nfft3z) then
! Compute new window
     pi=4.0*atan(1.0)
     w3=0
     call nuttal_window(w3,nfft3)
     nfft3z=nfft3
  endif

  if(k.lt.k0) then                             !Start a new data block
     ja=0
     ssum=0.
     ihsym=0
     if(.not. shared_data%params%ndiskdat) shared_data%id2(k+1:)=0   !Needed to prevent "ghosts". Not sure why.
  endif
  gain=10.0**(0.1*ingain)
  sq=0.
  pxmax=0.;
  do i=k0+1,k
     x1=shared_data%id2(i)
     if (abs(x1).gt.pxmax) pxmax = abs(x1);
     sq=sq + x1*x1
  enddo
  pxdb = 0.
  if(sq.gt.0.0) pxdb=10*log10(sq/(k-k0))
  pxdbmax=0.
  if(pxmax.gt.0) pxdbmax = 20*log10(pxmax)

  k0=k
  ja=ja+jstep                         !Index of first sample

  fac0=0.1
  do i=0,nfft3-1                      !Copy data into cx
     j=ja+i-(nfft3-1)
     xc(i)=0.
     if(j.ge.1 .and.j.le.NMAX) xc(i)=fac0*shared_data%id2(j)
  enddo
  ihsym=ihsym+1

  if(bLowSidelobes) xc(0:nfft3-1)=w3(1:nfft3)*xc(0:nfft3-1)    !Apply window 
  call four2a(xc,nfft3,1,-1,0)        !Real-to-complex FFT

  df3=12000.0/nfft3                   !JT9: 0.732 Hz = 0.42 * tone spacing
  iz=min(NSMAX,nint(5000.0/df3))
  fac=(1.0/nfft3)**2
  do i=1,iz
     j=i-1
     if(j.lt.0) j=j+nfft3
     sx=fac*(real(cx(j))**2 + aimag(cx(j))**2)
     if(ihsym.le.184) shared_data%ss(ihsym,i)=sx
     ssum(i)=ssum(i) + sx
     s(i)=1000.0*gain*sx
  enddo

  shared_data%savg=ssum/ihsym

  if(mod(ihsym,10).eq.0) then
     mode4=nch(nminw+1)
     nsmo=min(10*mode4,150)
     nsmo=4*nsmo
     call flat1(shared_data%savg,iz,nsmo,syellow)
     if(mode4.ge.2) call smo(syellow,iz,tmp,mode4)
     if(mode4.ge.2) call smo(syellow,iz,tmp,mode4)
     syellow(1:250)=0.
     ia=500./df3
     ib=2700.0/df3
     smin=minval(syellow(ia:ib))
     smax=maxval(syellow(1:iz))
     syellow=(50.0/(smax-smin))*(syellow-smin)
     where(syellow<0) syellow=0.
  endif

900 npts8=k/8

  return
end subroutine symspec
