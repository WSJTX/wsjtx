! SPDX-License-Identifier: GPL-3.0-or-later
! The stream's decode side: every period-mode decode pass, the deferred final
! and decode-again, and every decision about them (busy, skip, defer,
! replace, cancel, drain). lib/streaming_io.f90's frame loop makes an
! operation at each decode trigger, accepted configure, decode-again
! request, discontinuity, flush, halt and end of input, and for each line it
! routes behind decode work; decode_handle runs them one at a time, in stream
! order, on the decode thread (lib/stream_decode.c) or, without one, inline
! as each is made. The decoder works on this side's own buffer, never on the
! frame loop's.
!
! Routing: the decode side has work while an operation other than a
! configure is queued or running, a final waits, an early pass's completion
! is due, decode-again requests are held, or a busy line is still to end.
! While it has work, a line the frame loop writes in a period mode (a
! configure's or a frame's error, a marker's answer) is queued behind it, so
! it keeps its place among the decode lines; otherwise, and in MSK144 and
! JTTY, which decode on the frame loop's thread, the frame loop writes it.
! Replies to pack and render are never queued. A configure that turns
! busy_events on or off is work too.
!
! With busy_events (lib/streaming_emit.f90 states the lines), each pass that
! runs writes busy true as it starts and each final deferred writes it too;
! busy turns false where the decoder frees with nothing queued, or no pass
! next, and nothing more to come from before it freed, and at the end.
! Each pass decodes under a fresh progress generation, so the decoders'
! progress reports (the GUI's worker's, lib/decode_completion.f90) become its
! pass_progress lines.
module streaming_decode
  use, intrinsic :: iso_fortran_env, only: int16, int64, error_unit
  use, intrinsic :: iso_c_binding, only: c_double, c_ptr, c_loc, c_f_pointer, &
       c_funloc, c_funptr
  use streaming_emit, only: time_form, streaming_emit_set_form,             &
       streaming_emit_decode_finished, streaming_emit_error_code,          &
       streaming_emit_hold_finished, streaming_emit_pass_skipped,          &
       streaming_emit_finished_at, streaming_emit_set_pass,                &
       streaming_emit_hold, streaming_emit_line, streaming_emit_threads,   &
       streaming_emit_set_writer, streaming_emit_label, streaming_emit_busy, &
       streaming_emit_idle, streaming_emit_progress, streaming_emit_error
  use decode_completion_module, only: set_progress_receiver
  use jtty_mdec, only: jtty_set_planning_callback
  use streaming_jtty, only: jtty_measuring
  use streaming_period, only: trigger_fire_time, read_passes_busy_end,     &
       HSYM_SAMPLES
  use streaming_apply, only: ft8_identity_change
  use decoder_engine, only: engine_begin_input

  include 'jt9com.f90'

  private
  public :: pass_audio, decode_op, decode_handle, decode_drain, snapshot
  public :: superfox_even, superfox_hound, backpressure, hhmm_label
  public :: report_pass_line
  public :: decode_start, decode_threaded, decode_submit, decode_join
  public :: decode_read, decode_room, decode_wait, decode_has_work

  ! Operations.
  integer, parameter, public :: OP_PASS = 1, OP_CONFIGURE = 2, OP_AGAIN = 3, &
       OP_DISCONTINUITY = 4, OP_FREE = 5, OP_END = 6, OP_LINE = 7, OP_STOP = 8
  ! lib/stream_ft8_backpressure.cpp's stages and decisions.
  integer, parameter, public :: EARLY_ONE = 1, EARLY_TWO = 2, FINAL_STAGE = 3
  integer, parameter :: ACTION_PUBLISH = 0, ACTION_DEFER = 2, ACTION_REPLACE = 3
  integer, parameter :: NFSAMPLE = 12000
  character(len=*), parameter, public :: NO_DECODED_PERIOD =                &
       'no decoded period to decode again (none yet or since a ' //         &
       'mode or period change; MSK144 and JTTY keep none)'

  ! A pass's audio: the period's first n samples and, for JT65+JT9, the
  ! symbol spectra.
  type :: pass_audio
     integer :: n = 0
     integer(int16), allocatable :: id2(:)
     real, allocatable :: ss(:,:)
  end type pass_audio

  ! Each operation carries its place in the stream: the arrival of the last
  ! byte read, the audio clock's start and its last sample read (< 0: no
  ! audio clock), and the period; and the stream's mode, live and ending
  ! flags, parameters and label form as they stood.
  type :: decode_op
     integer :: kind = 0
     real(8) :: arrival = 0.d0, t0 = 0.d0
     integer(int64) :: last_sample = -1, period = 0
     integer :: mode = 0
     logical :: live = .false., ending = .false.
     type(params_block) :: params
     type(time_form) :: form
     ! A pass: its stage; an early pass's half-symbol, and whether it is the
     ! live multithreaded second stage at depth 2; the final's stop; the
     ! period's samples in at the trigger; its audio, or none (memory).
     integer :: stage = 0, nzhsym = 0, nstop = 0, k = 0
     logical :: depth2 = .false., memory = .false.
     type(pass_audio) :: audio
     ! A configure: whether it cancels the multithreaded FT8 scheduler,
     ! starts a new period, and sends clear_average and busy_events, with
     ! their values.
     logical :: cancels = .false., new_period = .false.
     logical :: clear_set = .false., clear_average = .false.
     logical :: events_set = .false., events = .false.
     ! A line: the lines to write, each ended by a newline.
     character(len=:), allocatable :: line
  end type decode_op

  ! The decoder's buffer: a pass's audio, zeros after it (work_n: the
  ! samples the last pass put there; < 0 before the first pass).
  type(dec_data), allocatable, save :: work
  integer, save :: work_n = -1
  ! Snapshot buffers no operation holds, kept for the next pass, and the
  ! bytes of every snapshot buffer, under lib/stream_decode.c's pool lock.
  type(pass_audio), save :: spare(3)
  integer(int64), save :: held_bytes = 0
  ! A decode thread runs the operations (the frame loop's to read).
  logical, save :: threaded = .false.
  ! Test-only hooks, read once at decode_start and inert when unset:
  !   WSJT_CODEC_TEST_INLINE=1             no decode thread
  !   WSJT_CODEC_TEST_HOLD_PASS=<n>:<ms>   sleep <ms> at the start of each
  !                                        pass at half-symbol n, before it decodes,
  !                                        then write "hold done nzhsym=n" on stderr
  !   WSJT_CODEC_TEST_HOLD_TAKE_MS=<ms>    the decode thread sleeps before it
  !                                        takes each operation
  !   WSJT_CODEC_TEST_ALLOC_LIMIT=<bytes>  a snapshot fails when the snapshot
  !                                        buffers would hold more
  !   WSJT_CODEC_TEST_PLAN_GATE=1          the decode thread's first pass waits,
  !                                        before it decodes, until the frame
  !                                        loop has said planning
  integer, save :: hold_pass_nzhsym = -1, hold_pass_ms = 0
  integer(int64), save :: alloc_limit = 0
  logical, save :: plan_gate_wanted = .false.
  ! The decode thread's own: the plan gate has not yet held a pass.
  logical, save :: gate_armed = .false.
  ! The mode and parameters as the last configure or pass left them.
  integer, save :: stream_mode = 0
  type(params_block), save :: stream_params
  ! This side's record of the period's passes: its number, the last FT8 pass
  ! before the stop point decided (its half-symbol, 0: none), and the final.
  integer(int64), save :: record_period = 0
  integer, save :: ft8_done = 0
  logical, save :: decoded = .false.
  ! When the last decode ended (lib/streaming_io.f90 states the busy model),
  ! and the period the engine's input belongs to.
  real(8), save :: busy_until = -huge(1.d0)
  integer(int64), save :: engine_period = -1
  ! Decode-again requests waiting for this period's last FT8 pass.
  integer, save :: pending_again = 0
  ! The last decoded period, for a decode-again request (again_k = 0: none).
  type(pass_audio), save :: again
  integer, save :: again_k = 0, again_nzhsym = 0, again_kin = 0, again_nutc = 0
  logical, save :: again_even = .false.
  type(time_form), save :: again_form
  ! The multithreaded FT8 final deferred while the decoder was busy: its
  ! period's samples, params, number and time form.
  logical, save :: deferred = .false.
  type(pass_audio), save :: deferred_audio
  integer, save :: deferred_k = 0
  integer(int64), save :: deferred_period = 0
  type(params_block), save :: deferred_params
  type(time_form), save :: deferred_form
  ! The last early pass's completion, which the scheduler takes, as the
  ! GUI's does, only when the decoder frees.
  logical, save :: completion_due = .false.
  integer, save :: completion_stage = 0
  integer(int64), save :: completion_period = 0
  ! The GUI's Clear Avg, as configures set or cancel it, for the next period
  ! decode or decode-again that runs.
  logical, save :: pending_clear = .false.
  ! busy_events as this side applies it, whether its last busy line said
  ! true, and the last pass's progress generation.
  logical, save :: events = .false., shown = .false.
  integer, save :: generation = 0
  ! busy_events as the frame loop has applied it.
  logical, save :: loop_events = .false.

  interface
     function stream_stdin_now() result(t) bind(C, name="stream_stdin_now")
       import :: c_double
       real(c_double) :: t
     end function stream_stdin_now
     function stream_stdin_buffered_before(t) result(n)                   &
          bind(C, name="stream_stdin_buffered_before")
       import :: c_double, c_int64_t
       real(c_double), value :: t
       integer(c_int64_t) :: n
     end function stream_stdin_buffered_before
     ! The GUI's multithreaded FT8 scheduler (lib/stream_ft8_backpressure.cpp).
     function stream_ft8_backpressure_request(stage, period, early_stages, &
          busy, pending) result(decision)                                  &
          bind(C, name="stream_ft8_backpressure_request")
       import :: c_int, c_int64_t
       integer(c_int), value :: stage, early_stages, busy, pending
       integer(c_int64_t), value :: period
       integer(c_int) :: decision
     end function stream_ft8_backpressure_request
     subroutine stream_ft8_backpressure_published(stage, period)          &
          bind(C, name="stream_ft8_backpressure_published")
       import :: c_int, c_int64_t
       integer(c_int), value :: stage
       integer(c_int64_t), value :: period
     end subroutine stream_ft8_backpressure_published
     subroutine stream_ft8_backpressure_completed(stage, period)          &
          bind(C, name="stream_ft8_backpressure_completed")
       import :: c_int, c_int64_t
       integer(c_int), value :: stage
       integer(c_int64_t), value :: period
     end subroutine stream_ft8_backpressure_completed
     subroutine stream_ft8_backpressure_publication_failed(stage, period) &
          bind(C, name="stream_ft8_backpressure_publication_failed")
       import :: c_int, c_int64_t
       integer(c_int), value :: stage
       integer(c_int64_t), value :: period
     end subroutine stream_ft8_backpressure_publication_failed
     subroutine stream_ft8_backpressure_cancel()                          &
          bind(C, name="stream_ft8_backpressure_cancel")
     end subroutine stream_ft8_backpressure_cancel
     ! lib/stream_stdin.c and lib/stream_decode.c
     function stream_stdin_threaded() result(yes) bind(C, name="stream_stdin_threaded")
       import :: c_int
       integer(c_int) :: yes
     end function stream_stdin_threaded
     function stream_stdin_arrival() result(t) bind(C, name="stream_stdin_arrival")
       import :: c_double
       real(c_double) :: t
     end function stream_stdin_arrival
     function stream_decode_start(body, take_ms) result(ok)                &
          bind(C, name="stream_decode_start")
       import :: c_funptr, c_int
       type(c_funptr), value :: body
       integer(c_int), value :: take_ms
       integer(c_int) :: ok
     end function stream_decode_start
     subroutine stream_decode_join() bind(C, name="stream_decode_join")
     end subroutine stream_decode_join
     function stream_decode_put(op, bytes, work, pass) result(queued)      &
          bind(C, name="stream_decode_put")
       import :: c_ptr, c_int64_t, c_int
       type(c_ptr), value :: op
       integer(c_int64_t), value :: bytes
       integer(c_int), value :: work, pass
       integer(c_int) :: queued
     end function stream_decode_put
     subroutine stream_decode_publish(position) bind(C, name="stream_decode_publish")
       import :: c_double
       real(c_double), value :: position
     end subroutine stream_decode_publish
     subroutine stream_decode_room() bind(C, name="stream_decode_room")
     end subroutine stream_decode_room
     subroutine stream_decode_idle() bind(C, name="stream_decode_idle")
     end subroutine stream_decode_idle
     function stream_decode_has_work() result(work) bind(C, name="stream_decode_has_work")
       import :: c_int
       integer(c_int) :: work
     end function stream_decode_has_work
     function stream_decode_next(want, idle, busy_until, op) result(step)  &
          bind(C, name="stream_decode_next")
       import :: c_int, c_double, c_ptr
       integer(c_int), value :: want, idle
       real(c_double), value :: busy_until
       type(c_ptr) :: op
       integer(c_int) :: step
     end function stream_decode_next
     subroutine stream_decode_done(held) bind(C, name="stream_decode_done")
       import :: c_int
       integer(c_int), value :: held
     end subroutine stream_decode_done
     subroutine stream_decode_sleep_ms(ms) bind(C, name="stream_decode_sleep_ms")
       import :: c_int
       integer(c_int), value :: ms
     end subroutine stream_decode_sleep_ms
     subroutine stream_decode_pool_lock() bind(C, name="stream_decode_pool_lock")
     end subroutine stream_decode_pool_lock
     subroutine stream_decode_pool_unlock() bind(C, name="stream_decode_pool_unlock")
     end subroutine stream_decode_pool_unlock
     subroutine stream_writer_lock() bind(C, name="stream_writer_lock")
     end subroutine stream_writer_lock
     subroutine stream_writer_unlock() bind(C, name="stream_writer_unlock")
     end subroutine stream_writer_unlock
     function stream_writer_trylock() result(held) bind(C, name="stream_writer_trylock")
       import :: c_int
       integer(c_int) :: held
     end function stream_writer_trylock
     subroutine stream_decode_progress_pass(generation, mode, label, nzhsym) &
          bind(C, name="stream_decode_progress_pass")
       import :: c_int
       integer(c_int), value :: generation, mode, label, nzhsym
     end subroutine stream_decode_progress_pass
     function stream_decode_progress_due(generation, mode, label, nzhsym)  &
          result(due) bind(C, name="stream_decode_progress_due")
       import :: c_int
       integer(c_int), value :: generation
       integer(c_int), intent(out) :: mode, label, nzhsym
       integer(c_int) :: due
     end function stream_decode_progress_due
     subroutine stream_decode_plan_wait() bind(C, name="stream_decode_plan_wait")
     end subroutine stream_decode_plan_wait
     subroutine stream_decode_plan_open() bind(C, name="stream_decode_plan_open")
     end subroutine stream_decode_plan_open
  end interface

contains

  ! Run one operation.
  subroutine decode_handle(op)
    type(decode_op), intent(inout) :: op
    stream_mode = op%mode
    select case (op%kind)
    case (OP_CONFIGURE)
       call configure(op)
    case (OP_PASS)
       call follow_period(op%period)
       stream_params = op%params
       if (op%stage .eq. FINAL_STAGE) then
          call final_pass(op)
       else
          call early_pass(op)
       end if
       call give(op%audio)
    case (OP_AGAIN)
       call follow_period(op%period)
       call decode_again_request(op)
    case (OP_DISCONTINUITY)
       call follow_period(op%period)
       call run_pending_again()
    case (OP_FREE)
       call follow_period(op%period)
       call decoder_frees()
    case (OP_LINE)
       call follow_period(op%period)
       call write_lines(op%line)
    case (OP_END)
       if (shown) call busy_ends()
    end select
  end subroutine decode_handle

  ! Before ready: the decoder's buffer, and the decode thread unless the
  ! library serializes nothing across threads (no OpenMP), no stdin reader
  ! thread runs, or a test runs the handler inline; without one, each
  ! operation runs as it is made.
  subroutine decode_start()
    character(len=32) :: value
    integer :: colon, length, status, take_ms
    allocate(work, stat=status)
    if (status .ne. 0) then
       call streaming_emit_error('no memory for the decoder''s buffer')
       write(error_unit, '(a)') 'wsjt-codec: no memory for the decoder''s buffer'
       stop 1
    end if
    take_ms = 0
    call get_environment_variable('WSJT_CODEC_TEST_HOLD_PASS', value, length, status)
    colon = index(value, ':')
    if (status .eq. 0 .and. colon .gt. 1) then
       read(value(:colon-1), *, iostat=status) hold_pass_nzhsym
       if (status .eq. 0) read(value(colon+1:), *, iostat=status) hold_pass_ms
       if (status .ne. 0) hold_pass_ms = 0
    end if
    call get_environment_variable('WSJT_CODEC_TEST_HOLD_TAKE_MS', value, length, status)
    if (status .eq. 0) read(value, *, iostat=status) take_ms
    if (status .ne. 0) take_ms = 0
    call get_environment_variable('WSJT_CODEC_TEST_ALLOC_LIMIT', value, length, status)
    if (status .eq. 0) read(value, *, iostat=status) alloc_limit
    if (status .ne. 0) alloc_limit = 0
    call get_environment_variable('WSJT_CODEC_TEST_PLAN_GATE', value, length, status)
    plan_gate_wanted = status .eq. 0 .and. value .eq. '1'
    call set_progress_receiver(report_progress)
    call jtty_set_planning_callback(jtty_planning)
    call get_environment_variable('WSJT_CODEC_TEST_INLINE', value, length, status)
    if (status .eq. 0 .and. value .eq. '1') return
    if (.not. streaming_emit_threads()) return
    if (stream_stdin_threaded() .eq. 0) then
       write(error_unit, '(a)') 'wsjt-codec: no stdin reader thread; decoding inline'
       return
    end if
    if (stream_decode_start(c_funloc(decode_thread), take_ms) .eq. 0) then
       write(error_unit, '(a)') 'wsjt-codec: no decode thread; decoding inline'
       return
    end if
    call streaming_emit_set_writer(stream_writer_lock, stream_writer_trylock, &
         stream_writer_unlock)
    threaded = .true.
  end subroutine decode_start

  logical function decode_threaded()
    decode_threaded = threaded
  end function decode_threaded

  ! Hand op to the decode side: queued for the decode thread once the queue
  ! has room, or run now.
  subroutine decode_submit(op)
    type(decode_op), intent(inout) :: op
    type(decode_op), pointer :: queued
    type(pass_audio) :: audio
    character(len=:), allocatable :: line
    integer(int64) :: bytes
    integer :: status
    logical :: work
    work = op%kind .ne. OP_CONFIGURE
    if (op%kind .eq. OP_CONFIGURE .and. op%events_set) then
       work = op%events .neqv. loop_events
       loop_events = op%events
    end if
    if (.not. threaded) then
       call decode_handle(op)
       return
    end if
    call stream_decode_room()
    if (allocated(op%audio%id2)) call move_alloc(op%audio%id2, audio%id2)
    if (allocated(op%audio%ss)) call move_alloc(op%audio%ss, audio%ss)
    if (allocated(op%line)) call move_alloc(op%line, line)
    allocate(queued, stat=status)
    if (status .ne. 0) call no_memory()
    queued = op
    if (allocated(audio%id2)) call move_alloc(audio%id2, queued%audio%id2)
    if (allocated(audio%ss)) call move_alloc(audio%ss, queued%audio%ss)
    if (allocated(line)) call move_alloc(line, queued%line)
    bytes = buffer_bytes(queued%audio) + storage_size(queued) / 8
    if (allocated(queued%line)) bytes = bytes + len(queued%line)
    if (stream_decode_put(c_loc(queued), bytes, merge(1, 0, work),          &
         merge(1, 0, op%kind .eq. OP_PASS)) .eq. 0) call no_memory()
  end subroutine decode_submit

  ! No memory to queue an operation: the session ends as when the input
  ! cannot be buffered, its line written at once, since queuing it needs
  ! memory too, and the writer lock kept, so that nothing follows it.
  subroutine no_memory()
    call streaming_emit_hold(.true.)
    call streaming_emit_error_code('out_of_memory', 'no memory to buffer the input')
    write(error_unit, '(a)') 'wsjt-codec: no memory to buffer the input'
    stop 1
  end subroutine no_memory

  ! The last operation, end or stop: the decode thread leaves its loop and
  ! is joined.
  subroutine decode_join(op)
    type(decode_op), intent(inout) :: op
    ! A decode thread the plan gate (a test hook) holds for a planning that
    ! never comes still ends.
    if (plan_gate_wanted) call stream_decode_plan_open()
    call decode_submit(op)
    if (.not. threaded) return
    call stream_decode_join()
    threaded = .false.
  end subroutine decode_join

  ! The decode thread: each operation lib/stream_decode.c gives it, and the
  ! drain where it falls, until end or stop.
  subroutine decode_thread() bind(C)
    type(c_ptr) :: next
    type(decode_op), pointer :: op
    logical :: last
    integer :: step
    last = .false.
    gate_armed = plan_gate_wanted
    do while (.not. last)
       step = stream_decode_next(merge(1, 0, deferred .or. completion_due),  &
            merge(1, 0, shown), busy_until, next)
       if (step .eq. 1) then
          call c_f_pointer(next, op)
          last = op%kind .eq. OP_END .or. op%kind .eq. OP_STOP
          call decode_handle(op)
          deallocate(op)
       else
          call drained(step .eq. 2)
       end if
       call stream_decode_done(merge(1, 0, deferred .or. completion_due .or.  &
            pending_again .gt. 0 .or. shown))
    end do
  end subroutine decode_thread

  ! Whether the decode side has work (above): only with a decode thread.
  logical function decode_has_work()
    decode_has_work = .false.
    if (threaded) decode_has_work = stream_decode_has_work() .ne. 0
  end function decode_has_work

  ! The frame loop has read to its trigger time at t0 and k_last (as busy
  ! reads them), before it acts on what it read.
  subroutine decode_read(t0, k_last)
    real(8),        intent(in) :: t0
    integer(int64), intent(in) :: k_last
    if (threaded) call stream_decode_publish(trigger_fire_time(stream_stdin_arrival(), &
         t0, k_last))
  end subroutine decode_read

  ! Before the frame loop takes a pass's audio: while the queued operations
  ! hold lib/stream_decode.c's cap.
  subroutine decode_room()
    if (threaded) call stream_decode_room()
  end subroutine decode_room

  ! Before a snapshot's second try: until no operation is queued or runs.
  subroutine decode_wait()
    if (threaded) call stream_decode_idle()
  end subroutine decode_wait

  ! Where the GUI's decoder frees, before a read on the thread that decodes
  ! too: before anything that came in, or audio due, after the last decode
  ! ended. The read takes nbytes; last_sample is its last audio sample (< 0:
  ! none, or no audio clock). With a decode thread, the decode thread finds
  ! this point (lib/stream_decode.c).
  subroutine decode_drain(nbytes, t0, last_sample)
    integer(int64), intent(in) :: nbytes, last_sample
    real(8),        intent(in) :: t0
    if (threaded) return
    if (.not. (deferred .or. completion_due .or. shown)) return
    if (read_passes_busy_end(stream_stdin_buffered_before(busy_until),       &
         nbytes, t0, last_sample, busy_until)) call drained(.true.)
  end subroutine decode_drain

  ! The decoder frees (decoder_frees). With nothing more to come from before
  ! it freed (idle), busy turns false unless a final then ran or waits.
  subroutine drained(idle)
    logical, intent(in) :: idle
    real(8) :: freed
    freed = busy_until
    call decoder_frees()
    if (idle .and. shown .and. .not. deferred .and. busy_until .eq. freed)  &
         call busy_ends()
  end subroutine drained

  ! An accepted configure. Where the GUI cancels its multithreaded FT8
  ! backpressure, the stream does too, dropping a waiting final; the dropped
  ! final's period ends first, under its own labels. A new period refuses the
  ! decode-again requests held for the last and forgets the decoded period.
  subroutine configure(op)
    type(decode_op), intent(in) :: op
    if (op%cancels) then
       call stream_ft8_backpressure_cancel()
       if (deferred) call skip_pass(deferred_params%nzhsym, .true.,         &
            'cancelled', deferred_params, deferred_form)
       call drop_deferred()
    end if
    call follow_period(op%period)
    if (op%new_period) then
       again_k = 0
       call give(again)
    end if
    if (op%clear_set) pending_clear = op%clear_average
    stream_params = op%params
    ! Turned on, busy_events says busy for a final that waits.
    if (op%events_set .and. (op%events .neqv. events)) then
       events = op%events
       shown = .false.
       if (events .and. deferred) call busy_waits()
    end if
  end subroutine configure

  ! An operation of a new period: the decode-again requests held for the
  ! last are refused, and the record of passes starts again.
  subroutine follow_period(period)
    integer(int64), intent(in) :: period
    if (period .eq. record_period) return
    do while (pending_again .gt. 0)
       pending_again = pending_again - 1
       call streaming_emit_error_code('decode_again_unavailable',             &
            'the mode or period changed before the decode again could run')
    end do
    record_period = period
    decoded  = .false.
    ft8_done = 0
  end subroutine follow_period

  ! nagain_flag asks once for the last decoded period again.
  subroutine decode_again_request(op)
    type(decode_op), intent(in) :: op
    if (again_k .le. 0) then
       call streaming_emit_error_code('decode_again_unavailable', NO_DECODED_PERIOD)
    else if (busy(op)) then
       ! The GUI's Decode button is disabled while its decoder is busy.
       call streaming_emit_error_code('decode_again_unavailable', 'the decoder is busy')
    else if (stream_mode .eq. 8 .and. ft8_done .gt. 0 .and. .not. decoded) then
       ! Between this period's FT8 passes the decoder holds their
       ! state; the request runs after the last pass.
       pending_again = pending_again + 1
    else
       call decode_again()
    end if
  end subroutine decode_again_request

  ! An FT8 pass before the stop point, run when its trigger fires if the GUI
  ! would run it.
  subroutine early_pass(op)
    type(decode_op), intent(inout) :: op
    type(params_block) :: p
    if (early_pass_runs(op)) then
       if (op%memory) then
          call skip_pass(op%nzhsym, .false., 'memory', op%params, op%form)
          if (backpressure(op%live, op%ending, op%mode, op%params))         &
               call stream_ft8_backpressure_publication_failed(op%stage, op%period)
       else
          p = op%params
          p%nclearave = pending_clear
          p%newdat = .true.
          p%nagain = .false.
          ! The GUI's live early stages of the multithreaded decoder.
          if (p%lmultift8 .and. op%live) then
             p%nft8cycles = 2
             if (op%depth2) p%ndepth = 2
          end if
          p%lmultift8 = .false.
          call streaming_emit_set_form(op%form)
          ! Live, the decoder completes each pass, as for the GUI; the period's
          ! decode_finished waits for its last.
          call streaming_emit_hold_finished(.true.)
          call ft8_window_pass(op, p, op%nzhsym)
          call streaming_emit_hold_finished(.false.)
          if (backpressure(op%live, op%ending, op%mode, op%params)) then
             completion_due    = .true.
             completion_stage  = op%stage
             completion_period = op%period
          end if
       end if
    end if
    ft8_done = op%nzhsym
  end subroutine early_pass

  ! This period's decode, from the decision on whether it runs; the frame
  ! loop triggers it once per period.
  subroutine final_pass(op)
    type(decode_op), intent(inout) :: op
    integer :: decision, nz
    logical :: requested, multithreaded, bp
    real(8) :: finished_before
    type(params_block) :: p
    bp = backpressure(op%live, op%ending, op%mode, op%params)
    if (op%mode .eq. 8 .and. superfox_even(op%params) .and. op%nstop .lt. 50) then
       ! On SuperFox's even sequence the GUI decodes nothing before 50, after
       ! asking its multithreaded backpressure.
       if (bp) decision = stream_ft8_backpressure_request(FINAL_STAGE,       &
            op%period, early_stages(op%params), merge(1, 0, busy(op)),       &
            merge(1, 0, deferred))
       decoded = .true.
       call run_pending_again()
       return
    end if
    decoded = .true.
    requested = op%params%lmultift8
    multithreaded = requested .and. .not. superfox_hound(op%params)
    if (bp) then
       decision = stream_ft8_backpressure_request(FINAL_STAGE, op%period,    &
            early_stages(op%params), merge(1, 0, busy(op)), merge(1, 0, deferred))
       if (decision .eq. ACTION_DEFER .or. decision .eq. ACTION_REPLACE) then
          call defer_final(op)
          call run_pending_again()
          return
       end if
    else if (busy(op)) then
       call skip_pass(op%params%nzhsym, .true., 'busy', op%params, op%form)
       call run_pending_again()
       return
    end if
    if (op%memory) then
       call skip_pass(op%params%nzhsym, .true., 'memory', op%params, op%form)
       if (bp) call stream_ft8_backpressure_publication_failed(FINAL_STAGE, op%period)
       call run_pending_again()
       return
    end if
    call begin_engine_input(op%period)
    call streaming_emit_set_form(op%form)
    finished_before = streaming_emit_finished_at()
    p = op%params
    p%nclearave = pending_clear
    if (op%mode .eq. 8 .and. .not. multithreaded) then
       ! The final pass of the standard decoder's sequence: it goes on from
       ! the audio this period's early passes saved in ft8_decode.f90, or
       ! takes its own when none ran. Live it sees the audio up to the stop;
       ! from a file the GUI's final reads the whole period (lib/jt9a.f90).
       p%lmultift8 = .false.
       if (op%live) then
          nz = p%nzhsym
          call ft8_window_pass(op, p, nz)
       else
          call load_work(op%audio, op%k)
          call report_pass(p, op%k)
          call run_decoder(p)
       end if
    else if (op%mode .eq. 8) then
       p%lmultift8 = .true.
       call load_work(op%audio, op%k)
       call report_pass(p, op%k)
       call run_decoder(p)
    else
       call load_work(op%audio, op%k)
       call report_pass(p, op%k)
       call run_decoder(p)
    end if
    busy_until = decoder_freed_at(finished_before)
    if (bp) call stream_ft8_backpressure_published(FINAL_STAGE, op%period)
    ! The GUI's Clear Avg applies to the next decode that runs.
    pending_clear = .false.
    call run_pending_again()
    ! This period becomes the one to decode again, on this pass's audio.
    call take_over(op%audio, again)
    again_k      = op%k
    again_nzhsym = p%nzhsym
    again_kin    = p%kin
    again_nutc   = p%nutc
    again_even   = p%b_even_seq
    again_form   = op%form
  end subroutine final_pass

  ! The GUI's Decode button: one pass with nagain over the last decoded
  ! period, with its time and sequence.
  subroutine decode_again()
    type(params_block) :: p
    real(8) :: finished_before
    p = stream_params
    p%nclearave = pending_clear
    p%nagain = .true.
    p%newdat = .false.
    p%nzhsym = again_nzhsym
    p%kin    = again_kin
    p%nutc   = again_nutc
    p%b_even_seq = again_even
    call streaming_emit_set_form(again_form)
    if (superfox_hound(p)) p%lmultift8 = .false.
    ! The single-threaded FT8 decoder decodes again only on the hsym-50 pass.
    if (stream_mode .eq. 8 .and. .not. p%lmultift8) p%nzhsym = 50
    call load_work(again, again_k)
    call report_pass(p, again_k)
    finished_before = streaming_emit_finished_at()
    call run_decoder(p)
    busy_until = decoder_freed_at(finished_before)
    pending_clear = .false.
  end subroutine decode_again

  ! A decode-again request held for the period's last FT8 pass, now it has run.
  subroutine run_pending_again()
    do while (pending_again .gt. 0)
       pending_again = pending_again - 1
       call decode_again()
    end do
  end subroutine run_pending_again

  ! One FT8 pass over the first nz half-symbols of op's audio, the rest zeroed.
  subroutine ft8_window_pass(op, p, nz)
    type(decode_op),    intent(in)    :: op
    type(params_block), intent(inout) :: p
    integer,            intent(in)    :: nz
    real(8) :: finished_before
    p%nzhsym = nz
    call begin_engine_input(op%period)
    call load_work(op%audio, min(op%audio%n, nz * HSYM_SAMPLES))
    call report_pass(p, op%k)
    finished_before = streaming_emit_finished_at()
    call run_decoder(p)
    busy_until = decoder_freed_at(finished_before)
  end subroutine ft8_window_pass

  ! The decoder on the decoder's buffer with p; with busy_events, its busy
  ! line first, and its progress reported under a fresh generation.
  subroutine run_decoder(p)
    type(params_block), intent(inout) :: p
    character(len=6) :: label
    integer :: pass, number, status
    pass = 0
    if (events) then
       label = streaming_emit_label(p%nutc, label_hhmmss(p%nmode))
       call streaming_emit_busy(p%nmode, label, p%nzhsym, logical(p%nagain), .false.)
       shown = .true.
       generation = generation + 1
       pass = generation
       read(label, '(i6)', iostat=status) number
       if (status .ne. 0) number = 0
       call stream_decode_progress_pass(pass, p%nmode, number, p%nzhsym)
    end if
    if (hold_pass_ms .gt. 0 .and. p%nzhsym .eq. hold_pass_nzhsym) then
       call stream_decode_sleep_ms(hold_pass_ms)
       write(error_unit, '(a,i0)') 'wsjt-codec: hold done nzhsym=', p%nzhsym
    end if
    if (gate_armed) then
       gate_armed = .false.
       call stream_decode_plan_wait()
    end if
    call multimode_decoder_progress(work%ss, work%id2, p, NFSAMPLE, pass)
    if (pass .gt. 0) call stream_decode_progress_pass(0, 0, 0, 0)
  end subroutine run_decoder

  ! busy true for the final now waiting.
  subroutine busy_waits()
    call streaming_emit_busy(deferred_params%nmode,                         &
         streaming_emit_label(deferred_params%nutc,                         &
         label_hhmmss(deferred_params%nmode), deferred_form),               &
         deferred_params%nzhsym, .false., .true.)
    shown = .true.
  end subroutine busy_waits

  subroutine busy_ends()
    call streaming_emit_idle()
    shown = .false.
  end subroutine busy_ends

  ! The decoders' progress reports, from any thread: the running pass's
  ! pass_progress, at most one a second (lib/stream_decode.c).
  subroutine report_progress(pass)
    integer, intent(in) :: pass
    integer(c_int) :: mode, number, nzhsym
    character(len=6) :: label
    if (stream_decode_progress_due(pass, mode, number, nzhsym) .eq. 0) return
    write(label, '(i6.6)') number
    call streaming_emit_progress(mode, label, nzhsym)
  end subroutine report_progress

  ! A JTTY decoder step on the frame loop's thread is about to measure an
  ! FFT plan (lib/jtty/jtty_mdecode.f90).
  subroutine jtty_planning()
    call jtty_measuring(loop_events)
    if (plan_gate_wanted) call stream_decode_plan_open()
  end subroutine jtty_planning

  ! A line operation's lines.
  subroutine write_lines(lines)
    character(len=*), intent(in) :: lines
    integer :: first, length
    first = 1
    do
       length = index(lines(first:), achar(10)) - 1
       if (length .lt. 0) exit
       call streaming_emit_line(lines(first:first+length-1))
       first = first + length + 1
    end do
  end subroutine write_lines

  ! The pass's stderr line, and its half-symbol for its decodes.
  subroutine report_pass(p, samples)
    type(params_block), intent(in) :: p
    integer,            intent(in) :: samples
    call report_pass_line(p, samples)
    call streaming_emit_set_pass(p%nzhsym)
  end subroutine report_pass

  ! Each decode pass on stderr: the samples in when it ran, and how.
  subroutine report_pass_line(p, samples)
    type(params_block), intent(in) :: p
    integer,            intent(in) :: samples
    character(len=16) :: delay
    write(delay, '(f16.1)') p%emedelay
    write(error_unit, '(a,i0,a,i0,a,i0,a,i0,a,l1,a,l1,a,l1,2a)')            &
         'wsjt-codec: pass nzhsym=', p%nzhsym,                               &
         ' samples=', samples, ' depth=', p%ndepth,                          &
         ' cycles=', p%nft8cycles,                                           &
         ' multithreaded=', p%lmultift8,                                     &
         ' live=', .not. p%ndiskdat,                                         &
         ' again=', p%nagain,                                                &
         ' emedelay=', trim(adjustl(delay))
  end subroutine report_pass_line

  ! When the decoder came free after a decode: when it sent decode_finished,
  ! if it did, so that a request answering that event never finds it busy.
  real(8) function decoder_freed_at(finished_before)
    real(8), intent(in) :: finished_before
    decoder_freed_at = stream_stdin_now()
    if (streaming_emit_finished_at() .gt. finished_before)                 &
         decoder_freed_at = streaming_emit_finished_at()
  end function decoder_freed_at

  ! The GUI's decoder starts an input for each period it receives.
  subroutine begin_engine_input(p)
    integer(int64), intent(in) :: p
    if (p .ne. engine_period) call engine_begin_input()
    engine_period = p
  end subroutine begin_engine_input

  ! The GUI's decoderBusy when the trigger that op's last byte read completes
  ! fires; never from a file, or at halt or end of input.
  logical function busy(op)
    type(decode_op), intent(in) :: op
    busy = .false.
    if (.not. op%live .or. op%ending) return
    busy = deferred .or.                                                   &
         trigger_fire_time(op%arrival, op%t0, op%last_sample) .lt. busy_until
  end function busy

  ! The GUI's multithreaded FT8 backpressure (usesFt8MtdFinal), for live audio.
  pure logical function backpressure(live, ending, mode, p)
    logical,            intent(in) :: live, ending
    integer,            intent(in) :: mode
    type(params_block), intent(in) :: p
    backpressure = live .and. .not. ending .and. mode .eq. 8 .and.         &
         p%lmultift8 .and. .not. superfox_hound(p)
  end function backpressure

  ! The GUI's early stages for the multithreaded decoder start
  ! (configuredFt8MtdEarlyStageCount).
  pure integer function early_stages(p)
    type(params_block), intent(in) :: p
    early_stages = 0
    if (p%ndecoderstart .eq. 0) early_stages = 1
    if (p%ndecoderstart .eq. 1) early_stages = 2
  end function early_stages

  ! The GUI's SuperFox is the setting with Fox or Hound (widgets/mainwindow.cpp).
  pure logical function superfox_even(p)
    type(params_block), intent(in) :: p
    integer :: contest
    contest = iand(p%nexp_decode, 7)
    superfox_even = p%b_superfox .and. p%b_even_seq                        &
         .and. (contest .eq. 6 .or. contest .eq. 7)
  end function superfox_even

  ! SuperFox Hound decodes single-threaded, as the GUI does.
  pure logical function superfox_hound(p)
    type(params_block), intent(in) :: p
    superfox_hound = iand(p%nexp_decode, 7) .eq. 7 .and. p%b_superfox
  end function superfox_hound

  ! JT9, JT65 and JT65+JT9 periods of a minute or more, which the GUI labels
  ! HHMM (widgets/mainwindow.cpp, decode).
  pure logical function hhmm_label(mode, trperiod)
    integer, intent(in) :: mode
    real(8), intent(in) :: trperiod
    hhmm_label = (mode .eq. 9 .or. mode .eq. 65 .or. mode .eq. 74) .and. trperiod .ge. 60.d0
  end function hhmm_label

  ! As lib/decoder.f90 labels decode_finished.
  pure logical function label_hhmmss(nmode)
    integer, intent(in) :: nmode
    label_hhmmss = nmode .eq. 4 .or. nmode .eq. 66 .or. (nmode .ge. 240 .and. nmode .le. 242)
  end function label_hhmmss

  ! Whether the early FT8 pass op runs when its trigger fires, as the GUI
  ! decides; one the GUI refuses is reported, and SuperFox's even sequence
  ! runs none.
  logical function early_pass_runs(op)
    type(decode_op), intent(in) :: op
    logical :: was_busy
    was_busy = busy(op)
    if (backpressure(op%live, op%ending, op%mode, op%params)) then
       early_pass_runs = stream_ft8_backpressure_request(op%stage, op%period, &
            early_stages(op%params), merge(1, 0, was_busy),                   &
            merge(1, 0, deferred)) .eq. ACTION_PUBLISH
       if (.not. early_pass_runs .and. was_busy) then
          call skip_pass(op%nzhsym, .false., 'busy', op%params, op%form)
       else if (.not. early_pass_runs) then
          call skip_pass(op%nzhsym, .false., 'backoff', op%params, op%form)
       end if
       ! SuperFox's even sequence decodes nothing before 50 (ft8_early_schedule).
       if (superfox_even(op%params)) early_pass_runs = .false.
    else
       early_pass_runs = .not. was_busy
       if (was_busy) call skip_pass(op%nzhsym, .false., 'busy', op%params, op%form)
    end if
  end function early_pass_runs

  ! A pass not run, on stderr and as pass_skipped with the period label of
  ! the decode params p, in time form `form`; a final not run still ends
  ! its period.
  subroutine skip_pass(nzhsym, final, reason, p, form)
    integer,            intent(in) :: nzhsym
    logical,            intent(in) :: final
    character(len=*),   intent(in) :: reason
    type(params_block), intent(in) :: p
    type(time_form),    intent(in) :: form
    character(len=5) :: stage
    stage = merge('final', 'early', final)
    write(error_unit, '(a,i0,4a)') 'wsjt-codec: skip nzhsym=',             &
         nzhsym, ' stage=', stage, ' reason=', reason
    call streaming_emit_set_form(form)
    call streaming_emit_hold(.true.)
    call streaming_emit_pass_skipped(p%nmode, p%nutc, label_hhmmss(p%nmode), &
         nzhsym, stage, reason)
    if (final) call streaming_emit_decode_finished(p%nutc, label_hhmmss(p%nmode))
    call streaming_emit_hold(.false.)
  end subroutine skip_pass

  ! The GUI's decoder frees: its scheduler takes the last early pass's
  ! completion, then a deferred final runs, unless its decode range or
  ! submode changed since it was deferred, when the GUI drops it and cancels.
  subroutine decoder_frees()
    if (completion_due) call stream_ft8_backpressure_completed(completion_stage, &
         completion_period)
    completion_due = .false.
    if (.not. deferred) return
    if (ft8_identity_change(deferred_params, stream_params)) then
       call stream_ft8_backpressure_cancel()
       call skip_pass(deferred_params%nzhsym, .true., 'cancelled', deferred_params, &
            deferred_form)
       call drop_deferred()
    else
       call run_deferred_final()
    end if
  end subroutine decoder_frees

  ! The GUI's deferred multithreaded final: this period's samples, params,
  ! number and time form, kept until the decoder comes free; a final already
  ! waiting gives way. Without its samples it cannot wait: it is skipped,
  ! and the GUI's scheduler cancelled, as when a waiting final is dropped.
  subroutine defer_final(op)
    type(decode_op), intent(inout) :: op
    if (deferred) call skip_pass(deferred_params%nzhsym, .true., 'replaced', &
         deferred_params, deferred_form)
    if (op%memory) then
       call drop_deferred()
       call skip_pass(op%params%nzhsym, .true., 'memory', op%params, op%form)
       call stream_ft8_backpressure_cancel()
       return
    end if
    call take_over(op%audio, deferred_audio)
    deferred_k      = op%k
    deferred_params = op%params
    deferred_params%nclearave = pending_clear
    deferred_period = op%period
    deferred_form   = op%form
    deferred        = .true.
    write(error_unit, '(a,i0)') 'wsjt-codec: defer nzhsym=',               &
         op%params%nzhsym
    if (events) call busy_waits()
  end subroutine defer_final

  subroutine drop_deferred()
    deferred = .false.
    call give(deferred_audio)
  end subroutine drop_deferred

  ! The deferred final, on its own samples, params and time; its period
  ! becomes the one to decode again.
  subroutine run_deferred_final()
    real(8) :: finished_before
    deferred = .false.
    call streaming_emit_set_form(deferred_form)
    call begin_engine_input(deferred_period)
    call load_work(deferred_audio, deferred_k)
    call report_pass(deferred_params, deferred_k)
    finished_before = streaming_emit_finished_at()
    call run_decoder(deferred_params)
    busy_until = decoder_freed_at(finished_before)
    call stream_ft8_backpressure_published(FINAL_STAGE, deferred_period)
    call take_over(deferred_audio, again)
    again_k      = deferred_k
    again_nzhsym = deferred_params%nzhsym
    again_kin    = deferred_params%kin
    again_nutc   = deferred_params%nutc
    again_even   = deferred_params%b_even_seq
    again_form   = deferred_form
  end subroutine run_deferred_final

  ! The decoder's buffer holds audio's first n samples, zeros after them, and
  ! audio's spectra when it has them.
  subroutine load_work(audio, n)
    type(pass_audio), intent(in) :: audio
    integer,          intent(in) :: n
    ! Zeroed at the first pass: a session that decodes none never touches it.
    if (work_n .lt. 0) then
       work%id2 = 0
       work%ss = 0
       work_n = 0
    end if
    work%id2(1:n) = audio%id2(1:n)
    if (work_n .gt. n) work%id2(n+1:work_n) = 0
    work_n = n
    if (allocated(audio%ss)) work%ss = audio%ss
  end subroutine load_work

  ! audio <- the first n samples of live's id2 and, with spectra, its symbol
  ! spectra, in a spare buffer when one is free; ok is false when no memory
  ! could be had.
  subroutine snapshot(audio, live, n, spectra, ok)
    type(pass_audio), intent(inout) :: audio
    type(dec_data),   intent(in)    :: live
    integer,          intent(in)    :: n
    logical,          intent(in)    :: spectra
    logical,          intent(out)   :: ok
    call stream_decode_pool_lock()
    call buffers(audio, n, spectra, ok)
    call stream_decode_pool_unlock()
    if (.not. ok) return
    audio%id2(1:n) = live%id2(1:n)
    if (spectra) audio%ss = live%ss
    audio%n = n
  end subroutine snapshot

  ! snapshot's buffers, under the pool lock.
  subroutine buffers(audio, n, spectra, ok)
    type(pass_audio), intent(inout) :: audio
    integer,          intent(in)    :: n
    logical,          intent(in)    :: spectra
    logical,          intent(out)   :: ok
    integer :: i, stat
    integer(int64) :: bytes
    ok = .false.
    if (.not. allocated(audio%id2)) then
       do i = 1, size(spare)
          if (allocated(spare(i)%id2)) then
             if (size(spare(i)%id2) .ge. n) exit
          end if
       end do
       if (i .gt. size(spare)) then
          do i = 1, size(spare)
             if (allocated(spare(i)%id2)) exit
          end do
       end if
       if (i .le. size(spare)) then
          call move_alloc(spare(i)%id2, audio%id2)
          if (allocated(spare(i)%ss)) call move_alloc(spare(i)%ss, audio%ss)
       end if
    end if
    if (allocated(audio%id2)) then
       if (size(audio%id2) .lt. n) then
          held_bytes = held_bytes - 2_int64 * size(audio%id2)
          deallocate(audio%id2)
       end if
    end if
    if (.not. allocated(audio%id2)) then
       bytes = 2_int64 * max(n, 1)
       if (.not. fits(bytes)) return
       allocate(audio%id2(max(n, 1)), stat=stat)
       if (stat .ne. 0) return
       held_bytes = held_bytes + bytes
    end if
    if (.not. spectra .and. allocated(audio%ss)) then
       held_bytes = held_bytes - 4_int64 * size(audio%ss)
       deallocate(audio%ss)
    end if
    if (spectra .and. .not. allocated(audio%ss)) then
       bytes = 4_int64 * 184 * NSMAX
       if (.not. fits(bytes)) return
       allocate(audio%ss(184, NSMAX), stat=stat)
       if (stat .ne. 0) return
       held_bytes = held_bytes + bytes
    end if
    ok = .true.
  end subroutine buffers

  ! Whether bytes more of snapshot buffers are within the test's limit.
  logical function fits(bytes)
    integer(int64), intent(in) :: bytes
    fits = alloc_limit .le. 0 .or. held_bytes + bytes .le. alloc_limit
  end function fits

  ! The bytes audio's buffers hold.
  integer(int64) function buffer_bytes(audio)
    type(pass_audio), intent(in) :: audio
    buffer_bytes = 0
    if (allocated(audio%id2)) buffer_bytes = 2_int64 * size(audio%id2)
    if (allocated(audio%ss)) buffer_bytes = buffer_bytes + 4_int64 * size(audio%ss)
  end function buffer_bytes

  ! to takes from's buffers, its own going back to the spares.
  subroutine take_over(from, to)
    type(pass_audio), intent(inout) :: from, to
    call give(to)
    if (allocated(from%id2)) call move_alloc(from%id2, to%id2)
    if (allocated(from%ss)) call move_alloc(from%ss, to%ss)
    to%n = from%n
    from%n = 0
  end subroutine take_over

  ! audio's buffers back to the spares, or freed when none is empty.
  subroutine give(audio)
    type(pass_audio), intent(inout) :: audio
    integer :: i
    audio%n = 0
    call stream_decode_pool_lock()
    if (allocated(audio%id2)) then
       do i = 1, size(spare)
          if (.not. allocated(spare(i)%id2)) then
             call move_alloc(audio%id2, spare(i)%id2)
             if (allocated(audio%ss)) call move_alloc(audio%ss, spare(i)%ss)
             exit
          end if
       end do
    end if
    held_bytes = held_bytes - buffer_bytes(audio)
    if (allocated(audio%id2)) deallocate(audio%id2)
    if (allocated(audio%ss)) deallocate(audio%ss)
    call stream_decode_pool_unlock()
  end subroutine give

end module streaming_decode
