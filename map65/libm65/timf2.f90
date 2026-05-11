module timf2_mod
  implicit none
contains

subroutine timf2(k,nxpol,nfft,nwindow,nb,peaklimit,iqadjust,iqapply,faclim, &
                 cx0,cy0,gainx,gainy,phasex,phasey,cx1,cy1,slimit,lstrong,px,py,nzap)

  use four2a_mod
  use, intrinsic :: iso_c_binding, only: c_signed_char
  implicit none

  ! Arguments
  integer, intent(in)    :: k, nxpol, nfft, nwindow, nb
  real,    intent(in)    :: peaklimit, faclim
  integer, intent(in)    :: iqadjust, iqapply
  complex, intent(in)    :: cx0(0:nfft-1), cy0(0:nfft-1)
  real,    intent(in)    :: gainx, gainy, phasex, phasey
  complex, intent(out)   :: cx1(0:nfft-1), cy1(0:nfft-1)
  real,    intent(inout) :: slimit
  ! logical*1, intent(out) :: lstrong(0:*)
  integer(c_signed_char), intent(out) :: lstrong(0:*)

  real,    intent(inout) :: px, py
  integer, intent(inout) :: nzap

  ! Parameters
  integer, parameter :: MAXFFT = 1024, MAXNH = MAXFFT/2
  integer, parameter :: MAXSIGS = 100

  ! Local arrays
  complex :: cx(0:MAXFFT-1), cxt(0:MAXFFT-1)
  complex :: cy(0:MAXFFT-1), cyt(0:MAXFFT-1)
  complex :: cxs(0:MAXFFT-1), covxs(0:MAXNH-1)
  complex :: cys(0:MAXFFT-1), covys(0:MAXNH-1)
  complex :: cxw(0:MAXFFT-1), covxw(0:MAXNH-1)
  complex :: cyw(0:MAXFFT-1), covyw(0:MAXNH-1)
  real    :: w(0:MAXFFT-1)
  real    :: s(0:MAXFFT-1)
  logical :: lprev
  integer   :: ia(MAXSIGS), ib(MAXSIGS)

  ! Local scalars
  complex :: h, u, v
  real    :: x, y, p, ave, peak, fac, pi
  integer :: nh, kstep, nsigs, iwid
  integer :: i, ja, jb

  ! Saved state
  logical :: first
  integer :: k0
  data first /.true./
  data k0 /99999999/
  save first, k0, w, s, nh, kstep, fac, covxs, covxw, covys, covyw

  ! Reference faclim and iqadjust to avoid unused warnings
  if (faclim + iqadjust == -9999.0) then
     ! no-op
  end if

  if(first) then
     pi=4.0*atan(1.0)
     do i=0,nfft-1
        w(i)=(sin(i*pi/nfft))**2
     enddo
     s=0.
     nh=nfft/2
     kstep=nfft
     if(nwindow.eq.2) kstep=nh
     fac=1.0/nfft
     slimit=1.e30
     first=.false.
  endif

  if(k.lt.k0) then
     covxs=0.
     covxw=0.
     covys=0.
     covyw=0.
  endif
  k0=k

  cx(0:nfft-1)=cx0
  if(nwindow.eq.2) cx(0:nfft-1)=w(0:nfft-1)*cx(0:nfft-1)
  call four2a(cx,nfft,1,1,1)                       !First forward FFT (X)

  if(nxpol.ne.0) then
     cy(0:nfft-1)=cy0
     if(nwindow.eq.2) cy(0:nfft-1)=w(0:nfft-1)*cy(0:nfft-1)
     call four2a(cy,nfft,1,1,1)                    !First forward FFT (Y)
  endif

  if(iqapply.ne.0) then                            !Apply I/Q corrections (X)
     h=gainx*cmplx(cos(phasex),sin(phasex))
     v=0.
     do i=0,nfft-1
        u=cx(i)
        if(i.gt.0) v=cx(nfft-i)
        x=real(u)  + real(v)  - (aimag(u) + aimag(v))*aimag(h) +         &
             (real(u) - real(v))*real(h)
        y=aimag(u) - aimag(v) + (aimag(u) + aimag(v))*real(h)  +         &
             (real(u) - real(v))*aimag(h)
        cxt(i)=0.5*cmplx(x,y)
     enddo
  else
     cxt(0:nfft-1)=cx(0:nfft-1)
  endif

  if(nxpol.ne.0) then
     if(iqapply.ne.0) then                         !Apply I/Q corrections (Y)
        h=gainy*cmplx(cos(phasey),sin(phasey))
        v=0.
        do i=0,nfft-1
           u=cy(i)
           if(i.gt.0) v=cy(nfft-i)
           x=real(u)  + real(v)  - (aimag(u) + aimag(v))*aimag(h) +         &
                (real(u) - real(v))*real(h)
           y=aimag(u) - aimag(v) + (aimag(u) + aimag(v))*real(h)  +         &
                (real(u) - real(v))*aimag(h)
           cyt(i)=0.5*cmplx(x,y)
        enddo
     else
        cyt(0:nfft-1)=cy(0:nfft-1)
     endif
  endif

