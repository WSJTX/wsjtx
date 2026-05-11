module symspec_mod
  implicit none
contains

subroutine symspec(k,nxpol,ndiskdat,nb,nbslider,idphi,nfsample,    &
     iqadjust,iqapply,gainx,gainy,phasex,phasey,rejectx,rejecty,  &
     pxdb,pydb,ssz5a,nkhz,ihsym,nzap,slimit,lstrong) bind(C, name='symspec_')

  use iso_c_binding
  use datcom_ptrs_mod
  use npar_ptrs_mod,  only: fcenter
  use four2a_mod
  use timf2_mod
  use iqcal_mod
  use iqfix_mod
  use iso_fortran_env, only: real32, real64

  ! C-facing arguments (keep these as in your modern version)
  integer(c_int),    intent(in)    :: k, nxpol, ndiskdat, nb, nbslider
  integer(c_int),    intent(in)    :: idphi, nfsample
  integer(c_int),    intent(in)    :: iqadjust, iqapply
  real(c_float),     intent(inout) :: gainx, gainy, phasex, phasey
  real(c_float),     intent(out)   :: rejectx, rejecty, pxdb, pydb
  real(c_float),     intent(out)   :: ssz5a(*)
  integer(c_int),    intent(out)   :: nkhz
  integer(c_int),    intent(inout) :: ihsym, nzap
  real(c_float),     intent(inout) :: slimit
  integer(c_signed_char), intent(out) :: lstrong(0:1023)

  
  ! ---- from legacy body, adapted to modules ----
  ! (no COMMON, no local fcenter, no logical*1 lstrong)


  real(real64) :: ts, hsym
  integer :: i, ipkx, ipky, iqadjust0, iqapply0, j, ja, jb, k0, k1, kstep, &
             mm, nadjx, nadjy, nblk, nblks, nfft2, npts, nsum, nwindow, n, nfast
  real(real32) :: fac, faclim, peaklimit, px, py, q, rejectx0, rms, rmsx, rmsy, &
             s135, s45, sigmas, sx, sy, u, x1, x2, x3, x4, dphi, pi
  real(real32) :: w(NFFT), w2a(NFFT), w2b(NFFT)
  complex :: z, zfac, zsumx, zsumy, cx(NFFT), cy(NFFT), cx00(NFFT), cy00(NFFT)
  complex :: cx0(0:1023), cx1(0:1023), cy0(0:1023), cy1(0:1023)
 
  data rms/999.0/, k0/99999999/, k1/0/, nadjx/0/, nadjy/0/
  save

nfast = 1

if (k.gt.5751000) goto 999
if (k.lt.NFFT) then
   ihsym = 0
   goto 999
endif

  if(k0.eq.99999999) then
     pi=4.0*atan(1.0)
     w2a=0.
     w2b=0.
     do i=1,NFFT
        w(i)=(sin(i*pi/NFFT))**2                          !Window for nfast=1
        if(i.lt.17833) w2a(i)=(sin(i*pi/17832.925))**2    !Window a for nfast=2
        j=i-8916
        if(j.gt.0 .and. j.lt.17833) w2b(i)=(sin(j*pi/17832.925))**2    ! b
     enddo
     w2a=sqrt(2.0)*w2a
     w2b=sqrt(2.0)*w2b
  endif

   hsym=2048.d0*96000.d0/11025.d0
  if(nfsample.eq.95238)   hsym=2048.d0*95238.1d0/11025.d0

if (k.lt.k0) then
   ! Perform the legacy reset
   ts    = 1.d0 - hsym
   savg  = 0.
   ihsym = 0
   k1    = 0
   if (ndiskdat.eq.0) dd(1:4,k+1:5760000)=0.
endif

k0 = k

  
  nzap=0
  sigmas=1.5*(10.0**(0.01*nbslider)) + 0.7
  peaklimit=sigmas*max(10.0,rms)
  faclim=3.0
  px=0.
  py=0.

  iqapply0=0
  iqadjust0=0
  if(iqadjust.ne.0) iqapply0=0
  nwindow=2
