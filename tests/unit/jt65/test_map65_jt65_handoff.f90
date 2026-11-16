program test_map65_jt65_handoff

  use iso_fortran_env, only: real32, real64
  use, intrinsic :: ieee_arithmetic, only: ieee_get_flag, ieee_invalid, ieee_is_finite, &
       ieee_quiet_nan, ieee_set_flag, ieee_value
  use decode1a_mod, only: decode1a, jt65_demod_bin, jt65_demod_sample_rate, &
       jt65_fft_size, jt65c_center_offset
  use filbig_mod
  use afc65b_mod, only: afc65b
  use jt65_test_fixture
  use jt65_test_vectors
  use npar_ptrs_mod, only: nsmax_active
  implicit none

  real(real32), allocatable :: dd(:,:), mode_b_wave(:,:)
  real(real64), parameter :: map65_base_frequency = 118.0_real64 * 11025.0_real64 / 1024.0_real64

  allocate(dd(4, nsmax_active))
  allocate(mode_b_wave(2, nsmax_active))
  call make_map65_wave(mode_b_wave, standard_tones, 2, map65_base_frequency)
  dd(1:2,:) = mode_b_wave
  call test_mono_filter_output(dd)
  call test_afc_zero_signal()
  call test_demodulation_bins()
  call make_map65_wave(dd, standard_tones, 1, map65_base_frequency)
  call test_mode(dd, 1, 1, 'JT65A MAP65 handoff')
  dd(1:2,:) = mode_b_wave
  call test_mode(dd, 2, 1, 'JT65B MAP65 handoff')
  call make_map65_wave(dd, standard_tones, 4, map65_base_frequency)
  call test_mode(dd, 4, 1, 'JT65C MAP65 handoff')
  call make_map65_wave(dd, ooo_tones, 1, map65_base_frequency)
  call test_mode(dd, 1, -1, 'JT65A OOO MAP65 handoff')
  dd(1:2,:) = mode_b_wave
  call test_mode(dd, 2, 1, 'JT65B xpol MAP65 handoff', .true.)
  deallocate(dd, mode_b_wave)
  print '(a)', 'MAP65 JT65 handoff tests passed'

