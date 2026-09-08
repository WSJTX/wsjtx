subroutine gen_fst4wave(itone,nsym,nsps,nwave,fsample,hmod,f0,    &
   icmplx,cwave,wave)

   use prog_args
   parameter(NTAB=65536)
   real wave(nwave)
   complex cwave(nwave),ctab(0:NTAB-1)
   character(len=1) :: cvalue 
   real, allocatable, save :: pulse(:)
   real, allocatable :: dphi(:), scaled_pulse(:)
   integer hmod
   integer itone(nsym)
   logical first, lshape
   data first/.true./
   data nsps0/-99/
   data lshape/.true./
   save first,twopi,dt,tsym,nsps0,ctab,lshape

   if(first) then
      twopi=8.0*atan(1.0)
      do i=0,NTAB-1
         phi=i*twopi/NTAB
         ctab(i)=cmplx(cos(phi),sin(phi))
      enddo
      call get_environment_variable("FST4_NOSHAPING",cvalue,nlen)
      if(nlen.eq.1 .and. cvalue.eq."1") lshape=.false.
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

! Generate one symbol at a time, retaining phase across symbol boundaries.
   allocate(dphi(0:nsps-1))
   dphi_peak=twopi*hmod/real(nsps)
   allocate(scaled_pulse(3*nsps))
   scaled_pulse=dphi_peak*pulse
   carrier=twopi*(f0-1.5*hmod/tsym)*dt
   if(icmplx.eq.0) wave(nsym*nsps+1:nwave)=0.
   if(icmplx.eq.1) cwave(nsym*nsps+1:nwave)=0.
   phi=0.0
   k=0
   do iblock=1,nsym
      dphi=0.0
! Accumulate overlapping pulses in tone order to preserve phase rounding.
      do j=max(1,iblock-1),min(nsym,iblock+1)
         ip=(iblock-j+1)*nsps+1
         dphi=dphi+scaled_pulse(ip:ip+nsps-1)*itone(j)
      enddo
      dphi=dphi+carrier
      do j=0,nsps-1
         k=k+1
         i=phi*float(NTAB)/twopi
         i=iand(i,NTAB-1)
         if(icmplx.eq.0) then
            wave(k)=aimag(ctab(i))
         else
            cwave(k)=ctab(i)
         endif
         phi=phi+dphi(j)
         if(phi.gt.twopi) phi=phi-twopi
      enddo
   enddo

! Compute the ramp-up and ramp-down symbols
   if(icmplx.eq.0) then
      if(lshape) then
         wave(1:nsps/4)=wave(1:nsps/4) *                      &
            (1.0-cos(twopi*(/(i,i=0,nsps/4-1)/)/real(nsps/2)))/2.0
         k1=(nsym-1)*nsps+3*nsps/4+1
         wave(k1:k1+nsps/4)=wave(k1:k1+nsps/4) *                              &
            (1.0+cos(twopi*(/(i,i=0,nsps/4)/)/real(nsps/2)))/2.0
      endif
   else
      if(lshape) then
         cwave(1:nsps/4)=cwave(1:nsps/4) *                    &
            (1.0-cos(twopi*(/(i,i=0,nsps/4-1)/)/real(nsps/2)))/2.0
         k1=(nsym-1)*nsps+3*nsps/4+1
         cwave(k1:k1+nsps/4)=cwave(k1:k1+nsps/4) *                              &
            (1.0+cos(twopi*(/(i,i=0,nsps/4)/)/real(nsps/2)))/2.0
      endif
   endif

   return
end subroutine gen_fst4wave