!  nwindow=0                                    !### No windowing ###
  nfft2=1024
  kstep=nfft2
  if(nwindow.ne.0) kstep=nfft2/2
  nblks = (k - k1) / kstep
  
  do nblk=1,nblks
     j=k1+1
     do i=0,nfft2-1
        cx0(i)=cmplx(dd(1,j+i),dd(2,j+i))
        if(nxpol.ne.0) cy0(i)=cmplx(dd(3,j+i),dd(4,j+i))
     enddo
     call timf2(k,nxpol,nfft2,nwindow,nb,peaklimit,iqadjust0,iqapply0,       &
          faclim,cx0,cy0,gainx,gainy,phasex,phasey,cx1,cy1,slimit,lstrong,   &
          px,py,nzap)

     do i=0,kstep-1
        dd(1,j+i)=real(cx1(i))
        dd(2,j+i)=aimag(cx1(i))
        if(nxpol.ne.0) then
           dd(3,j+i)=real(cy1(i))
           dd(4,j+i)=aimag(cy1(i))
        endif
     enddo
     k1=k1+kstep
  enddo

  npts=NFFT                           !Samples used in each half-symbol FFT

  ts=ts+hsym
  ja=ts   ! ts already set from ihsym above; do not integrate it again                             !Index of first sample
  jb=ja+npts-1                        !Last sample

  i=0
  fac=0.0002
  dphi=idphi/57.2957795
  zfac=fac*cmplx(cos(dphi),sin(dphi))

  do j=ja,jb                          !Copy data into cx, cy
     x1=dd(1,j)
     x2=dd(2,j)
     if(nxpol.ne.0) then 
        x3=dd(3,j)
        x4=dd(4,j)
     else
        x3=0.
        x4=0.
     endif
     i=i+1
     cx(i)=fac*cmplx(x1,x2)
     cy(i)=zfac*cmplx(x3,x4)          !NB: cy includes dphi correction
  enddo

  if(nzap/178.lt.50 .and. (ndiskdat.eq.0 .or. ihsym.lt.280)) then
     nsum=nblks*kstep - nzap
     if(nsum.le.0) nsum=1
     rmsx=sqrt(0.5*px/nsum)
     rmsy=sqrt(0.5*py/nsum)
     rms=rmsx
     if(nxpol.ne.0) rms=sqrt((px+py)/(4.0*nsum))
  endif
  pxdb=0.
  pydb=0.
  if(rmsx.gt.1.0) pxdb=20.0*log10(rmsx)
  if(rmsy.gt.1.0) pydb=20.0*log10(rmsy)
  if(pxdb.gt.60.0) pxdb=60.0
  if(pydb.gt.60.0) pydb=60.0

  cx00=cx
  if(nxpol.ne.0) cy00=cy

  do mm=1,nfast
     ihsym=ihsym+1
     if(nfast.eq.1) then
        cx=w*cx00                           !Apply window for 2nd forward FFT
        if(nxpol.ne.0) cy=w*cy00
     else
        if(mm.eq.1) then
           cx=w2a*cx00
           if(nxpol.ne.0) cy=w2a*cy00
        else
           cx=w2b*cx00
           if(nxpol.ne.0) cy=w2b*cy00
        endif
     endif

     call four2a(cx,NFFT,1,1,1)          !Second forward FFT (X)
     if(iqadjust.eq.0) nadjx=0
     if(iqadjust.ne.0 .and. nadjx.lt.50) call iqcal(nadjx,cx,NFFT,    &
          gainx,phasex,zsumx,ipkx,rejectx0)
     if(iqapply.ne.0) call iqfix(cx,NFFT,gainx,phasex)

     if(nxpol.ne.0) then
        call four2a(cy,NFFT,1,1,1)       !Second forward FFT (Y)
        if(iqadjust.eq.0) nadjy=0
        if(iqadjust.ne.0 .and. nadjy.lt.50) call iqcal(nadjy,cy,NFFT, &
             gainy,phasey,zsumy,ipky,rejecty)
        if(iqapply.ne.0) call iqfix(cy,NFFT,gainy,phasey)
     endif

     n=min(322,ihsym)
     do i=1,NFFT
        sx=real(cx(i))**2 + aimag(cx(i))**2  
        ss(1,n,i)=sx                    ! Pol = 0
        savg(1,i)=savg(1,i) + sx
       
        if(nxpol.ne.0) then
           z=cx(i) + cy(i)
           s45=0.5*(real(z)**2 + aimag(z)**2)
           ss(2,n,i)=s45                   ! Pol = 45
           savg(2,i)=savg(2,i) + s45

           sy=real(cy(i))**2 + aimag(cy(i))**2
           ss(3,n,i)=sy                    ! Pol = 90
           savg(3,i)=savg(3,i) + sy
        
           z=cx(i) - cy(i)
           s135=0.5*(real(z)**2 + aimag(z)**2)
           ss(4,n,i)=s135                  ! Pol = 135
           savg(4,i)=savg(4,i) + s135

           z=cx(i)*conjg(cy(i))
           q=sx - sy
           u=2.0*real(z)
           ssz5a(i)=0.707*sqrt(q*q + u*u)    !Spectrum of linear polarization
! Leif's formula:
!     ssz5a(i)=0.5*(sx+sy) + (real(z)**2 + aimag(z)**2 - sx*sy)/(sx+sy)
        else
           ssz5a(i)=sx
        endif
     enddo
  enddo

! NOTE: ipkx and ipky are 0 when no peak was found. The reject ratio
! calculation is only valid when both are non-zero. The guard
! (ipkx.ne.0 .and. ipky.ne.0) ensures that indices such as
! 1+NFFT-ipkx never evaluate to NFFT+1. No out-of-bounds access occurs.

  if(ihsym.eq.278) then
     if(iqadjust.ne.0 .and. ipkx.ne.0 .and. ipky.ne.0) then
        rejectx=10.0*log10(savg(1,1+nfft-ipkx)/savg(1,1+ipkx))
        rejecty=10.0*log10(savg(3,1+nfft-ipky)/savg(3,1+ipky))
     endif
  endif

  nkhz=nint(1000.d0*(fcenter-int(fcenter)))
  if(fcenter.eq.0.d0) nkhz=125

999 return
end subroutine symspec


end module symspec_mod
