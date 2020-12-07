subroutine genwave(itone,nsym,nsps,nwave,fsample,hmod,f0,icmplx,cwave,wave)

  real wave(nwave)
  complex cwave(nwave)
  integer hmod
  integer itone(nsym)
  logical ex
  real*8 dt,phi,dphi,twopi,freq,baud

  dt=1.d0/fsample
  twopi=8.d0*atan(1.d0)
  baud=fsample/nsps

! Calculate the audio waveform
  phi=0.d0
  if(icmplx.le.0) wave=0.
  if(icmplx.eq.1) cwave=0.
  k=0
  do j=1,nsym
     freq=f0 + itone(j)*hmod*baud
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

!### TEMPORARY code to allow transmitting both A and B submodes
  inquire(file='Q65_Tx2',exist=ex)
  if(ex) then
     k=0
     do j=1,nsym
        freq=f0 + itone(j)*2.d0*hmod*baud + 500.d0
        dphi=twopi*freq*dt
        do i=1,nsps
           k=k+1
           wave(k)=0.5*(wave(k)+sin(phi))
           phi=phi+dphi
           if(phi.gt.twopi) phi=phi-twopi
        enddo
     enddo
  endif
!###

  return
end subroutine genwave
