program test_map65_decode_history
  use iso_fortran_env, only: real64
  use npar_ptrs_mod, only: nfft_active, nsmax_active, nrate_active, t_start, manualDecodeFlag, &
                           active_input_generation
  use datcom_ptrs_mod, only: ss_old, savg_old
  use decodes_mod, only: nhsym1, nhsym2
  use map65a_mod, only: map65a
  use message_history_mod, only: message_record, recent_messages
  use q65_cursor_mod, only: q65_cursor_f0
  use wideband_sync, only: nkhz_center
  use decode_history_observations
  implicit none
  real(real64) :: test_fcenter = 144.0_real64
  integer, parameter :: test_nfcal = -1270
  real, allocatable :: samples(:,:)
  integer :: unit, before_jt65, before_q65
  integer :: messages, rx_log, before_display
  integer :: band_nfa = 0, band_nfb = 0, cursor_khz = 0, fits(3), test_nmode = 11
  integer :: click_bin
  real :: rec_khz
  character(len=128) :: rec
  character(len=*), parameter :: JT65_MSG = 'K1ABC W9XYZ FN42'

  ! Only the center bin has signal power; DSP stand-ins never read samples.
  nfft_active = 1024
  nsmax_active = 1
  allocate(samples(4,nsmax_active), ss_old(4,322,nfft_active), savg_old(4,nfft_active))
  samples = 0.0
  ss_old = 1.0
  savg_old = 1.0
  savg_old(1,nfft_active/2+1) = 10.0
  nhsym1 = 280
  nhsym2 = 302
  manualDecodeFlag = 0
  do unit = 12, 27
    open(unit, status='scratch')
  enddo
  close(23)

  active_input_generation = 1
  call run_pass(nhsym1, 0, 0)
  call require(jt65_attempts == 1 .and. q65_successes == 1, 'early pass decodes both signals')
  call run_pass(nhsym2, 0, 0)
  call require(jt65_attempts == 1 .and. q65_successes == 1, 'final pass retains early successes')

  jt65_attempts = 0
  q65_successes = 0
  call run_pass(nhsym2, 1, 1)
  call require(all(phase_attempts == 1), 'each JT65 trial remains eligible after an earlier success')
  call require(jt65_attempts == 13, 'JT65 runs once per trial, without replaying prior successes')
  call require(q65_successes == 1, 'unchanged Q65 input is decoded once across phase trials')
  call require(summary_count == 1 .and. fitted_phase == 90, 'later better trials determine the fitted phase')

  ! A second calibration must not inherit quality scores from the first.
  phase_has_signal = .false.
  phase_has_signal(6) = .true.
  phase_attempts = 0
  call run_pass(nhsym2, 1, 1)
  call require(all(phase_attempts == 1), 'unsuccessful trials remain eligible in a new calibration')
  call require(summary_count == 2 .and. abs(fitted_phase) == 180, 'missing trials do not retain old quality scores')

  before_jt65 = jt65_attempts
  before_q65 = q65_successes
  active_input_generation = 2
  call run_pass(nhsym1, 0, 0)
  call require(jt65_attempts == before_jt65+1 .and. q65_successes == before_q65+1, &
               'next receive period makes both frequencies eligible')
  active_input_generation = 3
  call run_pass(nhsym1, 0, 0)
  call require(jt65_attempts == before_jt65+2 .and. q65_successes == before_q65+2, &
               'next early pass resets history even without a preceding final pass')
  call run_pass(nhsym2, 0, 0)
  call require(jt65_attempts == before_jt65+2 .and. q65_successes == before_q65+2, &
               'final pass retains successes after a skipped-period boundary')

  ! A double-click decode reaches Messages/Band Map and map65_rx.log
  ! without repeating a decode already recorded by the wideband pass.
  messages = count_history_records(JT65_MSG)
  rx_log = count_records(21, JT65_MSG)
  call require(messages >= 1 .and. rx_log >= 1, 'wideband pass records the JT65 decode')
  before_jt65 = jt65_attempts
  before_display = display_calls
  call click()
  call require(jt65_attempts == before_jt65+1, 'click decodes JT65 at the cursor')
  call require(count_history_records(JT65_MSG) == messages .and. count_records(21, JT65_MSG) == rx_log, &
               'click does not repeat a decode the wideband pass recorded')
  call require(display_calls == before_display+1, 'click refreshes the Messages window')
  active_input_generation = 4
  call click()
  call require(count_history_records(JT65_MSG) == messages+1 .and. count_records(21, JT65_MSG) == rx_log+1, &
               'click records a decode the wideband pass did not')
  call click()
  call require(count_history_records(JT65_MSG) == messages+1 .and. count_records(21, JT65_MSG) == rx_log+1, &
               'second click on the same signal is not recorded again')

  ! A click on a JT65C RRR shorthand: its sync tone in the clicked bin and
  ! the upper tone 30*4 tone spacings (~3 bins) above, noise elsewhere. It
  ! must be found although the click's best JT65 sync bin is elsewhere, be
  ! labeled RRR, be recorded at the clicked kHz (band center (nfa+nfb)/2 =
  ! 130 kHz, cursor 130 kHz: 144.130 MHz), and carry no fit values left in
  ! sig by an earlier decode.
  stub_fit = 7.0
  active_input_generation = 5
  call run_pass(nhsym1, 0, 0)
  stub_fit = 0.0
  click_bin = nfft_active/2 + 1
  ss_old(2,1,:) = -99.0
  ss_old(2,1,click_bin) = 10.0
  ss_old(2,1,click_bin + nint(3*4*10.0*(11025.0/4096.0)/(96000.0/nfft_active))) = 10.0
  stub_short_from_ss = .true.
  test_nmode = 13
  band_nfa = 100
  band_nfb = 160
  cursor_khz = 130
  active_input_generation = 6
  call click()
  call require(last_history_record('RRR', rec), 'shorthand click is found and labeled RRR')
  read(rec(1:8), *) rec_khz
  read(rec(14:22), '(3i3)') fits
  call require(abs(rec_khz - 144.130) < 0.0005, 'shorthand click is recorded at the clicked kHz')
  call require(all(fits == 0), 'shorthand click records no stale fit values')

  ! A Q65 click after the center frequency moved from 144.000 to 144.125 MHz,
  ! before any automatic pass has seen the new value, must use the new
  ! fractional kHz for the cursor f0, for q65b and for the sync reference span.
  test_nmode = 11
  stub_short_from_ss = .false.
  test_fcenter = 144.125_real64
  nkhz_center = 0
  last_q65_f0 = -1.0_real64
  active_input_generation = 7
  call click()
  call require(last_q65_f0 > -1.0_real64, 'the Q65 click reaches q65b')
  call require(abs(last_q65_f0 - q65_cursor_f0(cursor_khz, 0, 125, nrate_active, test_nfcal)) < 1.0e-3_real64, &
               'a Q65 click aims at the cursor after the center frequency changed')
  call require(last_q65_nkhz == 125, 'q65b sees the kHz of the new center frequency')
  call require(last_ref_nfa == 23 .and. last_ref_nfb == 83, &
               'the sync reference span follows the new center frequency')
  print '(a)', 'MAP65 decode history tests passed.'

