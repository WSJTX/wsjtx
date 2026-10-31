module fst4_wavegen
  use iso_fortran_env, only: real64
  use waveform_kernels, only: waveform_workspace,render_gfsk,apply_waveform_ramp
  implicit none
  private
  public :: fst4_wavegen_workspace,gen_fst4wave_owned
  type fst4_wavegen_workspace
     type(waveform_workspace) :: samples
     logical :: first=.true.,shape=.true.
  end type
contains
  subroutine gen_fst4wave_owned(work,itone,nsym,nsps,nwave,fsample,hmod,f0,icmplx,cwave,wave)
    type(fst4_wavegen_workspace), intent(inout) :: work
    integer, intent(in) :: nsym,nsps,nwave,hmod,icmplx,itone(nsym)
    real, intent(in) :: fsample,f0
    real, intent(out) :: wave(*)
    complex, intent(out) :: cwave(*)
    character :: value
    integer :: length,tail
    real(real64) :: carrier,tsym
    if(work%first) then
       call get_environment_variable('FST4_NOSHAPING',value,length)
       work%shape=.not.(length.eq.1.and.value.eq.'1')
       work%first=.false.
    endif
    tsym=real(nsps,real64)/real(fsample,real64)
    carrier=(8.0_real64*atan(1.0_real64))*(real(f0,real64)-1.5_real64*hmod/tsym)/real(fsample,real64)
    tail=(nsym-1)*nsps+3*nsps/4
    if(icmplx.eq.0) then
       call render_gfsk(work%samples,itone,nsps,2.0,hmod,carrier,nsps,nsym*nsps,1, &
            .false.,.true.,wave=wave(1:nwave))
       if(work%shape) then
          call apply_waveform_ramp(wave(1:nwave),1,0,nsps/4,real(nsps/2),-1.0)
          call apply_waveform_ramp(wave(1:nwave),1,tail,nsps/4+1,real(nsps/2),1.0)
       endif
    else
       call render_gfsk(work%samples,itone,nsps,2.0,hmod,carrier,nsps,nsym*nsps,1, &
            .false.,.true.,cwave=cwave(1:nwave))
       if(work%shape) then
          call apply_waveform_ramp(cwave(1:nwave),1,0,nsps/4,real(nsps/2),-1.0)
          call apply_waveform_ramp(cwave(1:nwave),1,tail,nsps/4+1,real(nsps/2),1.0)
       endif
    endif
  end subroutine
end module

subroutine gen_fst4wave(itone,nsym,nsps,nwave,fsample,hmod,f0,icmplx,cwave,wave)
  use fst4_wavegen, only: fst4_wavegen_workspace,gen_fst4wave_owned
  implicit none
  type(fst4_wavegen_workspace), save :: legacy
  integer, intent(in) :: nsym,nsps,nwave,hmod,icmplx,itone(nsym)
  real, intent(in) :: fsample,f0
  real, intent(out) :: wave(*)
  complex, intent(out) :: cwave(*)
  call gen_fst4wave_owned(legacy,itone,nsym,nsps,nwave,fsample,hmod,f0,icmplx,cwave,wave)
end subroutine
