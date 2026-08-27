subroutine msk40_freq_search(cdat,fc,if1,if2,delf,nframes,navmask,cb,    &
     cdat2,xmax,bestf,cs,xccs)

  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite

  parameter (NSPM=240)
  complex cdat(NSPM*nframes)
  complex cdat2(NSPM*nframes)
  complex c(NSPM)                    !Coherently averaged complex data
  complex ct2(2*NSPM)
  complex cs(NSPM)
  complex cb(42)                     !Complex waveform for sync word 
  complex cc(0:NSPM-1)
  real xcc(0:NSPM-1)
  real xccs(0:NSPM-1)
  integer navmask(nframes)           !Tells which frames to average

  xmax=0.0
  bestf=0.0
  cs=0.0
  xccs=0.0

  navg=sum(navmask)
  if(navg.le.0 .or. if1.gt.if2) return
  n=nframes*NSPM
!  fac=1.0/(48.0*sqrt(float(navg)))
  fac=1.0/(24.0*sqrt(float(navg)))

  do ifr=if1,if2                     !Find freq that maximizes sync
     ferr=ifr*delf
     call tweak1(cdat,n,-(fc+ferr),cdat2)
     c=0
     do i=1,nframes
        ib=(i-1)*NSPM+1
        ie=ib+NSPM-1
        if( navmask(i) .eq. 1 ) c=c+cdat2(ib:ie)
     enddo

     cc=0
     ct2(1:NSPM)=c
     ct2(NSPM+1:2*NSPM)=c

     do ish=0,NSPM-1
        cc(ish)=dot_product(ct2(1+ish:42+ish),cb(1:42))
     enddo

     xcc=abs(cc)
     if(.not.all(ieee_is_finite(xcc))) cycle
     xb=maxval(xcc)*fac
     if(.not.ieee_is_finite(xb)) cycle
     if(ifr.eq.if1 .or. xb.gt.xmax) then
        xmax=xb
        bestf=ferr
        cs=c
        xccs=xcc
     endif
  enddo

!  write(71,3001) fc,delf,if1,if2,nframes,bestf,xmax
!3001 format(2f8.3,3i5,2f8.3)

  return
end subroutine msk40_freq_search