contains
  integer function count_history_records(text)
    character(len=*), intent(in) :: text
    type(message_record), allocatable :: records(:)
    integer :: i

    call recent_messages(huge(0), records)
    count_history_records = 0
    do i = 1, size(records)
      if (index(records(i)%line, text) > 0) count_history_records = count_history_records + 1
    enddo
  end function

  logical function last_history_record(text, found)
    character(len=*), intent(in) :: text
    character(len=*), intent(out) :: found
    type(message_record), allocatable :: records(:)
    integer :: i

    call recent_messages(huge(0), records)
    last_history_record = .false.
    found = ' '
    do i = 1, size(records)
      if (index(records(i)%line, text) == 0) cycle
      found = records(i)%line
      last_history_record = .true.
    enddo
  end function

  subroutine run_pass(half_symbols, again, find_phase)
    integer, intent(in) :: half_symbols, again, find_phase
    integer :: newdat, utc, done, ndphi, mcall3b, nsum, nsave
    newdat = 1
    utc = 1200 + int(active_input_generation)
    done = 0
    ndphi = find_phase
    mcall3b = 0
    nsum = 0
    nsave = 0
    call system_clock(t_start)
    call map65a(samples,newdat,utc,test_fcenter,100,0,band_nfa,band_nfb,0,cursor_khz, &
                again,done,0,ndphi,0, &
                test_nfcal,20,mcall3b,nsum,nsave,0,'K1ABC       ','FN42  ',0,3,0, &
                'W9XYZ       ','EN50  ',half_symbols,96000,0,1,test_nmode,0)
  end subroutine

  subroutine click()
    ! MainWindow::freezeDecode() sets nagain=1 and the manual decode flag.
    manualDecodeFlag = 1
    call run_pass(nhsym2, 1, 0)
  end subroutine

  integer function count_records(u, text)
    integer, intent(in) :: u
    character(len=*), intent(in) :: text
    character(len=128) :: rec
    integer :: ios
    count_records = 0
    rewind(u)
    do
      read(u, '(a)', iostat=ios) rec
      if (ios /= 0) exit
      if (index(rec, text) > 0) count_records = count_records + 1
    enddo
    backspace(u)
  end function

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    if (.not. condition) then
      print '(a)', 'FAIL: '//description
      error stop 1
    endif
  end subroutine
end program
