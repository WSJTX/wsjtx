! Period bookkeeping for the streaming reader. A period is TRperiod * 12 kHz
! samples; the decoder takes the first npts of them; a frame that straddles
! the boundary carries its remainder into the next period.
module streaming_period
  use, intrinsic :: iso_fortran_env, only: int64
  implicit none
  private
  public :: period_state, period_begin, period_room, period_take,          &
       period_full, period_ready, period_set_stop, gui_hsym_stop
  public :: sample_due, trigger_fire_time, read_passes_busy_end, clock_start

  ! The GUI's half-symbol: its detector hands symspec this many samples a call.
  integer, parameter, public :: HSYM_SAMPLES = 3456

  integer, parameter, public :: PERIOD_NFSAMPLE = 12000

  type period_state
     integer :: nperiod = 0   ! samples in the period
     integer :: npts    = 0   ! of them, the samples the decoder takes
     integer :: kperiod = 0   ! consumed so far this period
     integer :: k       = 0   ! handed to the decoder so far
  end type period_state

contains

  ! Begin a period of trperiod seconds in jt9 mode number `mode`, with a
  ! decoder buffer of nmax samples. The decoder's share is mode-dependent
  ! (21*3456 for FT4, at most 50*3456 for FT8); nstop half-symbols that fit
  ! in the period replace it, and whole takes the whole period. The period is
  ! never shorter than the share.
  subroutine period_begin(st, trperiod, mode, nmax, nstop, whole)
    type(period_state), intent(out) :: st
    real(8),            intent(in)  :: trperiod
    integer,            intent(in)  :: mode, nmax
    integer, optional,  intent(in)  :: nstop
    logical, optional,  intent(in)  :: whole
    st%npts = int(trperiod * PERIOD_NFSAMPLE)
    if (mode .eq. 5) st%npts = 21 * 3456
    if (mode .eq. 8 .and. st%npts .gt. 50 * 3456) st%npts = 50 * 3456
    if (present(nstop)) then
       if (nstop .gt. 0 .and. nstop * HSYM_SAMPLES .le. int(trperiod * PERIOD_NFSAMPLE)) &
            st%npts = nstop * HSYM_SAMPLES
    end if
    if (present(whole)) then
       if (whole .and. trperiod .gt. 0.d0) st%npts = int(trperiod * PERIOD_NFSAMPLE)
    end if
    if (st%npts .gt. nmax) st%npts = nmax
    if (st%npts .lt. 1) st%npts = PERIOD_NFSAMPLE
    st%nperiod = max(int(trperiod * PERIOD_NFSAMPLE), st%npts)
    st%kperiod = 0
    st%k       = 0
  end subroutine period_begin

  ! Move the decoder's share of the period in flight to nstop half-symbols
  ! when they fit in the period; samples already past a lower share stay
  ! discarded.
  subroutine period_set_stop(st, nstop, nmax)
    type(period_state), intent(inout) :: st
    integer,            intent(in)    :: nstop, nmax
    if (nstop .le. 0 .or. nstop * HSYM_SAMPLES .gt. st%nperiod) return
    st%npts = max(1, min(nstop * HSYM_SAMPLES, nmax))
  end subroutine period_set_stop

  ! The half-symbol at which the GUI decodes a period of jt9 mode `mode`
  ! (MainWindow::fixStop, widgets/mainwindow.cpp), with "Decode after EME
  ! delay" and the multithreaded FT8 decoder start. 0 for MSK144, which the
  ! GUI decodes as audio arrives instead, and for JT65+JT9, which it lacks.
  pure integer function gui_hsym_stop(mode, trperiod, eme_delay, multithreaded, decoder_start)
    integer, intent(in) :: mode, decoder_start
    real(8), intent(in) :: trperiod
    logical, intent(in) :: eme_delay, multithreaded
    integer, parameter :: fst4(0:6)     = [39, 85, 187, 387, 1003, 3107, 6232]
    integer, parameter :: fst4_eme(0:6) = [48, 95, 197, 396, 1012, 3107, 6232]
    integer :: i
    gui_hsym_stop = 179
    select case (mode)
    case (4)
       gui_hsym_stop = merge(179, 176, eme_delay)
    case (9)
       gui_hsym_stop = merge(179, 173, eme_delay)
    case (65)
       gui_hsym_stop = merge(179, 174, eme_delay)
    case (66)
       gui_hsym_stop = 48
       if (trperiod .eq. 30.d0) gui_hsym_stop = merge(100, 96, eme_delay)
       if (trperiod .eq. 60.d0) gui_hsym_stop = 196
       if (trperiod .eq. 120.d0) gui_hsym_stop = 408
       if (trperiod .eq. 300.d0) gui_hsym_stop = 1030
    case (8)
       gui_hsym_stop = 50
       if (multithreaded) then
          if (decoder_start .eq. 0 .or. decoder_start .eq. 3) gui_hsym_stop = 49
          if (decoder_start .eq. 2) gui_hsym_stop = 48
       end if
    case (5)
       gui_hsym_stop = 21
    case (240, 241, 242)
       i = 0
       if (trperiod .eq. 30.d0) i = 1
       if (trperiod .eq. 60.d0) i = 2
       if (trperiod .eq. 120.d0) i = 3
       if (trperiod .eq. 300.d0) i = 4
       if (trperiod .eq. 900.d0) i = 5
       if (trperiod .eq. 1800.d0) i = 6
       gui_hsym_stop = merge(fst4_eme(i), fst4(i), eme_delay)
    case (74, 144)
       gui_hsym_stop = 0
    end select
  end function gui_hsym_stop

  ! Of navail samples on offer, how many the period takes — never past its
  ! end, so what is left of a straddling frame is the next period's.
  pure integer function period_room(st, navail)
    type(period_state), intent(in) :: st
    integer,            intent(in) :: navail
    period_room = max(0, min(navail, st%nperiod - st%kperiod))
  end function period_room

  ! Account for ntake samples consumed into the period; the first nacc of
  ! them are the decoder's.
  subroutine period_take(st, ntake, nacc)
    type(period_state), intent(inout) :: st
    integer,            intent(in)    :: ntake
    integer,            intent(out)   :: nacc
    nacc = max(0, min(ntake, st%npts - st%k))
    st%kperiod = st%kperiod + ntake
    st%k       = st%k + nacc
  end subroutine period_take

  ! When stream sample k (from 0) is due, live audio being due from t0 on.
  pure real(8) function sample_due(t0, k)
    real(8),        intent(in) :: t0
    integer(int64), intent(in) :: k
    sample_due = t0 + dble(k) / PERIOD_NFSAMPLE
  end function sample_due

  ! When a decode trigger fires: when its last byte arrived, or when its last
  ! sample k is due if that is later (k < 0: no audio clock).
  pure real(8) function trigger_fire_time(arrival, t0, k)
    real(8),        intent(in) :: arrival, t0
    integer(int64), intent(in) :: k
    trigger_fire_time = arrival
    if (k .ge. 0) trigger_fire_time = max(arrival, sample_due(t0, k))
  end function trigger_fire_time

  ! The audio clock's start t0 after a read whose last sample, k, arrived at
  ! arrival, the stream having waited `wait` seconds for input since the last
  ! audio: moved earlier toward the start at which that sample is due on
  ! arrival, by no more than the wait; never later.
  pure real(8) function clock_start(t0, arrival, k, wait)
    real(8),        intent(in) :: t0, arrival, wait
    integer(int64), intent(in) :: k
    clock_start = max(min(t0, arrival - dble(k) / PERIOD_NFSAMPLE), t0 - wait)
  end function clock_start

  ! Whether a read of nbytes, bytes_before of which arrived before busy_end and
  ! whose last audio sample is k_last (< 0: none, or no audio clock), takes in
  ! anything that came after the decoder came free at busy_end.
  pure logical function read_passes_busy_end(bytes_before, nbytes, t0, k_last, busy_end)
    integer(int64), intent(in) :: bytes_before, nbytes, k_last
    real(8),        intent(in) :: t0, busy_end
    read_passes_busy_end = bytes_before .lt. nbytes
    if (k_last .ge. 0) read_passes_busy_end = read_passes_busy_end .or.       &
         sample_due(t0, k_last) .ge. busy_end
  end function read_passes_busy_end

  ! Every sample of the period is in.
  pure logical function period_full(st)
    type(period_state), intent(in) :: st
    period_full = st%kperiod .ge. st%nperiod
  end function period_full

  ! The decoder's samples are in.
  pure logical function period_ready(st)
    type(period_state), intent(in) :: st
    period_ready = st%k .ge. st%npts
  end function period_ready

end module streaming_period
