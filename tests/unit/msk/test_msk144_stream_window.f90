! SPDX-License-Identifier: GPL-3.0-or-later
! msk144_window is production code; only its decoder, mskrtd, is replaced by
! a recording probe, so each row checks what reaches the decoder.
module msk144_window_probe
  implicit none
  integer :: calls = 0
  integer*2 :: received(7168)
  integer :: got_nutc, got_ntol, got_rxfreq, got_depth, got_patience
  real :: got_tsec
  real(8) :: got_pcoeffs(5)
  logical :: got_shmsg, got_train, got_swl
  character(len=12) :: got_mycall, got_hiscall
  character(len=:), allocatable :: got_datadir
  ! The probe's call ends training as msk144signalquality does on success.
  logical :: finish_training = .false.
end module msk144_window_probe

program test_msk144_stream_window
  use msk144_window_probe
  use msk144_stream, only: msk144_settings, msk144_window, msk144_line_fields
  implicit none
  integer, parameter :: NPERIOD = 30 * 12000 + 2 * 7168
  integer*2 :: id2(NPERIOD)
  type(msk144_settings) :: settings
  character(len=500) :: data_dir
  integer :: i, k, nfail, last
  logical :: good
  integer :: npatience, nthreads
  common/patience/npatience,nthreads

  nfail = 0
  do i = 1, NPERIOD
     id2(i) = int(1 + mod(i, 30000), kind=2)
  end do
  data_dir = '/data/dir'

  ! Every sample of a 15 s period: one window per block end from 7168 on.
  good = .true.
  last = 0
  do k = 1, 15 * 12000
     call window(k, 3)
     if (calls .ne. last) then
        good = good .and. mod(k, 3584) .eq. 0 .and. k .ge. 7168 .and.         &
             all(received .eq. id2(k-7167:k)) .and. got_tsec .eq. (k - 7168) / 12000.0
        last = calls
     end if
  end do
  call ok('a 15 s period: 49 windows, [k-7168,k) at block ends k from 7168, tsec (k-7168)/12000', &
       good .and. calls .eq. 49)

  calls = 0
  call window(358400, 3)
  call ok('a window ending at 358400 reaches the decoder', calls .eq. 1)
  calls = 0
  call window(361984, 3)
  call ok('no window ending past 30 s reaches it', calls .eq. 0)

  ! Either half of the window all zeros: hspec's skip.
  calls = 0
  id2(1:3584) = 0
  call window(7168, 3)
  call ok('a window whose first half is zeros is skipped', calls .eq. 0)
  id2(1) = -1
  call window(7168, 3)
  call ok('one sample in it is enough', calls .eq. 1)
  calls = 0
  id2(3585:7168) = 0
  call window(7168, 3)
  call ok('a window whose second half is zeros is skipped', calls .eq. 0)
  id2(7168) = 1
  call window(7168, 3)
  call ok('one sample in that half too', calls .eq. 1)

  call window(10752, 5)
  call ok('depth 5 decodes at depth 1', got_depth .eq. 1)
  call window(10752, 7)
  call ok('depth 7 at depth 3', got_depth .eq. 3)
  call window(10752, 17)
  call ok('depth 17 at depth 1', got_depth .eq. 1)
  call window(10752, 2)
  call ok('depth 2 at depth 2', got_depth .eq. 2)

  settings%short_messages = .true.
  settings%swl = .false.
  settings%train = .true.
  settings%phase_eq = [1.d0, 2.d0, 3.d0, 4.d0, 5.d0]
  settings%mycall = 'KJ5HST'
  settings%hiscall = 'W1AW/7'
  call window(14336, 3)
  call ok('the options, calls, period time, tolerance and frequency reach the decoder', &
       got_shmsg .and. .not. got_swl .and. got_train .and.                    &
       all(got_pcoeffs .eq. [1.d0, 2.d0, 3.d0, 4.d0, 5.d0]) .and.              &
       got_mycall .eq. 'KJ5HST' .and. got_hiscall .eq. 'W1AW/7' .and.          &
       got_nutc .eq. 120500 .and. got_ntol .eq. 50 .and. got_rxfreq .eq. 1490)
  settings%short_messages = .false.
  settings%swl = .true.
  settings%train = .false.
  call window(14336, 3)
  call ok('each to its own argument', .not. got_shmsg .and. got_swl .and. .not. got_train)
  call ok('the data directory without its trailing blanks',                  &
       got_datadir .eq. '/data/dir' .and. len(got_datadir) .eq. 9)

  npatience = 1
  call window(14336, 3)
  call ok('the decoder plans its FFTs at patience 0, and the patience comes back', &
       got_patience .eq. 0 .and. npatience .eq. 1)

  settings%train = .true.
  call window(17920, 3)
  call ok('train stays set while training goes on', settings%train)
  finish_training = .true.
  call window(21504, 3)
  finish_training = .false.
  call ok('training that succeeds clears train', .not. settings%train)

  call fields('120500  -3 12.3 1496 &  ' // repeat('M', 36) // 'Z' // achar(0), &
       .true., 120500, -3, 12.3, 1496, repeat('M', 36) // 'Z', .false.,       &
       'a 37-character message, whole')
  call fields('120500  24  0.1 1500 ^  K1ABC W9XYZ R-04' // repeat(' ', 21) // achar(0), &
       .true., 120500, 24, 0.1, 1500, 'K1ABC W9XYZ R-04', .true.,              &
       'a decode made while training is marked')
  call fields('1205xx   8  8.7 1488 &  K1JT WA4CQG EM72', .false., 0, 0, 0.0, 0, '', &
       .false., 'a line that does not parse is no decode')

  if (nfail .eq. 0) then
     print '(a)', 'ALL MSK144 STREAM WINDOW CHECKS PASSED'
  else
     print '(a,i0,a)', 'MSK144 STREAM WINDOW: ', nfail, ' CHECK(S) FAILED'
     call exit(1)
  end if

