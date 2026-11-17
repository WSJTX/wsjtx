program test_map65_q65_handoff
  use iso_fortran_env, only: int16, real64
  use cacb_mod, only: ca, cb, init_cacb
  use q65b_mod, only: build_q65_samples
  implicit none

  integer, parameter :: nfft2 = 336000, nfft1 = 5376000, sample_rate = 12000
  integer, parameter :: k_auto = 2600000, k_manual = 2600100
  real, parameter :: df = 96000.0/nfft1
  real, parameter :: input_amplitude = 100000000.0
  real(real64), parameter :: pi = acos(-1.0_real64)
  integer(int16), allocatable :: wave(:), reference(:)
  real(real64) :: reference_rms
  integer :: tone_index

  allocate(wave(720000), reference(720000))
  call init_cacb(nfft1)
  call set_tone(k_auto, 1000)
  cb = cmplx(1000000., -1000000.)

  call build_q65_samples(k_auto,nfft2,df,.false.,0,0.0,reference)
  call require(maxval(abs(int(reference))) < 1000, 'X-only samples do not clip')
  call require(all(reference(2*nfft2+1:) == 0), '56-second samples have a zero tail')
  call expect_tone(reference, 1.0_real64, 'automatic center preserves 1000 Hz level and phase')
  reference_rms = rms(reference)

  cb = cmplx(-2000000., 3000000.)
  call build_q65_samples(k_auto,nfft2,df,.false.,180,45.0,wave)
  call require(all(wave == reference), 'single-polarization mode ignores Y')

  cb = ca
  call build_q65_samples(k_auto,nfft2,df,.true.,0,45.0,wave)
  call expect_tone(wave, sqrt(2.0_real64), 'equal X and Y combine coherently')

  cb = -ca
  call build_q65_samples(k_auto,nfft2,df,.true.,0,45.0,wave)
  call require(rms(wave) < 1.0_real64, 'opposite-phase Y cancels without correction')
  call build_q65_samples(k_auto,nfft2,df,.true.,180,45.0,wave)
  call expect_tone(wave, sqrt(2.0_real64), '180-degree correction restores opposite-phase Y')

  tone_index = k_auto + nint(1000.0/df)
  cb = 0.
  cb(tone_index) = cmplx(0.0, -input_amplitude)
  call build_q65_samples(k_auto,nfft2,df,.true.,90,45.0,wave)
  call expect_tone(wave, sqrt(2.0_real64), '90-degree correction preserves rotation sign')

  call set_tone(k_manual, 1000)
  call build_q65_samples(k_manual,nfft2,df,.false.,0,0.0,wave)
  call expect_tone(wave, 1.0_real64, 'shifted extraction offset preserves level and phase')

  call expect_filter(250, 0.5_real64)
  call expect_filter(2750, 0.5_real64)
  call expect_filter(3500, 0.0_real64)

  deallocate(wave, reference)
  print '(a)', 'MAP65 Q65 sample handoff contracts passed.'

contains

  subroutine set_tone(center, frequency)
    integer, intent(in) :: center, frequency
    ca = 0.
    ca(center+nint(real(frequency)/df)) = cmplx(input_amplitude, 0.0)
  end subroutine set_tone

  subroutine expect_tone(samples, gain, description)
    integer(int16), intent(in) :: samples(:)
    real(real64), intent(in) :: gain
    character(len=*), intent(in) :: description
    real(real64) :: expected
    integer :: i

    do i = 1, sample_rate
       expected = gain * 2.0_real64 * real(input_amplitude, real64) / real(nfft2, real64) * &
            cos(2.0_real64*pi*1000.0_real64*real(i, real64)/real(sample_rate, real64))
       if (abs(int(samples(i))-nint(expected)) > 2) then
          print '(a,i0)', 'FAIL: '//description//' at sample ', i
          error stop 1
       endif
    enddo
  end subroutine expect_tone

  subroutine expect_filter(frequency, expected_ratio)
    integer, intent(in) :: frequency
    real(real64), intent(in) :: expected_ratio
    real(real64) :: ratio

    call set_tone(k_auto, frequency)
    call build_q65_samples(k_auto,nfft2,df,.false.,0,0.0,wave)
    ratio = rms(wave)/reference_rms
    call require(abs(ratio-expected_ratio) < 0.01_real64, 'filter response at '//frequency_text(frequency))
  end subroutine expect_filter

  function frequency_text(frequency) result(value)
    integer, intent(in) :: frequency
    character(len=16) :: value
    write(value, '(i0,a)') frequency, ' Hz'
  end function frequency_text

  real(real64) function rms(samples)
    integer(int16), intent(in) :: samples(:)
    integer :: i
    rms = 0.0_real64
    do i=1,sample_rate
       rms = rms + real(samples(i),real64)**2
    enddo
    rms = sqrt(rms/real(sample_rate,real64))
  end function rms

  subroutine require(condition,description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    endif
  end subroutine require
end program test_map65_q65_handoff
