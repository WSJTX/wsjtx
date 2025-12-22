subroutine fst4_baseline(s,np,ia,ib,npct,sbase)

! Fit baseline to spectrum (for FST4)
! Input:  s(npts)         Linear scale in power
! Output: sbase(npts)    Baseline

  implicit real*8 (a-h,o-z)
  real*4 s(np),sw(np)
  real*4 sbase(np)
  real*4 base
  real*8 x(1000),y(1000),a(5)
  data nseg/8/

  do i=ia,ib
     sw(i)=10.0*log10(s(i))            !Convert to dB scale
  enddo

  nterms=3
  nlen=(ib-ia+1)/nseg                 !Length of test segment
  i0=(ib-ia+1)/2                      !Midpoint
  k=0
  do n=1,nseg                         !Loop over all segments
     ja=ia + (n-1)*nlen
     jb=ja+nlen-1
     call pctile(sw(ja),nlen,npct,base) !Find lowest npct of points
     do i=ja,jb
        if(sw(i).le.base) then
           if (k.lt.1000) k=k+1       !Save all "lower envelope" points
           x(k)=i-i0
           y(k)=sw(i)
        endif
     enddo
  enddo
  kz=k
  a=0.
  call polyfit(x,y,y,kz,nterms,0,a,chisqr)  !Fit a low-order polynomial
  sbase=0.0
  do i=ia,ib
     t=i-i0
     sbase(i)=a(1)+t*(a(2)+t*(a(3))) + 0.2
!     write(51,3051) i,sw(i),sbase(i)
!3051 format(i8,2f12.3)
  enddo

  sbase=10**(sbase/10.0)

  return
end subroutine fst4_baseline
