subroutine lpf1(dd,jz,dat,jz2)

  parameter (NFFT1=64*11025,NFFT2=32*11025)
  real dd(jz)
  real dat(jz)
  real x(NFFT1)
  complex cx(0:NFFT1/2)
  equivalence (x,cx)
  save x,cx

  fac=1.0/float(NFFT1)
  x(1:jz)=fac*dd(1:jz)
  x(jz+1:NFFT1)=0.0
  call four2a(cx,NFFT1,1,-1,0)                    !Forwarxd FFT, r2c
  cx(NFFT2/2:)=0.0

!  df=11025.0/NFFT1
!  do i=1,NFFT1/2
!     sx=real(cx(i))**2 + aimag(cx(i))**2
!     write(50,3000) i*df,sx
!3000 format(f15.6,e12.3)
!  enddo

  call four2a(cx,NFFT2,1,1,-1)                   !Inverse FFT, c2r
  jz2=jz/2
  dat(1:jz2)=x(1:jz2)

  return
end subroutine lpf1
