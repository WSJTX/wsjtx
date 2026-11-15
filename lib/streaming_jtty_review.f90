! SPDX-License-Identifier: GPL-3.0-or-later
! JTTY review in the stream session: the audio JTTY sessions decoded, kept as
! the GUI keeps it, and a jtty_review request that decodes it again on a
! receive context of its own, as the GUI's review does
! (MainWindow::startJttyReview and the review branch of
! MainWindow::pumpJttyReceive, widgets/mainwindow_jtty_receive.cpp). Every
! routine runs on the stream's own thread, the JTTY decoder's one owner.
!
! Kept audio: the last 180 s (2,160,000 samples) of what JTTY sessions
! decoded (after equalization), every session together, oldest first out, as
! Jtty::ReceiveHistory; 4,320,000 bytes once full. A session can be reviewed
! while any of its audio is kept, across sessions and mode changes.
!
! A request, at its place in the input:
!   {"t":"jtty_review","id":I,"session":N,"sample":S,"rxfreq":F,"ntol":T,
!    "nfa":A,"nfb":B}
! No session: every kept session, oldest first, each over all its kept audio
! (the GUI's Decode button in JTTY). session alone: that session's. With
! sample, a stream sample in that session (the picked waterfall row's end):
! refused unless the session's first kept sample < S <= its last kept + 1,
! and the window runs from max(first kept, S - 60000) for up to 480,000
! samples within what is kept; the review ends early at a complete message
! within ntol of rxfreq. rxfreq, ntol, nfa and nfb default to the configured
! values. Each job's window is copied when the request is read.
!
! Lines (lib/streaming_jtty.f90 states jtty_update's fields):
!   busy        {"v":1,"t":"busy","busy":true,"mode":"JTTY","review":I} and
!               {"v":1,"t":"busy","busy":false,"review":I}, with busy_events
!   jtty_update the live line's fields plus "review":I; session is the
!               reviewed session
!   reviewed    {"v":1,"t":"reviewed","id":I,"cancelled":B}, the last line
!   rejected    {"v":1,"t":"rejected","id":I,"reason":R,"key":K,"detail":S},
!               R bad_request or unavailable
! A review runs one decoder step at a time at a frame boundary when no input
! is buffered, so live JTTY decoding never waits for more than one of its
! steps (the GUI's pump runs live steps, then input, then one review step);
! without a stdin reader thread it runs whole at once. A new request cancels
! the review in progress, as does a configure that leaves JTTY or changes
! live_audio; halt and the end of input let it finish. The decoder's alias
! references are saved and restored around every review call, so the live
! session decodes as it would without the review.

module streaming_jtty_review
  use, intrinsic :: iso_c_binding, only: c_int, c_int64_t, c_float, c_double
  use, intrinsic :: iso_fortran_env, only: int16, int64, real64
  use jtty_receive_context, only: jtty_rx_create, jtty_rx_destroy,             &
       jtty_rx_begin, jtty_rx_process, jtty_rx_end
  use jtty_mdec, only: UPDATE_RECEPTION_ENDED, nalias_refs, next_alias_ref,     &
       alias_ref_f1, alias_ref_tsync, alias_ref_tones, MAX_ALIAS_REFERENCES,    &
       ALIAS_FRAME_SYMBOLS
  use streaming_apply, only: params_block
  use streaming_control, only: review_request, parse_review_request
  use streaming_emit, only: streaming_emit_line, streaming_emit_error_code
  use streaming_jtty, only: jtty_emit_updates, jtty_step_done,                &
       jtty_current_session, encode_emit_rejected, jtty_fatal
  use streaming_decode, only: decode_loop_events, decode_idle_point
  implicit none
  private

  public :: jtty_review_keep, jtty_review_request, jtty_review_idle,          &
       jtty_review_cancel, jtty_review_finish, jtty_review_release,          &
       jtty_review_live_audio

  integer, parameter :: NSPS = 384
  integer, parameter :: HISTORY = 180 * 12000          ! Jtty::ReceiveHistory::capacity
  integer, parameter :: BEFORE = 5 * 12000, SPAN = 40 * 12000

  ! The kept audio: sample number t (from 0, counting every sample kept) at
  ! ring_(mod(t, HISTORY) + 1); one run per stretch of a session.
  type :: run_t
     integer        :: session = 0
     integer(int64) :: origin = 0, first = 0, t = 0, count = 0
  end type run_t
  integer(int16), allocatable, save :: ring_(:)
  integer(int64), save :: total_ = 0
  type(run_t), allocatable, save :: runs_(:)
  integer, save :: nruns_ = 0

  type :: job_t
     integer        :: session = 0
     integer(int64) :: origin = 0, first = 0, end = 0
     integer(int16), allocatable :: pcm(:)
  end type job_t
  type(job_t), allocatable, save :: jobs_(:)
  integer, save :: njobs_ = 0, job_ = 0
  logical, save :: active_ = .false., begun_ = .false., picked_ = .false.
  logical, save :: shown_ = .false.
  integer, save :: id_ = 0, rxfreq_ = 0, ntol_ = 0, nfa_ = 0, nfb_ = 0
  integer(c_int), save :: handle_ = 0
  ! With live_audio false, a review of every session covers those after
  ! session file_from_, the last begun when live_audio last went false.
  integer, save :: file_from_ = 0

  ! The review's alias references while the live session's are in place.
  integer, save :: nrefs_ = 0, next_ref_ = 1
  real, save :: ref_f1_(MAX_ALIAS_REFERENCES) = 0.0
  real(real64), save :: ref_tsync_(MAX_ALIAS_REFERENCES) = 0.0_real64
  integer, save :: ref_tones_(ALIAS_FRAME_SYMBOLS, MAX_ALIAS_REFERENCES) = 0

  interface
     ! lib/stream_stdin.c: whether a reader thread drains stdin.
     function stream_stdin_threaded() result(yes) bind(C, name="stream_stdin_threaded")
       import :: c_int
       integer(c_int) :: yes
     end function stream_stdin_threaded
  end interface

contains

  ! The samples the open JTTY session just decoded, from stream sample first.
  subroutine jtty_review_keep(samples, first)
    integer(int16), intent(in) :: samples(:)
    integer(int64), intent(in) :: first
    integer :: session, i, n
    integer(int64) :: origin, slot
    n = size(samples)
    call jtty_current_session(session, origin)
    if (n .eq. 0 .or. session .eq. 0) return
    if (.not. allocated(ring_)) then
       allocate(ring_(HISTORY), runs_(16))
    end if
    if (nruns_ .gt. 0) then
       if (runs_(nruns_)%session .ne. session .or.                            &
            runs_(nruns_)%first + runs_(nruns_)%count .ne. first) call new_run_()
    else
       call new_run_()
    end if
    do i = 1, n
       slot = mod(total_ + i - 1, int(HISTORY, int64)) + 1
       ring_(slot) = samples(i)
    end do
    total_ = total_ + n
    runs_(nruns_)%count = runs_(nruns_)%count + n
    ! Forget what is no longer kept.
    do while (nruns_ .gt. 0)
       if (runs_(1)%t + runs_(1)%count .gt. total_ - HISTORY) exit
       runs_(1:nruns_ - 1) = runs_(2:nruns_)
       nruns_ = nruns_ - 1
    end do

  contains

    subroutine new_run_()
      type(run_t), allocatable :: grown(:)
      if (nruns_ .eq. size(runs_)) then
         allocate(grown(2 * size(runs_)))
         grown(1:nruns_) = runs_(1:nruns_)
         call move_alloc(grown, runs_)
      end if
      nruns_ = nruns_ + 1
      runs_(nruns_) = run_t(session, origin, first, total_, 0_int64)
    end subroutine new_run_

  end subroutine jtty_review_keep

  ! The first kept sample of session's audio and the sample after its last;
  ! first -1 when none is kept. origin: the session's first sample.
  subroutine kept_(session, first, end, origin)
    integer,        intent(in)  :: session
    integer(int64), intent(out) :: first, end, origin
    integer :: r
    first = -1
    end = -1
    origin = 0
    do r = 1, nruns_
       if (runs_(r)%session .ne. session) cycle
       if (first .lt. 0) then
          first = runs_(r)%first + max(0_int64, total_ - HISTORY - runs_(r)%t)
          origin = runs_(r)%origin
       end if
       end = runs_(r)%first + runs_(r)%count
    end do
  end subroutine kept_

  ! session's kept samples [first, end) into pcm; ok .false. unless all are
  ! kept in one run.
  subroutine copy_(session, first, end, pcm, ok)
    integer,                     intent(in)  :: session
    integer(int64),              intent(in)  :: first, end
    integer(int16), allocatable, intent(out) :: pcm(:)
    logical,                     intent(out) :: ok
    integer :: r
    integer(int64) :: s, t
    ok = .false.
    do r = 1, nruns_
       if (runs_(r)%session .ne. session) cycle
       if (first .lt. runs_(r)%first + max(0_int64, total_ - HISTORY - runs_(r)%t)) cycle
       if (end .gt. runs_(r)%first + runs_(r)%count) cycle
       allocate(pcm(end - first))
       do s = first, end - 1
          t = runs_(r)%t + (s - runs_(r)%first)
          pcm(s - first + 1) = ring_(mod(t, int(HISTORY, int64)) + 1)
       end do
       ok = .true.
       return
    end do
  end subroutine copy_

  ! A configure changed live_audio to live: the review in progress ends, as
  ! WSJT-X's opening a file cancels it (beginJttyDisk); from live to file, a
  ! review of every session then covers the sessions begun after this.
  subroutine jtty_review_live_audio(live)
    logical, intent(in) :: live
    integer :: session
    integer(int64) :: origin
    call jtty_review_cancel()
    if (.not. live) call jtty_current_session(session, origin, file_from_)
  end subroutine jtty_review_live_audio

  ! Read a jtty_review request; jtty: the stream is in JTTY; live: its
  ! live_audio; params: the configured settings.
  subroutine jtty_review_request(frame, jtty, live, params)
    character(len=*),   intent(in) :: frame
    logical,            intent(in) :: jtty, live
    type(params_block), intent(in) :: params
    type(review_request) :: req
    type(job_t), allocatable :: jobs(:)
    integer(int64) :: first, end, origin, start
    character(len=:), allocatable :: id
    integer :: r, n
    logical :: ok, listed

    call parse_review_request(frame, req)
    if (.not. req%id_ok) then
       call streaming_emit_error_code('invalid_request_id',                   &
            'jtty_review needs an integer id from -2147483648 to 2147483647')
       return
    end if
    id = itoa_(int(req%id, int64))
    ! Every request ends the review in progress first, even one it then
    ! refuses, as every Decode press in WSJT-X does (startJttyReview calls
    ! cancelJttyReview before anything else).
    call jtty_review_cancel()
    if (len(req%problem) .gt. 0) then
       call encode_emit_rejected(id, 'bad_request', req%problem_key, req%problem)
       return
    end if
    if (.not. jtty) then
       call encode_emit_rejected(id, 'unavailable', 'mode', 'a review needs the stream in JTTY')
       return
    end if

    allocate(jobs(max(1, nruns_)))
    n = 0
    if (req%session .gt. 0) then
       call kept_(req%session, first, end, origin)
       if (first .lt. 0) then
          call encode_emit_rejected(id, 'unavailable', 'session', 'no audio of that session is kept')
          return
       end if
       if (req%picked) then
          if (req%sample .le. first .or. req%sample .gt. end) then
             call encode_emit_rejected(id, 'unavailable', 'sample',                  &
                  'the audio at that sample is no longer kept')
             return
          end if
          start = max(first, req%sample - BEFORE)
          end = min(end, start + SPAN)
          first = start
       end if
       call add_(req%session, origin, first, end)
    else
       ! From a file, as WSJT-X's Decode reviews the loaded recording only:
       ! the sessions since live_audio last went false.
       do r = 1, nruns_
          listed = any(jobs(1:n)%session .eq. runs_(r)%session)
          if (listed .or. (.not. live .and. runs_(r)%session .le. file_from_)) cycle
          call kept_(runs_(r)%session, first, end, origin)
          call add_(runs_(r)%session, origin, first, end)
       end do
    end if

    call move_alloc(jobs, jobs_)
    njobs_ = n
    job_ = 1
    begun_ = .false.
    picked_ = req%picked
    id_ = req%id
    rxfreq_ = merge(req%rxfreq, params%nfqso, req%rxfreq .ge. 0)
    ntol_ = merge(req%ntol, params%ntol, req%ntol .ge. 0)
    nfa_ = merge(req%nfa, params%nfa, req%nfa .ge. 0)
    nfb_ = merge(req%nfb, params%nfb, req%nfb .ge. 0)
    active_ = .true.
    shown_ = decode_loop_events()
    if (shown_) call streaming_emit_line('{"v":1,"t":"busy","busy":true,"mode":"JTTY","review":' // &
         itoa_(int(id_, int64)) // '}')
    if (njobs_ .eq. 0) call done_(.false.)

  contains

    subroutine add_(session, origin, first, end)
      integer,        intent(in) :: session
      integer(int64), intent(in) :: origin, first, end
      if (end .le. first) return
      n = n + 1
      jobs(n)%session = session
      jobs(n)%origin = origin
      jobs(n)%first = first
      jobs(n)%end = end
      call copy_(session, first, end, jobs(n)%pcm, ok)
      if (.not. ok) n = n - 1
    end subroutine add_

  end subroutine jtty_review_request

  ! Review steps while no input waits: at a frame boundary, in JTTY. Without
  ! a stdin reader thread nothing shows input waiting, and a read would block
  ! the review behind the client: the whole review then runs at once, input
  ! waiting for it.
  subroutine jtty_review_idle()
    if (stream_stdin_threaded() .eq. 0) then
       call jtty_review_finish()
       return
    end if
    do while (active_)
       if (.not. decode_idle_point()) return
       call step_()
    end do
  end subroutine jtty_review_idle

  ! At halt and the end of input: the review in progress to its end.
  subroutine jtty_review_finish()
    do while (active_)
       call step_()
    end do
  end subroutine jtty_review_finish

  ! One decoder step of the review in progress, as the GUI's pump runs one.
  subroutine step_()
    integer(c_int) :: steps
    logical :: found
    associate (job => jobs_(job_))
      if (.not. begun_) then
         ! Each job starts with no alias references, as a fresh decoder.
         nrefs_ = 0
         next_ref_ = 1
         call swap_refs_()
         call jtty_rx_begin(handle_for_(), int(job%session, c_int64_t), job%origin, &
              job%first, NSPS)
         call swap_refs_()
         begun_ = .true.
      end if
      ! FFT plans are shared with the live session's by size and array
      ! (four2a), so the review plans at the live session's patience.
      call swap_refs_()
      steps = jtty_rx_process(handle_, job%pcm, int(size(job%pcm), c_int), job%first, &
           job%end, 1, nfa_, nfb_, real(rxfreq_, c_float), real(ntol_, c_float))
      call swap_refs_()
      call jtty_step_done()
      call jtty_emit_updates(handle_, job%session, id_, real(rxfreq_), real(ntol_), found)
      if (steps .gt. 0 .and. .not. (picked_ .and. found)) return
      call swap_refs_()
      call jtty_rx_end(handle_, UPDATE_RECEPTION_ENDED)
      call swap_refs_()
      call jtty_emit_updates(handle_, job%session, id_)
    end associate
    begun_ = .false.
    job_ = job_ + 1
    if (job_ .gt. njobs_) call done_(.false.)
  end subroutine step_

  ! A review in progress ends here, its last updates dropped, as
  ! cancelJttyReview drops them.
  subroutine jtty_review_cancel()
    if (.not. active_) return
    if (begun_) then
       call swap_refs_()
       call jtty_rx_end(handle_, UPDATE_RECEPTION_ENDED)
       call swap_refs_()
       call drop_updates_()
    end if
    call done_(.true.)
  end subroutine jtty_review_cancel

  subroutine jtty_review_release()
    if (handle_ .ne. 0) call jtty_rx_destroy(handle_)
    handle_ = 0
    if (allocated(jobs_)) deallocate(jobs_)
    if (allocated(ring_)) deallocate(ring_)
    if (allocated(runs_)) deallocate(runs_)
    nruns_ = 0
  end subroutine jtty_review_release

  subroutine done_(cancelled)
    logical, intent(in) :: cancelled
    if (shown_) call streaming_emit_line('{"v":1,"t":"busy","busy":false,"review":' // &
         itoa_(int(id_, int64)) // '}')
    call streaming_emit_line('{"v":1,"t":"reviewed","id":' // itoa_(int(id_, int64)) // &
         ',"cancelled":' // trim(merge('true ', 'false', cancelled)) // '}')
    active_ = .false.
    begun_ = .false.
    shown_ = .false.
    njobs_ = 0
    if (allocated(jobs_)) deallocate(jobs_)
  end subroutine done_

  ! The review's own receive context, made at its first use.
  integer(c_int) function handle_for_()
    if (handle_ .eq. 0) handle_ = jtty_rx_create()
    if (handle_ .eq. 0) call jtty_fatal('no JTTY receive context is available for a review')
    handle_for_ = handle_
  end function handle_for_

  ! The updates a cancelled review's context still holds, taken and dropped.
  subroutine drop_updates_()
    use jtty_receive_context, only: jtty_rx_take_updates
    use, intrinsic :: iso_c_binding, only: c_char
    character(kind=c_char) :: text(80 * 30)
    integer(c_int64_t) :: ids(30)
    real(c_float) :: frequencies(30)
    real(c_double) :: starts(30), latest(30)
    integer(c_int) :: terminals(30), snrs(30)
    do while (jtty_rx_take_updates(handle_, 30, text, ids, frequencies, starts, &
         latest, terminals, snrs) .eq. 30)
    end do
  end subroutine drop_updates_

  ! Exchange the decoder's alias references with the review's: the receive
  ! contexts do not hold them, and jtty_rx_begin and jtty_rx_end reset them.
  subroutine swap_refs_()
    integer :: n, next, tones(ALIAS_FRAME_SYMBOLS, MAX_ALIAS_REFERENCES)
    real :: f1(MAX_ALIAS_REFERENCES)
    real(real64) :: tsync(MAX_ALIAS_REFERENCES)
    n = nalias_refs
    next = next_alias_ref
    f1 = alias_ref_f1
    tsync = alias_ref_tsync
    tones = alias_ref_tones
    nalias_refs = nrefs_
    next_alias_ref = next_ref_
    alias_ref_f1 = ref_f1_
    alias_ref_tsync = ref_tsync_
    alias_ref_tones = ref_tones_
    nrefs_ = n
    next_ref_ = next
    ref_f1_ = f1
    ref_tsync_ = tsync
    ref_tones_ = tones
  end subroutine swap_refs_

  function itoa_(value) result(text)
    integer(int64), intent(in) :: value
    character(len=:), allocatable :: text
    character(len=24) :: buffer
    write(buffer, '(i0)') value
    text = trim(buffer)
  end function itoa_

end module streaming_jtty_review