! Identify frequencies with strong signals, copy frequency-domain
! data into array cs (strong) or cw (weak).

  do i=0,nfft-1
     p=real(cxt(i))**2 + aimag(cxt(i))**2
     if(nxpol.ne.0) p=p + real(cyt(i))**2 + aimag(cyt(i))**2
     s(i)=p
  enddo
  ave=0.0
  ave=sum(s(0:nfft-1))/nfft
  lstrong(0:nfft-1) = merge(1_c_signed_char, 0_c_signed_char, s(0:nfft-1) > 10.0*ave)

  nsigs=0
  lprev=.false.
  iwid=1
  ib=-99
  do i=0,nfft-1
     if ((lstrong(i) /= 0) .and. (.not. lprev)) then
        if(nsigs.lt.MAXSIGS) nsigs=nsigs+1
        ia(nsigs)=i-iwid
        if(ia(nsigs).lt.0) ia(nsigs)=0
     endif
     if ((lstrong(i) == 0) .and. lprev) then
        ib(nsigs)=i-1+iwid
        if(ib(nsigs).gt.nfft-1) ib(nsigs)=nfft-1
     endif
     lprev = (lstrong(i) /= 0)
  enddo

  if(nsigs.gt.0) then
     do i=1,nsigs
        ja=ia(i)
        jb=ib(i)
        if(ja.lt.0 .or. ja.gt.nfft-1 .or. jb.lt.0 .or. jb.gt.nfft-1) then
           cycle
        endif
        if(jb.eq.-99) jb=ja + min(2*iwid,nfft-1)
        lstrong(ja:jb) = 1_c_signed_char
     enddo
  endif

  do i=0,nfft-1
     if (lstrong(i) /= 0) then
        cxs(i)=fac*cxt(i)
        cxw(i)=0.
        if(nxpol.ne.0) then
           cys(i)=fac*cyt(i)
           cyw(i)=0.
        endif
     else
        cxw(i)=fac*cxt(i)
        cxs(i)=0.
        if(nxpol.ne.0) then
           cyw(i)=fac*cyt(i)
           cys(i)=0.
        endif
     endif
  enddo

  call four2a(cxw,nfft,1,-1,1)                 !Transform weak and strong X
  call four2a(cxs,nfft,1,-1,1)                 !back to time domain, separately

  if(nxpol.ne.0) then
     call four2a(cyw,nfft,1,-1,1)              !Transform weak and strong Y
     call four2a(cys,nfft,1,-1,1)              !back to time domain, separately
  endif

  if(nwindow.eq.2) then
     cxw(0:nh-1)=cxw(0:nh-1)+covxw(0:nh-1)     !Add previous segment's 2nd half
     covxw(0:nh-1)=cxw(nh:nfft-1)              !Save 2nd half
     cxs(0:nh-1)=cxs(0:nh-1)+covxs(0:nh-1)     !Ditto for strong signals
     covxs(0:nh-1)=cxs(nh:nfft-1)

     if(nxpol.ne.0) then
        cyw(0:nh-1)=cyw(0:nh-1)+covyw(0:nh-1)  !Add previous segment's 2nd half
        covyw(0:nh-1)=cyw(nh:nfft-1)           !Save 2nd half
        cys(0:nh-1)=cys(0:nh-1)+covys(0:nh-1)  !Ditto for strong signals
        covys(0:nh-1)=cys(nh:nfft-1)
     endif
  endif

! Apply noise blanking to weak data
  if(nb.ne.0) then
     do i=0,kstep-1
        peak=abs(cxw(i))
        if(nxpol.ne.0) peak=max(peak,abs(cyw(i)))
        if(peak.gt.peaklimit) then
           cxw(i)=0.
           if(nxpol.ne.0) cyw(i)=0.
           nzap=nzap+1
        endif
     enddo
  endif

! Compute power levels from weak data only
  do i=0,kstep-1
     px=px + real(cxw(i))**2 + aimag(cxw(i))**2
     if(nxpol.ne.0) py=py + real(cyw(i))**2 + aimag(cyw(i))**2
  enddo

  cx1(0:kstep-1)=cxw(0:kstep-1) + cxs(0:kstep-1)       !Weak + strong (X)
  if(nxpol.ne.0) then
     cy1(0:kstep-1)=cyw(0:kstep-1) + cys(0:kstep-1)    !Weak + strong (Y)
  endif

  return
end subroutine timf2

end module timf2_mod
