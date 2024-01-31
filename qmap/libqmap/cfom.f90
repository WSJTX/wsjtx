subroutine cfom(dd,k0,k,ndop0)
  
  parameter(NMAX=60*96000)
  real dd(2,NMAX)
  complex*16 w,wstep
  complex*8 c
  real*8 twopi,dphi
  logical first
  data first/.true./
  save twopi,w,first

  if(first) then
     twopi=8.d0*atan(1.d0)
     w=1.d0
     first=.false.
  endif

  dop0=0.5*ndop0
  dphi=dop0*twopi/96000.0
  wstep=cmplx(cos(dphi),sin(dphi))

  do j=k0+1,k
     c=w*cmplx(dd(1,j),dd(2,j))
     dd(1,j)=real(c)
     dd(2,j)=aimag(c)
     w=w*wstep
  enddo

  return
end subroutine cfom

subroutine zaptx(dd,k0,k)

  parameter(NMAX=60*96000)
  real dd(2,NMAX)

  dd(1:2,k0+1:k)=0.

  return
end subroutine zaptx
