subroutine gen_fst280wave(itone,nsym,nsps,nwave,fsample,hmod,f0,    &
     icmplx,cwave,wave)

  real wave(nwave)
  complex cwave(nwave)
  real, allocatable, save :: pulse(:)
  real, allocatable :: dphi(:)
  integer hmod
  integer itone(nsym)
  logical first
  data first/.true./
  data nsps0/-99/
  save first,twopi,dt,tsym,nsps0

  if(first.or.nsps.ne.nsps0) then
     if(allocated(pulse)) deallocate(pulse)
     allocate(pulse(1:3*nsps))
     twopi=8.0*atan(1.0)
     dt=1.0/fsample
     tsym=nsps/fsample
! Compute the smoothed frequency-deviation pulse
     do i=1,3*nsps
        tt=(i-1.5*nsps)/real(nsps)
        pulse(i)=gfsk_pulse(2.0,tt)
     enddo
     first=.false.
     nsps0=nsps
  endif

! Compute the smoothed frequency waveform.
! Length = (nsym+2)*nsps samples, zero-padded
  allocate( dphi(0:(nsym+2)*nsps-1) )
  dphi_peak=twopi*hmod/real(nsps)
  dphi=0.0 
  do j=1,nsym        
     ib=(j-1)*nsps
     ie=ib+3*nsps-1
     dphi(ib:ie) = dphi(ib:ie) + dphi_peak*pulse(1:3*nsps)*itone(j)
  enddo

! Calculate and insert the audio waveform
  phi=0.0
  dphi = dphi + twopi*(f0-1.5*hmod/tsym)*dt       !Shift frequency up by f0
  if(icmplx.eq.0) wave=0.
  if(icmplx.eq.1) cwave=0.
  k=0
  do j=0,(nsym+2)*nsps-1
     k=k+1
     if(icmplx.eq.0) then
        wave(k)=sin(phi)
     else
        cwave(k)=cmplx(cos(phi),sin(phi))
     endif
     phi=mod(phi+dphi(j),twopi)
  enddo

! Compute the ramp-up and ramp-down symbols
  if(icmplx.eq.0) then
     wave(1:nsps/2)=0.0
     wave(nsps/2+1:nsps)=wave(nsps/2+1:nsps) *                                &
          (1.0-cos(twopi*(/(i,i=0,nsps/2-1)/)/real(nsps)))/2.0
     k1=(nsym+1)*nsps+1
     wave(k1+nsps/2:)=0.0
     wave(k1:k1+nsps/2-1)=wave(k1:k1+nsps/2-1) *                              &
          (1.0+cos(twopi*(/(i,i=0,nsps/2-1)/)/real(nsps)))/2.0
  else
     cwave(1:nsps/2)=0.0
     cwave(nsps/2+1:nsps)=cwave(nsps/2+1:nsps) *                               &
          (1.0-cos(twopi*(/(i,i=0,nsps/2-1)/)/real(nsps)))/2.0
     k1=(nsym+1)*nsps+1
     cwave(k1+nsps/2:)=0.0
     cwave(k1:k1+nsps/2-1)=cwave(k1:k1+nsps/2-1) *                              &
          (1.0+cos(twopi*(/(i,i=0,nsps/2-1)/)/real(nsps)))/2.0
  endif

  return
end subroutine gen_fst280wave
