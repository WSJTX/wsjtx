subroutine blanker(iwave,nz,ndropmax,npct,c_bigfft)

  integer*2 iwave(nz)
  complex c_bigfft(0:nz/2)
  integer hist(0:32768)
  real fblank                     !Fraction of points to be blanked

  fblank=0.01*npct
  hist=0
  do i=1,nz
! ### NB: if iwave(i)=-32768, abs(iwave(i))=-32768 ###
     if(iwave(i).eq.-32768) iwave(i)=-32767
     n=abs(iwave(i))
     hist(n)=hist(n)+1
  enddo
  n=0
  do i=32768,0,-1
     n=n+hist(i)
     if(n.ge.nint(nz*fblank/ndropmax)) exit
  enddo
  nthresh=i
  ndrop=0
  ndropped=0

  xx=0.
  do i=1,nz
     i0=iwave(i)
     if(ndrop.gt.0) then
        i0=0
        ndropped=ndropped+1
        ndrop=ndrop-1
     endif

! Start to apply blanking
     if(abs(i0).gt.nthresh) then
        i0=0
        ndropped=ndropped+1
        ndrop=ndropmax
     endif
     
! Now copy the data into c_bigfft
     if(iand(i,1).eq.1) then
        xx=i0
     else
        yy=i0
        j=i/2 - 1
        c_bigfft(j)=cmplx(xx,yy)
     endif
  enddo

  return
end subroutine blanker
