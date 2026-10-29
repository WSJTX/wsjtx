subroutine genwave(itone,nsym,nsps,nwave,fsample,tonespacing,f0,icmplx,cwave,wave)
  use iso_fortran_env, only: real64
  use waveform_kernels, only: render_fsk
  implicit none
  integer, intent(in) :: nsym,nsps,nwave,icmplx,itone(nsym)
  real, intent(in) :: fsample,f0
  real(real64), intent(in) :: tonespacing
  real, intent(out) :: wave(nwave)
  complex, intent(out) :: cwave(nwave)
  if(icmplx.eq.1) then
     call render_fsk(itone,nsps,real(fsample,real64),tonespacing,real(f0,real64),cwave=cwave)
  else
     call render_fsk(itone,nsps,real(fsample,real64),tonespacing,real(f0,real64),wave=wave)
  endif
end subroutine