contains

  subroutine test_demodulation_bins()
    real(real64) :: frequency, mapped_frequency
    integer :: bin, mode65, signed_bin, tone

    do mode65=1,4
       if(mode65.eq.3) cycle
       do tone=1,66
          bin=jt65_demod_bin(mode65,tone)
          call require(bin.ge.1 .and. bin.le.jt65_fft_size, &
               'JT65 demodulation bin is in range')
          frequency=real((tone-1)*mode65,real64)*jt65_demod_sample_rate/ &
               jt65_fft_size
          if(mode65.eq.4) frequency=frequency-jt65c_center_offset
          signed_bin=bin-1
          if(signed_bin.gt.jt65_fft_size/2) signed_bin=signed_bin-jt65_fft_size
          mapped_frequency=real(signed_bin,real64)*jt65_demod_sample_rate/ &
               jt65_fft_size
          call require(abs(mapped_frequency-frequency).lt. &
               jt65_demod_sample_rate/jt65_fft_size, &
               'JT65 tone frequency maps to the selected FFT bin')
          call require(abs(frequency).lt.0.5_real64*jt65_demod_sample_rate, &
               'JT65 data tone fits the demodulation passband')
       enddo
    enddo
  end subroutine test_demodulation_bins

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require

  subroutine test_mono_filter_output(waveform)
    real(real32), intent(inout) :: waveform(:,:)
    complex(real32), allocatable :: cx(:), cy(:)
    integer :: newdat, n5
    logical :: invalid_raised

    allocate(cx(nsmax_active/64), cy(nsmax_active/64))
    waveform(3:4,:) = ieee_value(0.0_real32, ieee_quiet_nan)
    newdat = 1
    call ieee_set_flag(ieee_invalid, .false.)
    call filbig(waveform, nsmax_active, map65_base_frequency, newdat, 96000, .false., cx, cy, n5)
    call ieee_get_flag(ieee_invalid, invalid_raised)

    call require(.not. invalid_raised, 'mono filter avoids invalid inactive-channel arithmetic')
    call require(all(ieee_is_finite(real(cy(:n5)))) .and. &
         all(ieee_is_finite(aimag(cy(:n5)))), 'mono filter output is finite')
    call require(all(abs(cy(:n5)) == 0.0), 'mono filter output is zero')
  end subroutine test_mono_filter_output

  subroutine test_afc_zero_signal()
    integer, parameter :: npts = 1024
    complex :: cx(npts), cy(npts)
    real :: a(5), ccfbest, dtbest
    integer :: ipol, nflip, ndphi
    logical :: invalid_raised

    cx = (0.0, 0.0)
    cy = (0.0, 0.0)
    a = 0.0
    ipol = 1
    nflip = 1
    ndphi = 0
    call ieee_set_flag(ieee_invalid, .false.)
    call afc65b(cx, cy, npts, 1378.125/4.0, nflip, ipol, .false., ndphi, a, ccfbest, dtbest)
    call ieee_get_flag(ieee_invalid, invalid_raised)

    call require(.not. invalid_raised, 'flat AFC objective avoids invalid arithmetic')
    call require(all(ieee_is_finite(a(1:4))) .and. ieee_is_finite(ccfbest) .and. &
         ieee_is_finite(dtbest), 'flat AFC objective produces finite outputs')
    call require(all(a(1:3) == 0.0) .and. a(4) == 0.0, &
         'flat AFC objective preserves the accepted parameter point')
    call require(ccfbest == 0.0 .and. dtbest == 0.0 .and. ipol == 1, &
         'flat AFC objective has explicit detection defaults')
  end subroutine test_afc_zero_signal

  subroutine test_mode(waveform, mode65, polarity, description, cross_polarized)
    real(real32), intent(inout) :: waveform(:,:)
    integer, intent(in) :: mode65
    integer, intent(in) :: polarity
    character(len=*), intent(in) :: description
    logical, intent(in), optional :: cross_polarized
    integer :: newdat, nflip, nfsample, neme, ndepth, nqd, ndphi
    integer :: nutc, nkhz, ndf, ipol, ntol, nkv, nhist, nsum, nsave
    logical :: invalid_raised, xpol
    character(len=12) :: mycall, hiscall
    character(len=6) :: hisgrid
    character(len=22) :: decoded
    real :: dphi, sync2, a(5), dt, pol, qual

    xpol = .false.
    if (present(cross_polarized)) xpol = cross_polarized
    if (xpol) then
       waveform(3:4,:) = waveform(1:2,:)
    else
       waveform(3:4,:) = ieee_value(0.0_real32, ieee_quiet_nan)
    endif
    newdat = 1
    nflip = polarity
    nfsample = 96000
    mycall = 'K1ABC       '
    hiscall = 'W9XYZ       '
    hisgrid = 'FN42  '
    neme = 0
    ndepth = 3
    nqd = 1
    dphi = 0.0
    ndphi = 0
    nutc = 1
    nkhz = 1
    ndf = 0
    ipol = 0
    ntol = 100
    sync2 = 0.0
    a = 0.0
    dt = -1.7
    pol = 0.0
    nkv = -1
    nhist = -1
    nsum = 0
    nsave = 0
    qual = -999.0
    decoded = '                      '
    call ieee_set_flag(ieee_invalid, .false.)
    call decode1a(waveform, newdat, map65_base_frequency, nflip, mode65, nfsample, xpol, mycall, hiscall, &
         hisgrid, neme, ndepth, nqd, dphi, ndphi, nutc, nkhz, ndf, ipol, ntol, sync2, &
         a, dt, pol, nkv, nhist, nsum, nsave, qual, decoded)
    call ieee_get_flag(ieee_invalid, invalid_raised)

    call require(decoded == padded_message('K1ABC W9XYZ FN42'), description//' message')
    call require(.not. invalid_raised, description//' avoids invalid arithmetic')
    call require(newdat == 0, description//' consumes new data')
    call require(ieee_is_finite(sync2), description//' finite sync metric')
    call require(ieee_is_finite(dt), description//' finite timing')
    call require(ieee_is_finite(pol), description//' finite polarization')
    call require(ieee_is_finite(qual), description//' finite quality')
    call require(ieee_is_finite(a(1)) .and. ieee_is_finite(a(2)), description//' finite AFC')
  end subroutine test_mode

end program test_map65_jt65_handoff
