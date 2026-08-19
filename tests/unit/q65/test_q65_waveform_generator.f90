program test_q65_waveform_generator

  use iso_fortran_env, only: real32, real64
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use cgen65_mod, only: cgen65
  use gen_q65_cwave_mod, only: gen_q65_cwave
  implicit none

  external :: gen_q65_wave

  integer, parameter :: sample_rate = 96000
  integer, parameter :: waveform_capacity = 60 * sample_rate
  real(real64), parameter :: pi = 3.1415926535897932384626433832795_real64
  real(real64), parameter :: q65_symbol_duration = 7200.0_real64 / 12000.0_real64
  real(real64), parameter :: jt65_base_frequency = 118.0_real64 * 11025.0_real64 / 1024.0_real64

  complex, allocatable :: q65_wave(:), jt65_wave(:)
  integer*2, allocatable :: legacy_q65_wave(:)

  allocate(q65_wave(waveform_capacity), jt65_wave(waveform_capacity))
  allocate(legacy_q65_wave(2 * 60 * 11025))
  call test_q65_complex_wave(q65_wave, 1)
  call test_q65_complex_wave(q65_wave, 2)
  call test_q65_invalid_message(q65_wave)
  call test_q65_legacy_wave(legacy_q65_wave)
  call test_jt65_complex_wave(jt65_wave)
  print '(a)', 'Q65 waveform generator tests passed'

