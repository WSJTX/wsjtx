module ft8_waveform_generator
  use iso_fortran_env, only: real64
  use waveform_kernels, only: waveform_workspace,render_gfsk,apply_waveform_ramp
  implicit none
  private
  public :: gen_ft8wave_samples,render_ft8_samples
  type(waveform_workspace), save :: workspace
!$omp threadprivate(workspace)
contains
  subroutine gen_ft8wave_samples(itone,nsym,nsps,bt,fsample,f0,nwave, &
       sample_stride,output_count,cwave,wave)
    integer, intent(in) :: nsym,nsps,nwave,sample_stride,output_count
    integer, intent(in) :: itone(nsym)
    real, intent(in) :: bt,fsample,f0
    complex, intent(out), optional :: cwave(output_count)
    real, intent(out), optional :: wave(output_count)
    call render_ft8_samples(workspace,itone,nsym,nsps,bt,fsample,f0,nwave, &
         sample_stride,output_count,cwave=cwave,wave=wave)
  end subroutine
  subroutine render_ft8_samples(workspace,itone,nsym,nsps,bt,fsample,f0,nwave, &
       sample_stride,output_count,cwave,wave)
    type(waveform_workspace), intent(inout) :: workspace
    integer, intent(in) :: nsym,nsps,nwave,sample_stride,output_count
    integer, intent(in) :: itone(nsym)
    real, intent(in) :: bt,fsample,f0
    complex, intent(out), optional :: cwave(output_count)
    real, intent(out), optional :: wave(output_count)
    real(real64) :: carrier
    integer :: nramp
    if(sample_stride.lt.1.or.nwave.lt.1) error stop 'Invalid emission stride or sample count'
    if(output_count.ne.1+(nwave-1)/sample_stride) error stop 'Incorrect emitted sample count'
    carrier=(8.0_real64*atan(1.0_real64))*real(f0,real64)/real(fsample,real64)
    call render_gfsk(workspace,itone,nsps,fsample,bt,1,carrier,nsps,nwave, &
         sample_stride,.true.,present(cwave),wave=wave,cwave=cwave)
    nramp=nint(nsps/8.0)
    if(nramp.le.0) return
    if(present(wave)) then
       call apply_waveform_ramp(wave,sample_stride,0,nramp,real(2*nramp),-1.0)
       call apply_waveform_ramp(wave,sample_stride,nsym*nsps-nramp,nramp,real(2*nramp),1.0)
    else
       call apply_waveform_ramp(cwave,sample_stride,0,nramp,real(2*nramp),-1.0)
       call apply_waveform_ramp(cwave,sample_stride,nsym*nsps-nramp,nramp,real(2*nramp),1.0)
    endif
  end subroutine
end module

subroutine gen_ft8wave(itone,nsym,nsps,bt,fsample,f0,cwave,wave,icmplx,nwave)
  use ft8_waveform_generator, only: gen_ft8wave_samples
  implicit none
  integer, intent(in) :: nsym,nsps,icmplx,nwave
  integer, intent(in) :: itone(nsym)
  real, intent(in) :: bt,fsample,f0
  complex :: cwave(nwave)
  real :: wave(nwave)
  if(icmplx.eq.0) then
    call gen_ft8wave_samples(itone,nsym,nsps,bt,fsample,f0,nwave,1,nwave,wave=wave)
  else
    call gen_ft8wave_samples(itone,nsym,nsps,bt,fsample,f0,nwave,1,nwave,cwave=cwave)
  endif
end subroutine

subroutine stream_ft8wave_12k(itone,f0,wave) bind(C, name='stream_ft8wave_12k')
  use, intrinsic :: iso_c_binding, only: c_int,c_float
  use ft8_waveform_generator, only: gen_ft8wave_samples
  implicit none
  integer(c_int), intent(in) :: itone(79)
  real(c_float), value :: f0
  real(c_float), intent(out) :: wave(79*1920)
  call gen_ft8wave_samples(itone,79,4*1920,2.0_c_float,48000.0_c_float,f0, &
       79*4*1920,4,79*1920,wave=wave)
end subroutine
