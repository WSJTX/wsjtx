subroutine gen_fst280wave(itone,nsym,nsps,nwave,fsample,hmod,f0,    &
     icmplx,cwave,wave)

  parameter(NTAB=65536)
  real wave(nwave)
  complex cwave(nwave),ctab(0:NTAB-1)
  real, allocatable, save :: pulse(:)
  real, allocatable :: dphi(:)
  integer hmod
  integer itone(nsym)
!  integer*8 count0,count1,clkfreq
  logical first
  data first/.true./
  data nsps0/-99/
  save first,twopi,dt,tsym,nsps0,ctab

!  call system_clock(count0,clkfreq)
  if(first) then
     twopi=8.0*atan(1.0)
     do i=0,NTAB-1
        phi=i*twopi/NTAB
        ctab(i)=cmplx(cos(phi),sin(phi))
     enddo
  endif

  if(first.or.nsps.ne.nsps0) then
     if(allocated(pulse)) deallocate(pulse)
     allocate(pulse(1:3*nsps))
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
     i=phi*float(NTAB)/twopi
     i=iand(i,NTAB-1)
     if(icmplx.eq.0) then
        wave(k)=real(ctab(i))
     else
        cwave(k)=ctab(i)
     endif
     phi=phi+dphi(j)
     if(phi.gt.twopi) phi=phi-twopi
  enddo

! Compute the ramp-up and ramp-down symbols
  kshift=nsps-nint(fsample)
  if(icmplx.eq.0) then
     wave(1:nsps/2)=0.0
     wave(nsps/2+1:nsps)=wave(nsps/2+1:nsps) *                                &
          (1.0-cos(twopi*(/(i,i=0,nsps/2-1)/)/real(nsps)))/2.0
     k1=(nsym+1)*nsps+1
     wave(k1+nsps/2:)=0.0
     wave(k1:k1+nsps/2-1)=wave(k1:k1+nsps/2-1) *                              &
          (1.0+cos(twopi*(/(i,i=0,nsps/2-1)/)/real(nsps)))/2.0
     wave=cshift(wave,kshift)
  else
     cwave(1:nsps/2)=0.0
     cwave(nsps/2+1:nsps)=cwave(nsps/2+1:nsps) *                               &
          (1.0-cos(twopi*(/(i,i=0,nsps/2-1)/)/real(nsps)))/2.0
     k1=(nsym+1)*nsps+1
     cwave(k1+nsps/2:)=0.0
     cwave(k1:k1+nsps/2-1)=cwave(k1:k1+nsps/2-1) *                              &
          (1.0+cos(twopi*(/(i,i=0,nsps/2-1)/)/real(nsps)))/2.0
     cwave=cshift(cwave,kshift)
  endif

!  call system_clock(count1,clkfreq)
!  tt=float(count1-count0)/float(clkfreq)
!  write(*,3001) tt
!3001 format('Tgen:',f8.3)
  
  return
end subroutine gen_fst280wave