contains

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require

  subroutine test_q65_complex_wave(waveform, tone_spacing)
    complex, intent(inout) :: waveform(:)
    integer, intent(in) :: tone_spacing
    character(len=24) :: message, msgsent
    integer :: nwave, expected_nwave, sample
    real(real64) :: fsample, expected_frequency, measured_frequency
    complex :: step

    fsample = real(sample_rate, real64)
    message = 'K1ABC W9XYZ FN42'
    waveform = cmplx(0.0_real32, 0.0_real32)

    call gen_q65_cwave(message, 1000, tone_spacing, fsample, msgsent, waveform, nwave)

    expected_nwave = int(85.0_real64 * q65_symbol_duration * fsample + 0.5_real64)
    call require(nwave == expected_nwave, 'Q65 waveform length')
    call require(trim(msgsent) == trim(message), 'Q65 generated message')
    call require(all(ieee_is_finite(real(waveform(1:nwave)))), 'Q65 real samples are finite')
    call require(all(ieee_is_finite(aimag(waveform(1:nwave)))), 'Q65 imaginary samples are finite')
    call require(maxval(abs(abs(waveform(1:nwave)) - 1.0)) < 1.0e-5, &
         'Q65 complex envelope is constant')

    step = waveform(1)
    call require(abs(real(step) - real(cos(2.0_real64*pi*1000.0_real64/fsample), real32)) < 1.0e-5, &
         'Q65 I orientation')
    call require(abs(aimag(step) + real(sin(2.0_real64*pi*1000.0_real64/fsample), real32)) < 1.0e-5, &
         'Q65 Q orientation')

    sample = int(q65_symbol_duration * fsample) + 1000
    step = conjg(waveform(sample)) * waveform(sample + 1)
    measured_frequency = -atan2(real(aimag(step), real64), real(real(step), real64)) * &
         fsample / (2.0_real64*pi)
    expected_frequency = 1000.0_real64 + 3.0_real64 * tone_spacing * 12000.0_real64 / 7200.0_real64
    call require(abs(measured_frequency - expected_frequency) < 0.2_real64, &
         'Q65 tone spacing')
    call require_phase_steps(waveform, nwave - 1, 'Q65')
  end subroutine test_q65_complex_wave

  subroutine test_q65_invalid_message(waveform)
    complex, intent(inout) :: waveform(:)
    character(len=24) :: message, msgsent
    integer :: nwave

    message = 'HELLO@WORLD'
    waveform = cmplx(1.0_real32, 1.0_real32)
    call gen_q65_cwave(message, 1000, 1, real(sample_rate, real64), msgsent, waveform, nwave)

    call require(nwave == 0, 'invalid Q65 waveform length')
    call require(trim(msgsent) == '*** bad message ***', 'invalid Q65 generated message')
    call require(all(waveform == cmplx(0.0_real32, 0.0_real32)), 'invalid Q65 output is cleared')
  end subroutine test_q65_invalid_message

  subroutine test_q65_legacy_wave(waveform)
    integer*2, intent(inout) :: waveform(:)
    character(len=22) :: message, msgsent
    integer :: nwave, expected_nwave, sample
    real(real64) :: envelope

    message = 'K1ABC W9XYZ FN42'
    waveform = 0
    call gen_q65_wave(message, 1000, 1, msgsent, waveform, nwave)

    expected_nwave = 2 * int(85.0_real64 * q65_symbol_duration * 11025.0_real64 + 0.5_real64)
    call require(nwave == expected_nwave, 'legacy Q65 waveform length')
    call require(trim(msgsent) == trim(message), 'legacy Q65 generated message')
    do sample = 1, nwave, 2
       envelope = sqrt(real(waveform(sample), real64)**2 + &
            real(waveform(sample + 1), real64)**2)
       call require(abs(envelope - 32767.0_real64) < 2.0_real64, &
            'legacy Q65 I/Q envelope stays constant')
    end do
    call require(abs(real(waveform(1)) - real(32767.0_real64 * &
         cos(2.0_real64*pi*1000.0_real64/11025.0_real64), real32)) < 1.0, &
         'legacy Q65 I orientation')
    call require(abs(real(waveform(2)) - real(32767.0_real64 * &
         sin(2.0_real64*pi*1000.0_real64/11025.0_real64), real32)) < 1.0, &
         'legacy Q65 Q orientation')
  end subroutine test_q65_legacy_wave

  subroutine test_jt65_complex_wave(waveform)
    complex, intent(inout) :: waveform(:)
    character(len=22) :: message, msgsent
    integer :: nsendingsh, nwave, ndata
    real(real64) :: samfac
    complex :: step

    message = 'K1ABC W9XYZ FN42'
    samfac = 1.0_real64
    waveform = cmplx(0.0_real32, 0.0_real32)
    call cgen65(message, 1, samfac, nsendingsh, msgsent, waveform, nwave)

    ndata = int(126.0_real64 * real(sample_rate, real64) * samfac * 4096.0_real64 / 11025.0_real64)
    call require(nsendingsh == 0, 'JT65 normal message shorthand flag')
    call require(trim(msgsent) == trim(message), 'JT65 generated message')
    call require(nwave == ndata + 48000, 'JT65 waveform length')
    call require(all(ieee_is_finite(real(waveform(1:nwave)))), 'JT65 real samples are finite')
    call require(all(ieee_is_finite(aimag(waveform(1:nwave)))), 'JT65 imaginary samples are finite')
    call require(all(abs(waveform(ndata+1:nwave)) < 1.0e-5), 'JT65 padding is silent')

    step = waveform(1)
    call require(abs(real(step) - real(cos(2.0_real64*pi*jt65_base_frequency/sample_rate), real32)) < 1.0e-5, &
         'JT65 I orientation')
    call require(abs(aimag(step) + real(sin(2.0_real64*pi*jt65_base_frequency/sample_rate), real32)) < 1.0e-5, &
         'JT65 Q orientation')
    call require_phase_steps(waveform, ndata - 1, 'JT65')
  end subroutine test_jt65_complex_wave

  subroutine require_phase_steps(waveform, last_sample, description)
    complex, intent(in) :: waveform(:)
    integer, intent(in) :: last_sample
    character(len=*), intent(in) :: description
    complex :: step
    real(real64) :: phase_step
    integer :: sample

    do sample = 1, last_sample
       step = conjg(waveform(sample)) * waveform(sample + 1)
       phase_step = atan2(real(aimag(step), real64), real(real(step), real64))
       call require(abs(phase_step) < 0.25_real64, description//' phase step stays bounded')
    end do
  end subroutine require_phase_steps

end program test_q65_waveform_generator
