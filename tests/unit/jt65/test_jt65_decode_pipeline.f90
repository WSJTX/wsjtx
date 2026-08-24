module jt65_pipeline_callback

  use jt65_decode
  use jt65_test_vectors, only: padded_message
  implicit none

  integer :: callback_count
  integer :: callback_snr, callback_freq, callback_drift, callback_nflip
  integer :: callback_ft, callback_qual, callback_nsmo, callback_nsum, callback_minsync
  integer :: callback_message_count, callback_max_nsum
  real :: callback_sync, callback_dt, callback_width
  character(len=22) :: callback_message

contains

  subroutine capture_callback(this, sync, snr, dt, freq, drift, nflip, width, &
       decoded, ft, qual, nsmo, nsum, minsync)
    class(jt65_decoder), intent(inout) :: this
    real, intent(in) :: sync, dt, width
    integer, intent(in) :: snr, freq, drift, nflip, ft, qual, nsmo, nsum, minsync
    character(len=22), intent(in) :: decoded

    callback_count = callback_count + 1
    callback_sync = sync
    callback_snr = snr
    callback_dt = dt
    callback_freq = freq
    callback_drift = drift
    callback_nflip = nflip
    callback_width = width
    callback_message = decoded
    callback_ft = ft
    callback_qual = qual
    callback_nsmo = nsmo
    callback_nsum = nsum
    callback_minsync = minsync
    callback_max_nsum = max(callback_max_nsum, nsum)
    if (decoded /= padded_message('')) callback_message_count = callback_message_count + 1
  end subroutine capture_callback

end module jt65_pipeline_callback

program test_jt65_decode_pipeline

  use iso_fortran_env, only: real32
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use jt65_decode
  use jt65_pipeline_callback
  use jt65_test_fixture
  use jt65_test_vectors
  implicit none

  type(jt65_decoder) :: decoder
  real(real32), allocatable :: samples(:)
  allocate(samples(jt65_sample_count))
  call test_clean_modes(samples)
  call test_ooo_signal(samples)
  call test_silence_and_frequency_rejection(samples)
  call test_decoder_reuse(samples, decoder)
  call test_auto_clear_average(samples, decoder)
  print '(a)', 'JT65 decoder pipeline tests passed'

