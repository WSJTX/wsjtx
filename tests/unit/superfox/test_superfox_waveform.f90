program test_superfox_waveform
  use iso_fortran_env, only: real32,real64
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use sfox_tx, only: sfox_wave_for_tones
  use waveform_kernels, only: apply_waveform_ramp
  implicit none
  integer, parameter :: nsym=151,nsps=4096,npts=(nsym+2)*nsps,nramp=nsps/8
  real(real64), parameter :: twopi=8.0_real64*atan(1.0_real64)
  real(real64), parameter :: a=4*twopi*sqrt(2.0_real64/log(2.0_real64))
  real(real64), parameter :: peak=twopi/nsps,carrier=twopi*750.0_real64*(1.0_real64/48000)
  real(real32), allocatable :: wave(:)
  real(real64), allocatable :: pulse(:),steps(:)
  real(real64) :: t,phi,factor,error,maximum_error
  real(real32) :: expected
  real(real32) :: ramp(2*nramp),ramp_reference(2*nramp)
  integer :: tones(nsym),pattern,i,j,k,start,k1

  allocate(wave(npts),pulse(3*nsps),steps(0:npts-1))
  do i=0,2*nramp-1
     ramp(i+1)=sin(0.17_real64*i)
     if(i.lt.nramp) then
        ramp_reference(i+1)=ramp(i+1)*(1.0_real64-cos(twopi*i/(2.0_real64*nramp)))/2
     else
        ramp_reference(i+1)=ramp(i+1)*(1.0_real64+cos(twopi*(i-nramp)/(2.0_real64*nramp)))/2
     endif
  enddo
  call apply_waveform_ramp(ramp,1,0,nramp,real(2*nramp,real64),-1.0_real64)
  call apply_waveform_ramp(ramp,1,nramp,nramp,real(2*nramp,real64),1.0_real64)
  if(any(ramp.ne.ramp_reference)) error stop 'Double waveform ramps differ'
  do i=1,3*nsps
     t=real(i,real64)/nsps-1.5_real64
     pulse(i)=0.5_real64*(erf(a*(t+0.5_real64))-erf(a*(t-0.5_real64)))
  enddo
  k1=(nsym+1)*nsps+1
  maximum_error=0
  do pattern=1,12
     select case(pattern)
     case(1)
        tones=0
     case(2)
        tones=128
     case(3)
        tones=[(128*mod(i,2),i=1,nsym)]
     case(4)
        tones=[(128*mod(i+1,2),i=1,nsym)]
     case(5)
        tones=0
        tones(1)=128
     case(6)
        tones=0
        tones(nsym)=128
     case(7)
        tones=0
        tones((nsym+1)/2)=128
     case(8)
        tones=128
        tones((nsym+1)/2)=0
     case(9)
        tones=0
        tones((nsym+1)/2:)=128
     case(10)
        tones=128
        tones((nsym+1)/2:)=0
     case(11)
        tones=[(mod(37*i+11,129),i=1,nsym)]
     case(12)
        tones=35
     end select
     steps=0
     do j=1,nsym
        start=(j-1)*nsps
        steps(start:start+3*nsps-1)=steps(start:start+3*nsps-1)+peak*pulse*tones(j)
     enddo
     steps(0:2*nsps-1)=steps(0:2*nsps-1)+peak*tones(1)*pulse(nsps+1:3*nsps)
     steps(nsym*nsps:npts-1)=steps(nsym*nsps:npts-1)+peak*tones(nsym)*pulse(1:2*nsps)
     steps=steps+carrier
     call sfox_wave_for_tones(tones,wave)
     if(any(wave(1:nsps-nramp).ne.0).or.any(wave(k1+nramp:).ne.0)) &
          error stop 'SuperFox silent spans differ'
     if(wave(nsps-nramp+1).ne.0) error stop 'SuperFox ramp-up does not start at zero'
     phi=0
     do k=1,npts-1
        factor=1
        if(k.le.nsps-nramp.or.k.ge.k1+nramp) then
           factor=0
        elseif(k.le.nsps) then
           i=k-(nsps-nramp+1)
           factor=(1.0_real64-cos(twopi*i/(2.0_real64*nramp)))/2
        elseif(k.ge.k1) then
           i=k-k1
           factor=(1.0_real64+cos(twopi*i/(2.0_real64*nramp)))/2
        endif
        expected=sin(phi)
        expected=expected*factor
        error=abs(real(wave(k),real64)-real(expected,real64))
        if(.not.ieee_is_finite(error).or.error.gt.1.0e-6_real64) then
           print *, 'SuperFox waveform error',pattern,k,error
           error stop 1
        endif
        maximum_error=max(maximum_error,error)
        ! SuperFox emits its initial zero phase before consuming source increment 1.
        phi=phi+steps(k)
        if(phi.ge.twopi) phi=phi-twopi
     enddo
  enddo
  do i=1,2
     tones=0
     tones(1)=-1
     if(i.eq.2) tones(1)=129
     wave=1
     call sfox_wave_for_tones(tones,wave)
     if(any(wave.ne.0)) error stop 'Invalid SuperFox tones produced audio'
  enddo
  print '(a,es14.6)', 'SuperFox waveform tests passed; maximum error ',maximum_error
end program test_superfox_waveform
