module ft8_waveform_generator
  implicit none
  private
  public :: gen_ft8wave_samples
contains
  subroutine gen_ft8wave_samples(itone,nsym,nsps,bt,fsample,f0,nwave, &
       sample_stride,output_count,cwave,wave)
    use ft8_waveform, only: emit_ft8_waveform
    use timer_module, only: timer
    integer, intent(in) :: nsym,nsps,nwave,sample_stride,output_count
    integer, intent(in) :: itone(nsym)
    real, intent(in) :: bt,fsample,f0
    complex, intent(out), optional :: cwave(output_count)
    real, intent(out), optional :: wave(output_count)
    integer, parameter :: NTAB=65536
    complex :: ctab(0:NTAB-1)
    real :: pulse(23040),dphi(0:(nsym+2)*nsps-1)
    real :: twopi,dt,hmod,dphi_peak,tt,phi
    real, external :: gfsk_pulse
    integer :: ibt,i,j,ib,ie
    real :: fchk,fchk0
    data fchk0/0.0/
    save pulse,twopi,dt,hmod,fchk0,ctab

    ibt=nint(10*bt)
    fchk=nsym+nsps+bt+fsample
    if(fchk.ne.fchk0) then
       twopi=8.0*atan(1.0)
       dt=1.0/fsample
       hmod=1.0
  ! Compute the frequency-smoothing pulse
       do i=1,3*nsps
          tt=(i-1.5*nsps)/real(nsps)
          pulse(i)=gfsk_pulse(bt,tt)
       enddo
       do i=0,NTAB-1
          phi=i*twopi/NTAB
          ctab(i)=cmplx(cos(phi),sin(phi))
       enddo
       fchk0=fchk
    endif

  ! Compute the smoothed frequency waveform.
  ! Length = (nsym+2)*nsps samples, first and last symbols extended
    dphi_peak=twopi*hmod/real(nsps)
    dphi=0.0
    do j=1,nsym
       ib=(j-1)*nsps
       ie=ib+3*nsps-1
       dphi(ib:ie) = dphi(ib:ie) + dphi_peak*pulse(1:3*nsps)*itone(j)
    enddo
  ! Add dummy symbols at beginning and end with tone values equal to 1st and last symbol, respectively
    dphi(0:2*nsps-1)=dphi(0:2*nsps-1)+dphi_peak*itone(1)*pulse(nsps+1:3*nsps)
    dphi(nsym*nsps:(nsym+2)*nsps-1)=dphi(nsym*nsps:(nsym+2)*nsps-1)+dphi_peak*itone(nsym)*pulse(1:2*nsps)

    dphi = dphi + twopi*f0*dt
    call emit_ft8_waveform(dphi(nsps:nsps+nwave-1),nwave,nsym,nsps,ctab,NTAB, &
         twopi,sample_stride,output_count,cwave=cwave,wave=wave)
  end subroutine gen_ft8wave_samples
end module ft8_waveform_generator

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
end subroutine gen_ft8wave
