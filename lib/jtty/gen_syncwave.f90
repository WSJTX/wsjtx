subroutine gen_syncwave(csync,nss)

  implicit none
  integer, intent(in)  :: nss              ! samples/symbol at 6000 samples/sec
  integer              :: barker13(13)
  integer              :: i,j,k
  real                 :: twopi, fsample, dt, baud, phi, dphi
  complex, intent(out) :: csync(13*nss)
  data barker13 /0,0,0,0,0,3,3,0,0,3,0,3,0/

  twopi=8.0*atan(1.0)
  fsample = 6000.0
  dt=1.0/fsample
  baud=fsample/nss 
  csync=cmplx(0.,0.)
  k=1
  phi=0.0
  do i=1,13
   dphi=twopi*baud*barker13(i)*dt
   do j=1,nss
      csync(k)=cmplx(cos(phi),sin(phi))
      k=k+1
      phi=phi+dphi
   enddo
  enddo

  return
end subroutine gen_syncwave
