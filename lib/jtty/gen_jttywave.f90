subroutine gen_jttywave(itone,nsym,nsps,bt,fsample,f0,cwave,wave,icmplx,nwave)
  use ft8_waveform_generator, only: gen_ft8wave_samples
  implicit none
  integer, intent(in) :: nsym,nsps,icmplx,itone(nsym)
  integer, intent(inout) :: nwave
  real, intent(in) :: bt,fsample,f0
  real, intent(out) :: wave(nwave)
  complex, intent(out) :: cwave(nwave)

  if(nsym.le.0.or.nwave.le.0) then
     nwave=0
     return
  endif
  if(icmplx.eq.0) then
     call gen_ft8wave_samples(itone,nsym,nsps,bt,fsample,f0,nwave,1,nwave,wave=wave)
  else
     call gen_ft8wave_samples(itone,nsym,nsps,bt,fsample,f0,nwave,1,nwave,cwave=cwave)
  endif
end subroutine gen_jttywave
