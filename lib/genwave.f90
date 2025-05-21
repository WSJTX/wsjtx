subroutine genwave(itone,nsym,nsps,nwave,fsample,tonespacing,f0,icmplx, &
     cwave,wave)

  real*8 tonespacing
  real wave(nwave)
  complex cwave(nwave)
  integer itone(nsym)
  real*8 dt,phi,dphi,twopi,freq

  dt=1.d0/fsample
  twopi=8.d0*atan(1.d0)

! Calculate the audio waveform
  phi=0.d0
  if(icmplx.le.0) wave=0.
  if(icmplx.eq.1) cwave=0.
  k=0
  do j=1,nsym
     freq=f0 + itone(j)*tonespacing
     dphi=twopi*freq*dt
     do i=1,nsps
        k=k+1
        if(icmplx.eq.1) then
           cwave(k)=cmplx(cos(phi),sin(phi))
        else
           wave(k)=sin(phi)
        endif
        phi=phi+dphi
        if(phi.gt.twopi) phi=phi-twopi
     enddo
  enddo

  return
end subroutine genwave
