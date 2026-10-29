subroutine gen_ft4wave(itone,nsym,nsps,fsample,f0,cwave,wave,icmplx,nwave)
  use iso_fortran_env, only: real64
  use waveform_kernels, only: waveform_workspace,render_gfsk,apply_waveform_ramp
  implicit none
  integer, intent(in) :: nsym,nsps,icmplx,nwave,itone(nsym)
  real, intent(in) :: fsample,f0
  real, intent(out) :: wave(nwave)
  complex, intent(out) :: cwave(nwave)
  type(waveform_workspace), save :: workspace
!$omp threadprivate(workspace)
  real(real64) :: carrier
  carrier=(8.0_real64*atan(1.0_real64))*real(f0,real64)/real(fsample,real64)
  wave=0
  if(icmplx.eq.0) then
     call render_gfsk(workspace,itone,nsps,fsample,1.0,1,carrier,0,(nsym+2)*nsps,1, &
          .false.,.false.,wave=wave)
     call apply_waveform_ramp(wave,1,0,nsps,real(2*nsps),-1.0)
     call apply_waveform_ramp(wave,1,(nsym+1)*nsps,nsps,real(2*nsps),1.0)
  else
     call render_gfsk(workspace,itone,nsps,fsample,1.0,1,carrier,0,(nsym+2)*nsps,1, &
          .false.,.false.,cwave=cwave)
     call apply_waveform_ramp(cwave,1,0,nsps,real(2*nsps),-1.0)
     call apply_waveform_ramp(cwave,1,(nsym+1)*nsps,nsps,real(2*nsps),1.0)
  endif
end subroutine