contains

  subroutine window(k, ndepth)
    integer, intent(in) :: k, ndepth
    call msk144_window(id2, k, 120500, 50, 1490, ndepth, settings, data_dir)
  end subroutine window

  subroutine fields(line, expect_ok, utc, snr, dt, freq, message, training, label)
    character(len=*), intent(in) :: line, message, label
    logical,          intent(in) :: expect_ok, training
    integer,          intent(in) :: utc, snr, freq
    real,             intent(in) :: dt
    integer :: got_utc, got_snr, got_freq
    real :: got_dt
    character(len=37) :: got_message
    logical :: got_training, got_ok
    character(len=80) :: padded
    padded = line
    call msk144_line_fields(padded, got_utc, got_snr, got_dt, got_freq, got_message, &
         got_training, got_ok)
    if (expect_ok) then
       call ok(label, got_ok .and. got_utc .eq. utc .and. got_snr .eq. snr .and. &
            abs(got_dt - dt) .lt. 1e-4 .and. got_freq .eq. freq .and.          &
            got_message .eq. message .and. (got_training .eqv. training))
    else
       call ok(label, .not. got_ok)
    end if
  end subroutine fields

  subroutine ok(label, cond)
    character(len=*), intent(in) :: label
    logical,          intent(in) :: cond
    if (cond) then
       print '(a,a)', '  PASS  ', label
    else
       print '(a,a)', '  FAIL  ', label
       nfail = nfail + 1
    end if
  end subroutine ok

end program test_msk144_stream_window

subroutine mskrtd(id2,nutc0,tsec,ntol,nrxfreq,ndepth,mycall,hiscall, &
     bshmsg,btrain,pcoeffs,bswl,datadir,line)
  use msk144_window_probe
  use msk144_training, only: trainings_done
  implicit none
  integer*2 :: id2(7168)
  integer :: nutc0, ntol, nrxfreq, ndepth
  real :: tsec
  real*8 :: pcoeffs(5)
  logical*1 :: bshmsg,btrain,bswl
  character*(*) :: mycall,hiscall,datadir,line
  integer :: npatience, nthreads
  common/patience/npatience,nthreads
  received = id2
  got_patience = npatience
  got_nutc = nutc0
  got_tsec = tsec
  got_ntol = ntol
  got_rxfreq = nrxfreq
  got_depth = ndepth
  got_mycall = mycall
  got_hiscall = hiscall
  got_shmsg = bshmsg
  got_train = btrain
  got_swl = bswl
  got_pcoeffs = pcoeffs
  got_datadir = datadir
  calls = calls + 1
  if (finish_training) then
     btrain = .false.
     trainings_done = trainings_done + 1
  end if
  line = char(0)
end subroutine mskrtd
