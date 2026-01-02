subroutine gen_syncwave(csync)
  integer barker13(13)
  complex csync(13*192)
  data barker13/0,0,0,0,0,1,1,0,0,1,0,1,0/

  twopi=8.0*atan(1.0)
  fsample = 6000.0
  dt=1.0/fsample
  nsps=192            ! 6000 S/s
  baud=fsample/nsps 
  cwave=0
  k=1
  phi=0.0
  do i=1,13
   dphi=twopi*baud*barker13(i)*dt
   do j=1,nsps
      csync(k)=cmplx(cos(phi),sin(phi))
      k=k+1
      phi=phi+dphi
   enddo
  enddo

  return
end subroutine gen_syncwave
