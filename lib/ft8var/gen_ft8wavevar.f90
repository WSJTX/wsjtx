module ft8var_waveform_generator
  use iso_fortran_env, only: real64
  use waveform_kernels, only: waveform_workspace
  use ft8_waveform_generator, only: render_ft8_samples
  implicit none
  private
  public :: gen_ft8wavevar_samples
  type(waveform_workspace), save :: workspace
!$omp threadprivate(workspace)
contains
  subroutine gen_ft8wavevar_samples(itone,nsym,nsps,bt,fsample,f0,nwave, &
       sample_stride,output_count,cwave,wave)
    integer, intent(in) :: nsym,nsps,nwave,sample_stride,output_count
    integer, intent(in) :: itone(nsym)
    real, intent(in) :: bt,fsample,f0
    complex, intent(out), optional :: cwave(output_count)
    real, intent(out), optional :: wave(output_count)
    call render_ft8_samples(workspace,itone,nsym,nsps,bt,fsample,f0,nwave, &
         sample_stride,output_count,cwave=cwave,wave=wave)
  end subroutine
end module

subroutine gen_ft8wavevar(itone,nsym,nsps,bt,fsample,f0,cwave,wave,icmplx,nwave)
  use ft8var_waveform_generator, only: gen_ft8wavevar_samples
  implicit none
  integer, intent(in) :: nsym,nsps,icmplx,nwave
  integer, intent(in) :: itone(nsym)
  real, intent(in) :: bt,fsample,f0
  complex :: cwave(nwave)
  real :: wave(nwave)
  if(icmplx.eq.0) then
    call gen_ft8wavevar_samples(itone,nsym,nsps,bt,fsample,f0,nwave,1,nwave,wave=wave)
  else
    call gen_ft8wavevar_samples(itone,nsym,nsps,bt,fsample,f0,nwave,1,nwave,cwave=cwave)
  endif
end subroutine
