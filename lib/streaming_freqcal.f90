! SPDX-License-Identifier: GPL-3.0-or-later
! FreqCal in the stream session ({"mode":"FREQCAL"}): WSJT-X's frequency
! measurement, freqcal (lib/freqcal.f90) called as MainWindow::dataSink calls
! it, on the period's last 55,296 samples at the end of every 8th
! 3456-sample block from the 16th up to the GUI's stop,
! ((int(trperiod/0.288))/8)*8 half-symbols (MainWindow::fixStop): 1, 3, 5
! or 12 measurements a period at a trperiod of 5, 10, 15 or 30 s, the only
! periods FreqCal takes. lib/streaming_io.f90 reads FreqCal's audio in those
! blocks, through the receive chain (equalized with ref_spectrum "apply" on
! live audio), and calls freqcal_block at each block end, on the stream's own
! thread, as the GUI measures on its GUI thread; a part block is never
! measured. Each measurement is one line:
!   freqcal  {"v":1,"t":"freqcal","time":"HHMMSS","freq":1500.370,
!             "error":0.370,"level":6.3,"snr":55.2,"low_snr":false}
!            time: the period's label (lib/streaming_io.f90) plus k/12000 s,
!            k the block's end in samples from the period's start, in whole
!            seconds, past midnight wrapped; never the clock, which the
!            GUI's line reads. freq: the measured frequency, Hz (fMeas);
!            error: it less rxfreq (DF); level: the noise level plus 8 dB,
!            as the GUI shows it; snr: the peak over the noise (not over
!            level), dB; low_snr: snr below 20, where the GUI's line has its *. Each number is the GUI line's, as it prints it:
!            freq and error to 3 decimals, level and snr to 1. With no peak
!            in the window (silence) freq, level and snr are -99.9 and
!            error -99.9 less rxfreq, as in the GUI; a value the line cannot
!            print as a finite number is null.
! The GUI's line also carries its dial kHz, which only labels it (and so
! nothing here: the dial is the client's), and its time from the clock.
! rxfreq and ntol are the GUI's Rx Freq and F Tol, the search window: from
! rxfreq - ntol to rxfreq + ntol Hz, or from 0 to 2 * rxfreq when ntol is
! above rxfreq. A configure that carries "mode":"FREQCAL" sets the receive
! frequency to 1500 unless it carries rxfreq, as choosing FreqCal sets the
! GUI's Rx Freq (the transmit frequency keeps its value, as the GUI's Tx Freq), and
! one that carries mode or trperiod resets ntol to 50, the GUI's JT9 F Tol,
! unless it carries ntol (lib/streaming_apply.f90). FreqCal takes them in the
! GUI's ranges, 200 to 5000 Hz and 1 to 1000 Hz. Above them (rxfreq + ntol
! beyond 6000 Hz) freqcal would index past its spectrum; at rxfreq + ntol of
! 6000 Hz, a peak in
! the window's top bin makes freqcal read one value past it, as in the GUI,
! which takes the same values. Band stepping, the
! fmt.all log and the fit (calibrate) are the client's. FreqCal publishes no
! decode, as the GUI's dataSink returns before decode() in FreqCal: no
! decode, decode_finished, busy, pass_progress or pass_skipped line, and no
! period to decode again.
module streaming_freqcal
  use, intrinsic :: iso_fortran_env, only: int16
  implicit none
  private
  public :: freqcal_period_ok, freqcal_stop, freqcal_measures, freqcal_problem, freqcal_block

  character(len=*), parameter, public :: FREQCAL_PERIOD_RULE =                &
       'trperiod must be 5, 10, 15 or 30 s in FREQCAL'
  ! The GUI's block (MainWindow::initializeFFT(6912)) and freqcal's window.
  integer, parameter :: BLOCK = 3456, WINDOW = 55296, NFSAMPLE = 12000

