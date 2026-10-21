program test_streaming_period
  use streaming_period, only: period_state, period_begin, period_room,      &
       period_take, period_full, period_ready, period_set_stop, gui_hsym_stop
  implicit none

  integer, parameter :: NMAX = 1800 * 12000
  integer :: nfail = 0

  call sizes()
  call frames_of_128('FT8', 8, 15.d0, 180000, 50 * 3456)
  call frames_of_128('FT4', 5, 7.5d0,  90000, 21 * 3456)
  call straddle()
  call short_periods()
  call gui_stops()
  call stop_points()

  if (nfail .eq. 0) then
     print '(a)', 'ALL STREAMING PERIOD CHECKS PASSED'
  else
     print '(a,i0,a)', 'STREAMING PERIOD: ', nfail, ' CHECK(S) FAILED'
     call exit(1)
  end if

contains

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

  subroutine sizes()
    type(period_state) :: st
    call period_begin(st, 15.d0, 8, NMAX)
    call ok('FT8: period 180000, decoder 172800',                            &
         st%nperiod .eq. 180000 .and. st%npts .eq. 50 * 3456)
    call period_begin(st, 7.5d0, 5, NMAX)
    call ok('FT4: period 90000, decoder 72576',                              &
         st%nperiod .eq. 90000 .and. st%npts .eq. 21 * 3456)
    call period_begin(st, 60.d0, 9, NMAX)
    call ok('JT9: period 720000, decoder 720000',                            &
         st%nperiod .eq. 720000 .and. st%npts .eq. 720000)
    call period_begin(st, 30.d0, 8, NMAX)
    call ok('FT8 at 30 s: decoder still 172800, period 360000',              &
         st%nperiod .eq. 360000 .and. st%npts .eq. 50 * 3456)
  end subroutine sizes

  subroutine frames_of_128(name, mode, tr, nperiod, npts)
    character(len=*), intent(in) :: name
    integer,          intent(in) :: mode, nperiod, npts
    real(8),          intent(in) :: tr
    type(period_state) :: st
    integer :: left, ntake, nacc, iperiod, consumed, fed, ready_at
    logical :: good
    good = .true.
    left = 0
    do iperiod = 1, 3
       call period_begin(st, tr, mode, NMAX)
       consumed = 0; fed = 0; ready_at = -1
       do while (.not. period_full(st))
          if (left .eq. 0) left = 128
          ntake = period_room(st, left)
          call period_take(st, ntake, nacc)
          left = left - ntake
          consumed = consumed + ntake
          fed = fed + nacc
          if (ready_at .lt. 0 .and. period_ready(st)) ready_at = consumed
       end do
       ! `left` is what the frame crossing this boundary owes the next period.
       good = good .and. consumed .eq. nperiod .and. fed .eq. npts           &
            .and. ready_at .ge. npts .and. ready_at .lt. npts + 128           &
            .and. left .eq. mod(128 - mod(iperiod * nperiod, 128), 128)
    end do
    call ok(name // ': three periods of 128-sample frames — nperiod consumed,' &
         // ' npts to the decoder, ready within a frame of npts, remainder carried', good)
  end subroutine frames_of_128

  subroutine straddle()
    type(period_state) :: st
    integer :: ntake, nacc, left
    call period_begin(st, 7.5d0, 5, NMAX)
    call period_take(st, 90000 - 100, nacc)
    left = 128
    ntake = period_room(st, left)
    call ok('straddle: a 128-sample frame 100 short of the boundary yields 100', &
         ntake .eq. 100)
    call period_take(st, ntake, nacc)
    left = left - ntake
    call ok('straddle: the period is full and 28 samples remain',              &
         period_full(st) .and. left .eq. 28)
    call period_begin(st, 7.5d0, 5, NMAX)
    ntake = period_room(st, left)
    call period_take(st, ntake, nacc)
    call ok('straddle: the next period opens with those 28, all to the decoder', &
         ntake .eq. 28 .and. nacc .eq. 28 .and. st%kperiod .eq. 28 .and. st%k .eq. 28)
    call ok('straddle: nothing past the decoder share reaches it',              &
         period_room(st, 100000) .eq. 90000 - 28)
  end subroutine straddle

  subroutine short_periods()
    type(period_state) :: st
    call period_begin(st, 1.d0, 5, NMAX)
    call ok('FT4 at 1 s: period stretched to the decoder share, 72576',       &
         st%nperiod .eq. st%npts .and. st%npts .eq. 21 * 3456)
    call period_begin(st, 15.d0, 8, 100000)
    call ok('small buffer: decoder share capped at the buffer, period 180000', &
         st%npts .eq. 100000 .and. st%nperiod .eq. 180000)
    call period_begin(st, 0.d0, 9, NMAX)
    call ok('zero TRperiod: one second of decoder share, period the same',     &
         st%npts .eq. 12000 .and. st%nperiod .eq. 12000)
  end subroutine short_periods

  ! MainWindow::fixStop's table (widgets/mainwindow.cpp), mode by mode.
  subroutine gui_stops()
    integer :: start, i
    integer, parameter :: fst4_tr(7) = [15, 30, 60, 120, 300, 900, 1800]
    integer, parameter :: fst4(7)     = [39, 85, 187, 387, 1003, 3107, 6232]
    integer, parameter :: fst4_eme(7) = [48, 95, 197, 396, 1012, 3107, 6232]
    integer, parameter :: mtd(0:4) = [49, 50, 48, 49, 50]
    logical :: good
    call ok('JT4 176, 179 after EME delay',                                   &
         gui_hsym_stop(4, 60.d0, .false., .false., 3) .eq. 176 .and.          &
         gui_hsym_stop(4, 60.d0, .true., .false., 3) .eq. 179)
    call ok('JT9 173, 179 after EME delay',                                   &
         gui_hsym_stop(9, 60.d0, .false., .false., 3) .eq. 173 .and.          &
         gui_hsym_stop(9, 60.d0, .true., .false., 3) .eq. 179)
    call ok('JT65 174, 179 after EME delay',                                  &
         gui_hsym_stop(65, 60.d0, .false., .false., 3) .eq. 174 .and.         &
         gui_hsym_stop(65, 60.d0, .true., .false., 3) .eq. 179)
    call ok('Q65 15/30/60/120/300 s: 48, 96, 196, 408, 1030',                 &
         gui_hsym_stop(66, 15.d0, .false., .false., 3) .eq. 48 .and.          &
         gui_hsym_stop(66, 30.d0, .false., .false., 3) .eq. 96 .and.          &
         gui_hsym_stop(66, 60.d0, .false., .false., 3) .eq. 196 .and.         &
         gui_hsym_stop(66, 120.d0, .false., .false., 3) .eq. 408 .and.        &
         gui_hsym_stop(66, 300.d0, .false., .false., 3) .eq. 1030)
    call ok('Q65 after EME delay: 100 at 30 s, the rest unchanged',           &
         gui_hsym_stop(66, 15.d0, .true., .false., 3) .eq. 48 .and.           &
         gui_hsym_stop(66, 30.d0, .true., .false., 3) .eq. 100 .and.          &
         gui_hsym_stop(66, 60.d0, .true., .false., 3) .eq. 196 .and.          &
         gui_hsym_stop(66, 300.d0, .true., .false., 3) .eq. 1030)
    good = .true.
    do i = 1, 7
       good = good .and.                                                      &
            gui_hsym_stop(240, dble(fst4_tr(i)), .false., .false., 3) .eq. fst4(i) .and. &
            gui_hsym_stop(241, dble(fst4_tr(i)), .false., .false., 3) .eq. fst4(i) .and. &
            gui_hsym_stop(240, dble(fst4_tr(i)), .true., .false., 3) .eq. fst4_eme(i)
    end do
    call ok('FST4 and FST4W: the TR table, and the EME table after EME delay', good)
    call ok('FT4 21, FT8 50 single-threaded at any decoder start',            &
         gui_hsym_stop(5, 7.5d0, .false., .false., 3) .eq. 21 .and.           &
         gui_hsym_stop(8, 15.d0, .false., .false., 2) .eq. 50 .and.           &
         gui_hsym_stop(8, 15.d0, .true., .false., 0) .eq. 50)
    good = .true.
    do start = 0, 4
       good = good .and. gui_hsym_stop(8, 15.d0, .false., .true., start) .eq. mtd(start)
    end do
    call ok('FT8 multithreaded, decoder start 0-4: 49, 50, 48, 49, 50', good)
    call ok('MSK144 and JT65+JT9: no stop point',                            &
         gui_hsym_stop(144, 15.d0, .false., .false., 3) .eq. 0 .and.          &
         gui_hsym_stop(74, 60.d0, .false., .false., 3) .eq. 0)
  end subroutine gui_stops

  subroutine stop_points()
    type(period_state) :: st
    integer :: nacc
    call period_begin(st, 60.d0, 65, NMAX, 174)
    call ok('JT65 at stop 174: decoder 174 half-symbols, period 720000',      &
         st%npts .eq. 174 * 3456 .and. st%nperiod .eq. 720000)
    call period_begin(st, 15.d0, 8, NMAX, 48)
    call ok('FT8 at stop 48: decoder 48 half-symbols',                       &
         st%npts .eq. 48 * 3456 .and. st%nperiod .eq. 180000)
    call period_begin(st, 15.d0, 144, NMAX, 0)
    call ok('no stop point: the whole period',                               &
         st%npts .eq. 180000 .and. st%nperiod .eq. 180000)
    call period_begin(st, 15.d0, 66, NMAX, 1030)
    call ok('a stop past the period: the mode share', st%npts .eq. 180000)
    call period_begin(st, 1.d0, 5, NMAX, 21)
    call ok('FT4 at 1 s, stop 21 past it: the share, period stretched',       &
         st%npts .eq. 21 * 3456 .and. st%nperiod .eq. 21 * 3456)
    call period_begin(st, 60.d0, 65, NMAX, 174, .true.)
    call ok('whole: the period, not the stop', st%npts .eq. 720000)
    call period_begin(st, 15.d0, 8, NMAX, 50, .true.)
    call ok('whole FT8: the 15 s period', st%npts .eq. 180000)
    call period_begin(st, 60.d0, 65, NMAX, 179)
    call period_take(st, 174 * 3456, nacc)
    call ok('at 174 half-symbols, not ready for a stop at 179', .not. period_ready(st))
    call period_set_stop(st, 174, NMAX)
    call ok('the stop moved to 174: ready', period_ready(st) .and. st%npts .eq. 174 * 3456)
    call period_set_stop(st, 0, NMAX)
    call ok('a zero stop leaves the share alone', st%npts .eq. 174 * 3456)
    call period_set_stop(st, 300, NMAX)
    call ok('a stop past the period leaves the share alone', st%npts .eq. 174 * 3456)
  end subroutine stop_points

end program test_streaming_period