contains

  subroutine reset_capture()
    callback_count = 0
    callback_snr = 0
    callback_freq = 0
    callback_drift = 0
    callback_nflip = 0
    callback_ft = 0
    callback_qual = 0
    callback_nsmo = 0
    callback_nsum = 0
    callback_minsync = 0
    callback_message_count = 0
    callback_max_nsum = 0
    callback_sync = 0.0
    callback_dt = 0.0
    callback_width = 0.0
    callback_message = '                      '
  end subroutine reset_capture

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require

  subroutine decode_signal(decoder_to_use, waveform, nsubmode, clearave, decode_flags, &
       decode_nutc, decode_depth, decode_again)
    type(jt65_decoder), intent(inout) :: decoder_to_use
    real(real32), intent(in) :: waveform(:)
    integer, intent(in) :: nsubmode
    logical, intent(in) :: clearave
    integer, intent(in), optional :: decode_flags, decode_nutc, decode_depth
    logical, intent(in), optional :: decode_again
    logical :: newdat, nagain, nrobust, ljt65apon
    integer :: npts, nutc, nf1, nf2, nfqso, ntol, minsync, n2pass
    integer :: ntrials, naggressive, ndepth, nexp_decode, nQSOProgress
    real :: emedelay
    character(len=12) :: mycall, hiscall
    character(len=6) :: hisgrid

    npts = min(size(waveform), 52 * jt65_sample_rate)
    nutc = 1
    if (present(decode_nutc)) nutc = decode_nutc
    nf1 = 1300
    nf2 = 1700
    nfqso = 1500
    ntol = 100
    minsync = 0
    n2pass = 1
    ntrials = 1000
    naggressive = 5
    ndepth = 3
    if (present(decode_depth)) ndepth = decode_depth
    emedelay = -999.9
    nexp_decode = 32
    if (present(decode_flags)) nexp_decode = decode_flags
    nQSOProgress = 6
    newdat = .true.
    nagain = .true.
    if (present(decode_again)) nagain = decode_again
    nrobust = .false.
    ljt65apon = .false.
    mycall = 'K1ABC       '
    hiscall = 'W9XYZ       '
    hisgrid = 'FN42  '

    call decoder_to_use%decode(capture_callback, waveform, npts, newdat, nutc, nf1, nf2, &
         nfqso, ntol, nsubmode, minsync, nagain, n2pass, nrobust, ntrials, naggressive, &
         ndepth, emedelay, clearave, mycall, hiscall, hisgrid, nexp_decode, nQSOProgress, &
         ljt65apon)
  end subroutine decode_signal

  subroutine require_decode(expected_message, description)
    character(len=*), intent(in) :: expected_message, description
    character(len=22) :: expected

    expected = padded_message(expected_message)
    call require(callback_count == 1, description//' emits exactly one callback')
    call require(callback_message == expected, description//' message')
    call require(abs(real(callback_freq) - 1500.0) <= 4.0, description//' frequency')
    call require(abs(callback_dt) <= 1.0, description//' timing')
    call require(callback_sync > 0.0, description//' sync metric')
    call require(ieee_is_finite(callback_sync), description//' finite sync metric')
    call require(ieee_is_finite(callback_dt), description//' finite timing')
    call require(ieee_is_finite(callback_width), description//' finite width metric')
  end subroutine require_decode

  subroutine test_clean_modes(waveform)
    real(real32), intent(inout) :: waveform(:)
    call reset_capture()
    call make_jt65_wave(waveform, standard_tones, 1, 1500.0_real64)
    call decode_signal(decoder, waveform, 0, .true.)
    call require_decode('K1ABC W9XYZ FN42', 'JT65A clean signal')

    call reset_capture()
    call make_jt65_wave(waveform, standard_tones, 4, 1500.0_real64)
    call decode_signal(decoder, waveform, 2, .true.)
    call require_decode('K1ABC W9XYZ FN42', 'JT65C clean signal')

    call reset_capture()
    call make_jt65_wave(waveform, standard_tones, 2, 1500.0_real64)
    call decode_signal(decoder, waveform, 1, .true.)
    call require_decode('K1ABC W9XYZ FN42', 'JT65B clean signal')
  end subroutine test_clean_modes

  subroutine test_ooo_signal(waveform)
    real(real32), intent(inout) :: waveform(:)
    type(jt65_decoder) :: ooo_decoder

    call reset_capture()
    call make_jt65_wave(waveform, ooo_tones, 1, 1500.0_real64)
    call decode_signal(ooo_decoder, waveform, 0, .true., 64)
    call require_decode('K1ABC W9XYZ FN42', 'JT65A VHF OOO signal')
    call require(callback_nflip < 0, 'JT65A VHF OOO signal polarity')

    call reset_capture()
    call decode_signal(ooo_decoder, waveform, 0, .true.)
    call require(callback_count == 0, 'JT65A HF OOO signal is rejected')
  end subroutine test_ooo_signal

  subroutine test_silence_and_frequency_rejection(waveform)
    real(real32), intent(inout) :: waveform(:)

    waveform = 0.0_real32
    call reset_capture()
    call decode_signal(decoder, waveform, 0, .true.)
    call require(callback_count == 0, 'silence produces no callback')

    call make_jt65_wave(waveform, standard_tones, 1, 3000.0_real64)
    call reset_capture()
    call decode_signal(decoder, waveform, 0, .true.)
    call require(callback_count == 0, 'out-of-window signal produces no callback')
  end subroutine test_silence_and_frequency_rejection

  subroutine test_decoder_reuse(waveform, decoder_to_use)
    real(real32), intent(inout) :: waveform(:)
    type(jt65_decoder), intent(inout) :: decoder_to_use

    call make_jt65_wave(waveform, standard_tones, 1, 1500.0_real64)
    call reset_capture()
    call decode_signal(decoder_to_use, waveform, 0, .true.)
    call require_decode('K1ABC W9XYZ FN42', 'first reused-decoder signal')

    waveform = 0.0_real32
    call reset_capture()
    call decode_signal(decoder_to_use, waveform, 0, .true.)
    call require(callback_count == 0, 'reused decoder silence produces no callback')

    call make_jt65_wave(waveform, standard_tones, 1, 1500.0_real64)
    call reset_capture()
    call decode_signal(decoder_to_use, waveform, 0, .true.)
    call require_decode('K1ABC W9XYZ FN42', 'second reused-decoder signal')
  end subroutine test_decoder_reuse

  subroutine test_auto_clear_average(waveform, decoder_to_use)
    real(real32), intent(inout) :: waveform(:)
    type(jt65_decoder), intent(inout) :: decoder_to_use
    integer, parameter :: averaging_depth = 16 + 128
    real(real64), parameter :: weak_amplitude = 1.1_real64
    real(real64), parameter :: noise_amplitude = 25.0_real64

    call make_jt65_wave(waveform, standard_tones, 1, 1500.0_real64, weak_amplitude, &
         noise_amplitude, 1)
    call reset_capture()
    call decode_signal(decoder_to_use, waveform, 0, .true., 64, 1, averaging_depth, .false.)
    call require(callback_message_count == 0, 'weak JT65 signal is saved without decoding')

    call make_jt65_wave(waveform, standard_tones, 1, 1500.0_real64, weak_amplitude, &
         noise_amplitude, 3)
    call reset_capture()
    call decode_signal(decoder_to_use, waveform, 0, .false., 64, 3, averaging_depth, .false.)
    call require(callback_message_count == 1, 'two weak JT65 signals produce an averaged decode')
    call require(callback_max_nsum >= 2, 'averaged JT65 decode reports multiple periods')

    call make_jt65_wave(waveform, standard_tones, 1, 1500.0_real64, weak_amplitude, &
         noise_amplitude, 5)
    call reset_capture()
    call decode_signal(decoder_to_use, waveform, 0, .false., 64, 5, averaging_depth, .false.)
    call require(callback_message_count == 0, &
         'auto-clear prevents a previous JT65 average from decoding again')
  end subroutine test_auto_clear_average

end program test_jt65_decode_pipeline