contains

  ! FreqCal's periods, the GUI's TR spin box in FreqCal.
  pure logical function freqcal_period_ok(trperiod)
    real(8), intent(in) :: trperiod
    freqcal_period_ok = any(trperiod .eq. [5.d0, 10.d0, 15.d0, 30.d0])
  end function freqcal_period_ok

  ! MainWindow::fixStop in FreqCal, half-symbols: 16, 32, 48 or 104.
  pure integer function freqcal_stop(trperiod)
    real(8), intent(in) :: trperiod
    freqcal_stop = (int(trperiod / 0.288d0) / 8) * 8
  end function freqcal_stop

  ! dataSink measures at period sample k: a block end whose half-symbol is a
  ! multiple of 8, above 8 and at most the stop.
  pure logical function freqcal_measures(k, trperiod)
    integer, intent(in) :: k
    real(8), intent(in) :: trperiod
    integer :: ihsym
    freqcal_measures = .false.
    if (k .lt. WINDOW .or. mod(k, BLOCK) .ne. 0) return
    ihsym = k / BLOCK
    freqcal_measures = mod(ihsym, 8) .eq. 0 .and. ihsym .gt. 8 .and.          &
         ihsym .le. freqcal_stop(trperiod)
  end function freqcal_measures

  ! An rxfreq or ntol outside the GUI's Rx Freq and F Tol ranges, as the
  ! decoder's option names its key (lib/streaming_apply.f90 engine_range_key).
  function freqcal_problem(nfqso, ntol) result(problem)
    use decoder_engine, only: option_problem
    integer, intent(in) :: nfqso, ntol
    type(option_problem) :: problem
    problem = option_problem()
    if (nfqso .lt. 200 .or. nfqso .gt. 5000) then
       problem = option_problem('receive_frequency_hz', 'from 200 to 5000 Hz')
    else if (ntol .lt. 1 .or. ntol .gt. 1000) then
       problem = option_problem('tolerance_hz', 'from 1 to 1000 Hz')
    end if
  end function freqcal_problem

  ! The measurement at period sample k, if dataSink makes one there: freqcal on
  ! id2, the period's samples, planning its FFT at patience 0 as the GUI's
  ! process does, and its freqcal line, labelled nutc in form.
  subroutine freqcal_block(id2, k, trperiod, nfqso, ntol, nutc, form)
    use four2a_patience, only: patience_override
    use streaming_emit, only: time_form, streaming_emit_label, streaming_emit_line, &
         streaming_emit_enabled
    integer(int16),  intent(in) :: id2(*)
    integer,         intent(in) :: k, nfqso, ntol, nutc
    real(8),         intent(in) :: trperiod
    type(time_form), intent(in) :: form
    character(len=80) :: line
    character(len=6)  :: label, time_text
    integer :: patience, kk, nkhz, noffset, ftol, hh, mm, ss, t
    if (.not. freqcal_measures(k, trperiod)) return
    kk = k
    ! The dial kHz labels only the GUI's line.
    nkhz = 0
    noffset = nfqso
    ftol = ntol
    line = ' '
    patience = patience_override
    patience_override = 0
    call freqcal(id2, kk, nkhz, noffset, ftol, line)
    patience_override = patience
    if (.not. streaming_emit_enabled()) return
    label = streaming_emit_label(nutc, .false., form)
    read(label, '(3i2)') hh, mm, ss
    t = mod(hh * 3600 + mm * 60 + ss + k / NFSAMPLE, 86400)
    write(time_text, '(3i2.2)') t / 3600, mod(t / 60, 60), mod(t, 60)
    ! The line: (i2.2,':',i2.2,':',i2.2,i7,i3,i6,2f10.3,2f7.1,2x,a1,a1).
    call streaming_emit_line('{"v":1,"t":"freqcal","time":"' // time_text //       &
         '","freq":' // number_(line(25:34)) // ',"error":' // number_(line(35:44)) // &
         ',"level":' // number_(line(45:51)) // ',"snr":' // number_(line(52:58)) //  &
         ',"low_snr":' // trim(merge('true ', 'false', line(61:61) .eq. '*')) // '}')
  end subroutine freqcal_block

  ! A field of the line as a JSON number, as printed, or null when it is not
  ! a finite number.
  function number_(field) result(text)
    use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
    character(len=*), intent(in) :: field
    character(len=:), allocatable :: text
    real(8) :: value
    integer :: ios
    text = 'null'
    if (len_trim(field) .eq. 0) return
    read(field, *, iostat=ios) value
    if (ios .ne. 0) return
    if (.not. ieee_is_finite(value)) return
    text = trim(adjustl(field))
    if (text(1:1) .eq. '.') then
       text = '0' // text
    else if (text(1:1) .eq. '-') then
       if (text(2:2) .eq. '.') text = '-0' // text(2:)
    end if
  end function number_

end module streaming_freqcal
