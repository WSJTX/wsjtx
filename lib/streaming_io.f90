! Streaming I/O — framed audio + control on stdin.
!
! Reads framed messages from stdin. After a
! one-time 8-byte WSJT session header (4-byte magic 'WSJT' + 1-byte fmt
! + 1-byte channels + 2-byte rate_kHz LE), stdin carries a sequence of
! frames:
!
!   [type:1][len:4 LE][body:len bytes]
!
! Frame types:
!   0x01  audio chunk     body = N samples in declared format
!   0x02  control JSON    body = UTF-8 JSON line (no trailing newline) of at
!                         most 262,144 bytes; a longer one is reported
!                         (control_frame_too_large) and skipped
!
! Other types are skipped (forward-compat: consumers ignore unknown
! frame types just as NDJSON consumers ignore unknown "t" values). A frame
! of any type longer than 43,200,000 bytes, the sample buffer (1800 s at
! 12 kHz), is fatal: error frame_too_large, then exit 1.
!
! Control frames recognized today: {"t":"configure",...}, {"t":"halt"},
! {"t":"discontinuity"}, and the JTTY encode request {"t":"pack",...}
! (lib/streaming_jtty.f90), answered in any mode. See
! lib/streaming_control.f90 for the parser + schema.
!
! Apply policy:
!   configure : applied at receive. A change of mode or period starts a
!               new period; what was in flight is discarded. Producers
!               send `configure` first; audio is interpreted under the
!               most recent one. A declined configure applies none of
!               its keys. A configure whose trperiod is not a finite
!               number above 0 and at most 1800 s is declined, with
!               error invalid_trperiod. One the decoder would refuse in
!               its mode is declined with configure_range_error (the
!               engine's checks, lib/decoder_engine.f90); values it
!               carries from earlier frames are checked against a new
!               mode too. The command-line starting values are checked
!               once, after ready.
!   halt      : drain the current period (decode if it has samples), exit.
!
! A period is TRperiod * 12 kHz samples (lib/streaming_period.f90). Keep a
! frame remainder for the next period; retain only the first npts samples
! for decoding. The reader's phase is that of the first audio after
! `configure`; send every sample.
!
! JTTY ({"mode":"JTTY"}; no integer code or command-line flag selects it) has
! no periods: its audio goes to lib/streaming_jtty.f90, which states its
! output. A JTTY session ends at a discontinuity, an odd-length audio frame, a
! configure that leaves JTTY, halt and end of input; the period modes ignore
! discontinuity. Entering JTTY, rxfreq carries over, ntol becomes 20 and nfa
! and nfb their values at the start of the stream (-L and -H, 200 and 4000 Hz
! by default; with -9 and an -S other than 2700, nfa is -S) unless the frame
! sets them. Any configure that carries mode or trperiod and stays in JTTY
! keeps the session but resets in the same way whichever of nfa, nfb and ntol
! it does not set. trperiod is validated and unused. lib/streaming_jtty.f90
! states which frequency values a configure in JTTY is declined for
! (configure_range_error).
!
! Format support: fmt=0x00 (int16 PCM) only. Other formats may be
! added later.
!
! Platform: reads stdin through a portable C read-fully helper on fd 0
! (stream_stdin_read_fully, lib/stream_stdin.c) — the same code path on
! every platform. Fortran unit I/O cannot read a redirected stdin
! portably: native Windows has no /dev/stdin, and re-OPENing the
! preconnected unit 5 with ACCESS='STREAM' is non-conforming (F2018
! 12.5.6.1 — ACCESS is not a changeable mode), rejected by libgfortran
! everywhere. Binary mode is forced on Windows first via
! stream_set_stdin_binary (lib/stream_setmode.c; no-op on POSIX). The
! helper loops read() until the request fills or the stream truly ends,
! so a pipe short-read is never mistaken for EOF.

subroutine jt9_stream(shared_data, mode, TRperiod)
  use, intrinsic :: iso_fortran_env, only: int8, int16, int64, error_unit
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use, intrinsic :: iso_c_binding, only: c_int64_t
  use prog_args, only: data_dir
  use timer_module, only: timer
  use streaming_emit, only: streaming_emit_ready,                          &
       streaming_emit_decode_finished, streaming_emit_error,               &
       streaming_emit_error_code, streaming_emit_error_version,            &
       streaming_emit_error_type, streaming_emit_set_time_form,            &
       streaming_emit_utc_from_iso, streaming_emit_hold_finished,          &
       streaming_emit_error_range, streaming_emit_mode_name
  use streaming_control, only: parse_control_frame, configure_fields,      &
       control_type_error, CTRL_CONFIGURE, CTRL_HALT, CTRL_PARSE_ERR,      &
       CTRL_DISCONTINUITY, CTRL_PACK, MODE_JTTY
  use streaming_apply, only: apply_configure_fields, engine_range_key
  use decoder_engine, only: engine_params_problem, option_problem
  use streaming_jtty, only: jtty_audio, jtty_end, jtty_range_key,          &
       jtty_emit_range_error, jtty_release, jtty_note_station, jtty_encode
  use streaming_period, only: period_state, period_begin, period_room,   &
       period_take, period_full, period_ready, period_set_stop,           &
       gui_hsym_stop, HSYM_SAMPLES

  include 'jt9com.f90'

  type(dec_data) :: shared_data
  integer        :: mode
  real(8)        :: TRperiod

  integer, parameter :: HDR_LEN = 8
  integer, parameter :: NFSAMPLE = 12000
  integer, parameter :: FRAME_AUDIO   = 1
  integer, parameter :: FRAME_CONTROL = 2
  integer, parameter :: CTL_BUF_LEN = 262144
  ! 4-byte session magic "WSJT" (W=0x57 S=0x53 J=0x4A T=0x54)
  integer(int8), parameter :: MAGIC(4) = [                                 &
       int(z'57', int8), int(z'53', int8),                                 &
       int(z'4A', int8), int(z'54', int8) ]

  integer :: ios, fmt, ch, rate_khz
  integer(int8)  :: hdr(HDR_LEN), type_byte(1), len_bytes(4)
  integer(int64) :: frame_len_wide, max_audio_frame_bytes
  integer        :: frame_len

  integer :: nsps, kstep, nhsym, nhsym0
  ! The period's sample accounting; `body_left` carries a straddling
  ! frame's remainder into the next period.
  type(period_state) :: period
  ! This period's decode has run.
  logical :: decoded
  ! The last FT8 pass before the stop point already run this period, as the
  ! half-symbol it covered (0: none).
  integer :: ft8_done
  ! The GUI's "Decode after EME delay", and live audio rather than a file.
  logical :: eme_delay_on, live_audio
  ! Decode-again requests waiting for this period's last FT8 pass.
  integer :: pending_again
  integer :: ingain, nminw, ihsym, npts8
  real    :: pxdb, df3, pxdbmax, s(NSMAX)
  integer :: npct_unused
  integer :: body_left, take_bytes, take_samples
  integer(int16) :: chunk(4096)
  integer(int8)  :: byte_sink(4096)
  ! FT8 progressive-decode working buffer (mirrors jt9.f90:19's id2a) — the
  ! FT8 decoder's `dd` array is populated only on nzhsym <= 47 calls, so we
  ! mirror the WAV path's 41/47/50 call cadence with a working copy zeroed
  ! past the per-call sample boundary.
  integer(int16), save :: id2a(180000)  !Keep this big array off the stack
  ! The last decoded period, for a decode-again request (again_k = 0: none),
  ! and the period in flight while that runs.
  integer(int16), allocatable, save :: again_id2(:), held_id2(:)
  real, allocatable, save :: again_ss(:,:), held_ss(:,:)
  integer, save :: again_k = 0, again_nzhsym = 0, again_kin = 0, again_nutc = 0
  logical, save :: again_even = .false., again_iso = .false.
  character(len=CTL_BUF_LEN), save :: ctl_buf     !Off the stack, as id2a
  integer(int8), save :: ctl_bytes(CTL_BUF_LEN)
  integer :: i_ctl, prev_mode, next_mode
  real(8) :: prev_TRperiod, next_TRperiod
  type(params_block) :: next_params
  character(len=:), allocatable :: range_key
  type(option_problem) :: problem
  type(configure_fields) :: no_keys
  logical :: next_eme_on
  ! Audio samples consumed in any mode; JTTY's origin and times count them.
  integer(int64) :: stream_samples
  ! Session-baseline nfa/nfb stashed after jt9.f90's init_streaming_extra_fields
  ! returned. apply_configure_fields restores these on mode change with no
  ! explicit consumer override; passed in as dummy args.
  integer :: baseline_nfa, baseline_nfb
  logical :: bLowSidelobes, eof_period, halt_req
  type(configure_fields)   :: cfg
  type(control_type_error) :: terr
  integer :: action

  ! C stdin helpers. stream_set_stdin_binary (lib/stream_setmode.c) is a
  ! no-op on POSIX; stream_stdin_read_fully (lib/stream_stdin.c) is the
  ! read-fully byte reader used on all platforms.
  interface
     subroutine stream_set_stdin_binary() bind(C, name="stream_set_stdin_binary")
     end subroutine stream_set_stdin_binary
     function stream_stdin_read_fully(buf, n) result(nread)               &
          bind(C, name="stream_stdin_read_fully")
       import :: c_int64_t
       type(*), dimension(*), intent(inout) :: buf
       integer(c_int64_t), value :: n
       integer(c_int64_t) :: nread
     end function stream_stdin_read_fully
  end interface

  bLowSidelobes = .false.
  ingain        = 0
  nminw         = 1

  ! Read the framed byte stream from stdin via the C helper (stdin_read
  ! below) — no Fortran unit is opened. Binary mode must be forced before
  ! the first read on Windows or the CRT stops at 0x1A and rewrites CRLF.
  call stream_set_stdin_binary()

  call stdin_read(hdr, HDR_LEN, ios)
  if (ios /= 0) then
     call streaming_emit_error('short header (need 8 bytes)')
     write(error_unit, '(a)') 'wsjt-codec --stream: short header (need 8 bytes)'
     stop 1
  end if

  if (any(hdr(1:4) /= MAGIC)) then
     call streaming_emit_error('bad magic (expected ''WSJT'')')
     write(error_unit, '(a)') 'wsjt-codec --stream: bad magic (expected ''WSJT'')'
     stop 1
  end if

  fmt      = iand(int(hdr(5)), 255)
  ch       = iand(int(hdr(6)), 255)
  rate_khz = iand(int(hdr(7)), 255) + ishft(iand(int(hdr(8)), 255), 8)

  if (fmt /= 0) then
     call streaming_emit_error('unsupported format (only fmt=0 int16 PCM supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'wsjt-codec --stream: unsupported fmt=', fmt, ' (only int16 PCM supported)'
     stop 1
  end if

  if (ch /= 1) then
     call streaming_emit_error('unsupported channel count (only mono supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'wsjt-codec --stream: unsupported channels=', ch, ' (only mono supported)'
     stop 1
  end if

  if (rate_khz /= 12) then
     call streaming_emit_error('unsupported sample rate (only 12 kHz supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'wsjt-codec --stream: unsupported rate=', rate_khz, ' kHz (only 12 kHz supported)'
     stop 1
  end if

  write(error_unit, '(a,i0,a,i0,a,i0,a)')                                  &
       'wsjt-codec --stream: header ok (fmt=', fmt, ' ch=', ch,            &
       ' rate=', rate_khz, ' kHz)'

  nsps  = 6912
  kstep = nsps / 2

  ! Streaming-specific params override. The full params init was already
  ! performed in jt9.f90's stream-mode dispatch arm via
  ! init_default_params + init_streaming_extra_fields;
  ! all that remains here is the streaming-vs-WAV diff.
  !
  ! ndiskdat is the GUI's file input. With live_audio, FT8 bails out on the
  ! wall clock as the GUI's live decodes do (ft8_decode.f90), which assumes
  ! the stream's periods start on the UTC period boundaries.
  eme_delay_on  = .false.
  live_audio    = .false.
  pending_again = 0
  shared_data%params%ndiskdat = .not. live_audio
  shared_data%params%emedelay = merge(2.5, 0.0, eme_delay_on)

  ! Stash session-baseline nfa/nfb. apply_configure_fields
  ! restores these on mode change with no explicit consumer override —
  ! mirrors WAV semantics where each invocation starts from CLI defaults.
  baseline_nfa = shared_data%params%nfa
  baseline_nfb = shared_data%params%nfb

  call streaming_emit_ready()
  ! Command-line starting values are checked once; the session still runs.
  problem = engine_params_problem(shared_data%params, .true.)
  if (problem%field .ne. ' ') call emit_range_error(problem, no_keys, mode, .true.)
  max_audio_frame_bytes = 2_int64 * int(size(shared_data%id2), int64)
  body_left = 0
  stream_samples = 0

  ! ===== Outer period loop ===========================================
  do
     call begin_period()
     eof_period = .false.
     halt_req   = .false.

     ! The previous period ended inside an audio frame: its remainder is
     ! this period's first audio.
     if (body_left .gt. 0) call consume_audio()

     ! Inner frame loop: read frames until the period is full, halt, or EOF.
     do while (.not. period_full(period) .and. .not. halt_req .and. .not. eof_period)
        call stdin_read(type_byte, 1, ios)
        if (ios /= 0) then
           eof_period = .true.; exit
        end if
        call stdin_read(len_bytes, 4, ios)
        if (ios /= 0) then
           eof_period = .true.; exit
        end if
        frame_len_wide = iand(int(len_bytes(1), int64), int(z'FF', int64))       &
             + 256_int64 * iand(int(len_bytes(2), int64), int(z'FF', int64))     &
             + 65536_int64 * iand(int(len_bytes(3), int64), int(z'FF', int64))   &
             + 16777216_int64 * iand(int(len_bytes(4), int64), int(z'FF', int64))
        if (frame_len_wide .gt. max_audio_frame_bytes) then
           call streaming_emit_error_code('frame_too_large',                    &
                'frame length exceeds the streaming sample-buffer capacity')
           write(error_unit, '(a,i0,a,i0)') 'wsjt-codec --stream: frame length ', &
                frame_len_wide, ' exceeds capacity ', max_audio_frame_bytes
           stop 1
        end if
        frame_len = int(frame_len_wide)

        if (iand(int(type_byte(1)), 255) .eq. FRAME_AUDIO) then
           if (mod(frame_len, 2) /= 0) then
              call streaming_emit_error_code('odd_audio_frame',                &
                   'audio frame length must contain whole int16 samples')
              ! The skipped samples are a hole in the JTTY session.
              if (mode .eq. MODE_JTTY) call jtty_end(shared_data%params)
              body_left = frame_len
              do while (body_left .gt. 0)
                 take_bytes = min(body_left, size(byte_sink))
                 call stdin_read(byte_sink, take_bytes, ios)
                 if (ios /= 0) then
                    eof_period = .true.; exit
                 end if
                 body_left = body_left - take_bytes
              end do
              cycle
           end if
           body_left = frame_len
           if (mode .eq. MODE_JTTY) then
              call consume_jtty_body()
           else
              call consume_audio()
           end if

        else if (iand(int(type_byte(1)), 255) .eq. FRAME_CONTROL) then
           ! Read JSON body into char buffer (cap at CTL_BUF_LEN)
           if (frame_len .gt. CTL_BUF_LEN) then
              call streaming_emit_error_code('control_frame_too_large',        &
                   'control frame exceeds the configured buffer capacity')
              ! drain + skip
              body_left = frame_len
              do while (body_left .gt. 0)
                 take_bytes = min(body_left, size(byte_sink))
                 call stdin_read(byte_sink, take_bytes, ios)
                 if (ios /= 0) exit
                 body_left = body_left - take_bytes
              end do
              cycle
           end if
           call stdin_read(ctl_bytes, frame_len, ios)
           if (ios /= 0) then
              eof_period = .true.; exit
           end if
           ctl_buf = ' '
           do i_ctl = 1, frame_len
              ctl_buf(i_ctl:i_ctl) = achar(iand(int(ctl_bytes(i_ctl)), 255))
           end do
           call parse_control_frame(ctl_buf(1:frame_len), action, cfg, terr)
           select case (action)
           case (CTRL_HALT)
              halt_req = .true.
           case (CTRL_CONFIGURE)
              if (cfg%version_set .and. cfg%version .ne. 1) then
                 ! RFC v1 §3/§4.1: unknown schema version -> decline this frame.
                 ! Checked BEFORE per-key types: an unrecognized version means
                 ! we cannot trust the body's schema. (A wrong-TYPED version
                 ! leaves version_set=.false., so it falls through to the
                 ! type-error gate below — reported as configure_type_error
                 ! key:"version" only if version is the sole mistyped key;
                 ! otherwise the FIRST mistyped key wins, since parse records
                 ! one error and checks version last. Either way: declined.)
                 call streaming_emit_error_version(cfg%version)
                 cycle
              end if
              if (terr%present) then
                 ! RFC v1 §4.1: a key's JSON value type mismatched the schema.
                 ! Decline the ENTIRE frame — no field is applied (the apply
                 ! below is gated on reaching it; cycle skips it AND the npts/
                 ! buffer-discard logic, so a co-present valid key never takes
                 ! effect).
                 call streaming_emit_error_type(trim(terr%key),             &
                      trim(terr%expected), trim(terr%got))
                 cycle
              end if
              if (cfg%mode_invalid) then
                 call streaming_emit_error_code('unknown_mode',                 &
                      'configure frame contains an unsupported mode')
                 cycle
              end if
              if (cfg%trperiod_set .and.                                      &
                  (.not. ieee_is_finite(cfg%trperiod) .or.                     &
                   cfg%trperiod .le. 0.d0 .or.                                &
                   cfg%trperiod .gt. dble(NTMAX))) then
                 call streaming_emit_error_code('invalid_trperiod',             &
                      'trperiod must be finite and between 0 and 1800 seconds')
                 cycle
              end if
              ! Applied to a copy, so that a declined frame changes nothing.
              next_mode     = mode
              next_TRperiod = TRperiod
              next_params   = shared_data%params
              call apply_configure_fields(cfg, next_mode, next_TRperiod,   &
                   next_params, baseline_nfa, baseline_nfb)
              ! The GUI's EME delay goes with "Decode after EME delay", in every
              ! mode, over the mode policy a mode or trperiod key reapplies;
              ! eme_delay_seconds, when sent, wins.
              next_eme_on = eme_delay_on
              if (cfg%decode_after_eme_delay_set) next_eme_on = cfg%decode_after_eme_delay
              if (cfg%mode_set .or. cfg%trperiod_set .or.                     &
                   cfg%decode_after_eme_delay_set)                             &
                   next_params%emedelay = merge(2.5, 0.0, next_eme_on)
              if (cfg%eme_delay_seconds_set) next_params%emedelay =           &
                   real(cfg%eme_delay_seconds, kind(next_params%emedelay))
              if (next_mode .eq. MODE_JTTY) then
                 range_key = jtty_range_key(next_params)
                 if (len(range_key) .gt. 0) then
                    call jtty_emit_range_error(range_key)
                    cycle
                 end if
              else
                 ! A frame that would leave the decoder refusing its decodes.
                 problem = engine_params_problem(next_params, .true.)
                 if (problem%field .ne. ' ') then
                    call emit_range_error(problem, cfg, next_mode, .false.)
                    cycle
                 end if
              end if
              if (mode .eq. MODE_JTTY .and. next_mode .ne. MODE_JTTY)       &
                   call jtty_end(shared_data%params)
              prev_mode          = mode
              prev_TRperiod      = TRperiod
              mode               = next_mode
              TRperiod           = next_TRperiod
              shared_data%params = next_params
              call jtty_note_station(cfg)
              call streaming_emit_set_time_form(cfg%utc_set, cfg%nutc_set, TRperiod)
              eme_delay_on = next_eme_on
              if (cfg%live_audio_set) live_audio = cfg%live_audio
              shared_data%params%ndiskdat = .not. live_audio
              ! A configure that changes the applied mode or period starts a
              ! new period; what was in flight is discarded. Otherwise the
              ! period in flight decodes at the stop point the new settings give.
              if (mode .ne. prev_mode .or. TRperiod .ne. prev_TRperiod) then
                 call begin_period()
                 again_k = 0
              else if (.not. decoded) then
                 if (file_reads_whole()) then
                    period%npts = min(period%nperiod, size(shared_data%id2))
                 else
                    call period_set_stop(period, current_stop(), size(shared_data%id2))
                 end if
                 if (period%k .gt. period%npts) then
                    shared_data%id2(period%npts+1:period%k) = 0
                    period%k = period%npts
                 end if
              end if
              ! nagain_flag asks once for the last decoded period again.
              if (cfg%nagain_flag_set .and. cfg%nagain_flag) then
                 if (again_k .le. 0) then
                    call streaming_emit_error_code('decode_again_unavailable', &
                         'no decoded period to decode again (none yet or since a ' // &
                         'mode or period change; MSK144 and JTTY keep none)')
                 else if (mode .eq. 8 .and. ft8_done .gt. 0 .and. .not. decoded) then
                    ! Between this period's FT8 passes the decoder holds their
                    ! state; the request runs after the last pass.
                    pending_again = pending_again + 1
                 else
                    call decode_again()
                 end if
              end if
              shared_data%params%nagain = .false.
              if (mode .ne. MODE_JTTY .and. .not. decoded) then
                 call run_early_ft8(.false.)
                 if (period_ready(period)) call run_period_decode()
              end if
           case (CTRL_DISCONTINUITY)
              if (mode .eq. MODE_JTTY) call jtty_end(shared_data%params)
           case (CTRL_PACK)
              call jtty_encode(ctl_buf(1:frame_len))
           case (CTRL_PARSE_ERR)
              call streaming_emit_error_code('configure_parse_error',          &
                   'malformed control frame (missing or invalid t field)')
           end select

        else
           ! Unknown frame type: drain body
           body_left = frame_len
           do while (body_left .gt. 0)
              take_bytes = min(body_left, size(byte_sink))
              call stdin_read(byte_sink, take_bytes, ios)
              if (ios /= 0) then
                 eof_period = .true.; exit
              end if
              body_left = body_left - take_bytes
           end do
        end if
     end do

     ! A period cut short (halt, or the producer closed the pipe) still
     ! decodes what it has — once.
     if (.not. decoded .and. period%k .gt. 0) call run_period_decode()

     if (halt_req .or. eof_period) then
        if (mode .eq. MODE_JTTY) call jtty_end(shared_data%params)
        call jtty_release()
        return
     end if
  end do

contains

  ! Start a period: live, its decode at the GUI's stop point for the settings;
  ! from a file, on the whole period as the GUI's file decode reads it.
  subroutine begin_period()
    do while (pending_again .gt. 0)
       pending_again = pending_again - 1
       call streaming_emit_error_code('decode_again_unavailable',             &
            'the mode or period changed before the decode again could run')
    end do
    call period_begin(period, TRperiod, mode, size(shared_data%id2), current_stop(), &
         file_reads_whole())
    shared_data%id2 = 0
    nhsym    = 0
    nhsym0   = 0       ! seed at 0 so the symspec catch-up loop is O(nhsym)
    decoded  = .false.
    ft8_done = 0
  end subroutine begin_period

  ! From a file the GUI's decode at the stop reads the whole period, as far as
  ! the engine takes it: FT4 72576 samples, multithreaded FT8 the stop
  ! (lib/decoder_engine.f90 analysis_extent and its FT8 cut), the rest whole.
  logical function file_reads_whole()
    file_reads_whole = .not. live_audio .and. mode .ne. 5 .and.              &
         .not. (mode .eq. 8 .and. shared_data%params%lmultift8 .and. .not. superfox_hound())
  end function file_reads_whole

  integer function current_stop()
    current_stop = 0
    if (mode .eq. MODE_JTTY) return
    current_stop = gui_hsym_stop(mode, TRperiod, eme_delay_on,               &
         shared_data%params%lmultift8 .and. .not. superfox_hound(),          &
         shared_data%params%ndecoderstart)
  end function current_stop

  ! The audio frame's samples into the period, decoding at the stop point
  ! even inside the frame, then the rest of the frame up to the period's end.
  subroutine consume_audio()
    call consume_audio_body()
    if (.not. decoded .and. period_ready(period)) then
       call run_period_decode()
       call consume_audio_body()
    end if
  end subroutine consume_audio

  ! The FT8 passes the GUI runs before its stop point, as half-symbols, and
  ! whether the second is at depth 2 (widgets/mainwindow.cpp dataSink and
  ! decode(), lib/jt9a.f90). Live, the multithreaded setting picks them, with
  ! SuperFox Hound's standard final too: 41 at decoder starts 0 and 1, then 46
  ! at depth 2 at start 1, none from start 2; otherwise 41 and 47. From a file,
  ! jt9a's: 41 and 46 below start 2 for the multithreaded decoder, else 41
  ! and 47. On SuperFox's even sequence the GUI decodes nothing before 50.
  subroutine ft8_early_schedule(h, n, depth2)
    integer, intent(out) :: h(2), n
    logical, intent(out) :: depth2
    h = 0
    n = 0
    depth2 = .false.
    if (superfox_even()) then
       if (live_audio .or. current_stop() .lt. 50) return
    end if
    if (live_audio .and. shared_data%params%lmultift8) then
       if (shared_data%params%ndecoderstart .eq. 0) then
          h(1) = 41
          n = 1
       else if (shared_data%params%ndecoderstart .eq. 1) then
          h = [41, 46]
          n = 2
          depth2 = .true.
       end if
    else if (.not. live_audio .and. shared_data%params%lmultift8 .and.       &
         .not. superfox_hound()) then
       if (shared_data%params%ndecoderstart .lt. 2) then
          h = [41, 46]
          n = 2
       end if
    else
       h = [41, 47]
       n = 2
    end if
  end subroutine ft8_early_schedule

  ! The GUI's SuperFox is the setting with Fox or Hound (widgets/mainwindow.cpp).
  logical function superfox_even()
    integer :: contest
    contest = iand(shared_data%params%nexp_decode, 7)
    superfox_even = shared_data%params%b_superfox .and. shared_data%params%b_even_seq &
         .and. (contest .eq. 6 .or. contest .eq. 7)
  end function superfox_even

  ! The sample count at which the period's next pass is due.
  integer function next_pass_samples()
    integer :: h(2), n, i
    logical :: depth2
    next_pass_samples = period%npts
    if (mode .ne. 8) return
    call ft8_early_schedule(h, n, depth2)
    do i = 1, n
       if (h(i) .gt. ft8_done .and. h(i) * HSYM_SAMPLES .gt. period%k) then
          next_pass_samples = min(next_pass_samples, h(i) * HSYM_SAMPLES)
          return
       end if
    end do
  end function next_pass_samples

  ! Run each FT8 pass before the stop point whose audio is in, or with
  ! every, each one still to run (a period cut short decodes what it has).
  subroutine run_early_ft8(every)
    logical, intent(in) :: every
    integer :: h(2), n, i, ndepth0, ncycles0
    logical :: depth2, requested
    if (mode .ne. 8 .or. decoded) return
    call ft8_early_schedule(h, n, depth2)
    do i = 1, n
       if (h(i) .le. ft8_done) cycle
       if (.not. every .and. period%k .lt. h(i) * HSYM_SAMPLES) exit
       shared_data%params%newdat = .true.
       shared_data%params%nagain = .false.
       requested = shared_data%params%lmultift8
       ndepth0   = shared_data%params%ndepth
       ncycles0  = shared_data%params%nft8cycles
       ! The GUI's live early stages of the multithreaded decoder.
       if (requested .and. live_audio) then
          shared_data%params%nft8cycles = 2
          if (i .eq. 2 .and. depth2) shared_data%params%ndepth = 2
       end if
       shared_data%params%lmultift8 = .false.
       ! Live, the decoder completes each pass, as for the GUI; the period's
       ! decode_finished waits for its last.
       call streaming_emit_hold_finished(.true.)
       call ft8_window_pass(h(i))
       call streaming_emit_hold_finished(.false.)
       shared_data%params%lmultift8  = requested
       shared_data%params%ndepth     = ndepth0
       shared_data%params%nft8cycles = ncycles0
       ft8_done = h(i)
    end do
  end subroutine run_early_ft8

  ! This period's decode, on the `npts` samples in `id2`. Runs once per
  ! period: when the decoder's share is in, or from the drain when the
  ! period ends short of it.
  subroutine run_period_decode()
    integer :: nstop
    logical :: requested, multithreaded, at_stop
    nstop = current_stop()
    ! The GUI's nzhsym is its stop point, for a decode at it, live or from a
    ! file; a period cut short decodes what it has. FT8 is tuned for its stop
    ! (jt9.f90:552).
    at_stop = period_ready(period) .and. nstop .gt. 0 .and.                 &
         nstop * HSYM_SAMPLES .le. period%nperiod
    if (mode .eq. 8 .and. superfox_even() .and. nstop .lt. 50) then
       ! On SuperFox's even sequence the GUI decodes nothing before 50.
       decoded = .true.
       call run_pending_again()
       return
    end if
    call run_early_ft8(.true.)
    decoded = .true.
    shared_data%params%newdat = .true.
    shared_data%params%nagain = .false.
    shared_data%params%ntr = int(TRperiod)
    shared_data%params%nzhsym = nhsym
    if (at_stop) shared_data%params%nzhsym = nstop
    if (mode .eq. 8 .and. nstop .gt. 0) shared_data%params%nzhsym = nstop
    ! Non-FT8/FST4/Q65: cap at 181 to match WAV-path exit (ss array bound).
    if (mode .ne. 8   .and. mode .ne. 240 .and. mode .ne. 241 .and.    &
        mode .ne. 242 .and. mode .ne. 66  .and. shared_data%params%nzhsym .gt. 181) then
       shared_data%params%nzhsym = 181
    end if
    shared_data%params%kin    = 64800
    if (mode .eq. 4 .or. mode .eq. 5 .or. mode .eq. 9 .or. mode .eq. 65 .or. mode .eq. 66 .or. &
         mode .eq. 240 .or. mode .eq. 241 .or. mode .eq. 242) &
         shared_data%params%kin = period%k

    requested = shared_data%params%lmultift8
    multithreaded = requested .and. .not. superfox_hound()
    if (mode .eq. 144) then
       call report_pass(period%k)
       call decode_msk144(shared_data%id2, shared_data%params, data_dir)
    else if (mode .eq. 8 .and. .not. multithreaded) then
       ! The final pass of the standard decoder's sequence; the early passes
       ! fill ft8_decode.f90's saved `dd`, without which it decodes silence.
       ! Live it sees the audio up to the stop; from a file the GUI's final
       ! reads the whole period (lib/jt9a.f90).
       shared_data%params%lmultift8 = .false.
       if (live_audio) then
          call ft8_window_pass(shared_data%params%nzhsym)
       else
          call report_pass(period%k)
          call multimode_decoder(shared_data%ss, shared_data%id2,      &
               shared_data%params, NFSAMPLE)
       end if
       shared_data%params%lmultift8 = requested
    else if (mode .eq. 8) then
       shared_data%params%lmultift8 = .true.
       call report_pass(period%k)
       call multimode_decoder(shared_data%ss, shared_data%id2,         &
            shared_data%params, NFSAMPLE)
    else
       call report_pass(period%k)
       call multimode_decoder(shared_data%ss, shared_data%id2,         &
            shared_data%params, NFSAMPLE)
    end if
    if(mode==66) shared_data%params%nclearave=.false.
    call run_pending_again()
    if (mode .eq. 144 .or. mode .eq. MODE_JTTY) then
       again_k = 0
    else
       call keep(again_id2, shared_data%id2, period%k)
       ! Of the stream's modes only JT65+JT9 decodes from the spectra.
       if (mode .eq. 74) then
          if (.not. allocated(again_ss)) allocate(again_ss(184,NSMAX))
          again_ss  = shared_data%ss
       end if
       again_k      = period%k
       again_nzhsym = shared_data%params%nzhsym
       again_kin    = shared_data%params%kin
       again_nutc   = shared_data%params%nutc
       again_even   = shared_data%params%b_even_seq
       again_iso    = streaming_emit_utc_from_iso()
    end if
  end subroutine run_period_decode

  ! The GUI's Decode button: one pass with nagain over the last decoded
  ! period, with its time and sequence, the period in flight set aside meanwhile.
  subroutine decode_again()
    integer :: k_now, nutc_now, nzhsym_now, kin_now
    logical :: requested, even_now, iso_now
    k_now    = period%k
    nutc_now = shared_data%params%nutc
    nzhsym_now = shared_data%params%nzhsym
    kin_now    = shared_data%params%kin
    even_now = shared_data%params%b_even_seq
    iso_now  = streaming_emit_utc_from_iso()
    requested = shared_data%params%lmultift8
    call keep(held_id2, shared_data%id2, k_now)
    if (mode .eq. 74) then
       if (.not. allocated(held_ss)) allocate(held_ss(184,NSMAX))
       held_ss = shared_data%ss
       shared_data%ss = again_ss
    end if
    shared_data%id2(1:k_now) = 0
    shared_data%id2(1:again_k) = again_id2(1:again_k)
    shared_data%params%nagain = .true.
    shared_data%params%newdat = .false.
    shared_data%params%nzhsym = again_nzhsym
    shared_data%params%kin    = again_kin
    shared_data%params%nutc   = again_nutc
    shared_data%params%b_even_seq = again_even
    call streaming_emit_set_time_form(again_iso, .not. again_iso, TRperiod)
    if (superfox_hound()) shared_data%params%lmultift8 = .false.
    ! The single-threaded FT8 decoder decodes again only on the hsym-50 pass.
    if (mode .eq. 8 .and. .not. shared_data%params%lmultift8)              &
         shared_data%params%nzhsym = 50
    call report_pass(again_k)
    call multimode_decoder(shared_data%ss, shared_data%id2, shared_data%params, NFSAMPLE)
    if (mode .eq. 66) shared_data%params%nclearave = .false.
    shared_data%params%lmultift8 = requested
    shared_data%params%nagain = .false.
    shared_data%params%newdat = .true.
    shared_data%params%nzhsym = nzhsym_now
    shared_data%params%kin    = kin_now
    shared_data%params%nutc   = nutc_now
    shared_data%params%b_even_seq = even_now
    call streaming_emit_set_time_form(iso_now, .not. iso_now, TRperiod)
    shared_data%id2(1:again_k) = 0
    shared_data%id2(1:k_now) = held_id2(1:k_now)
    if (mode .eq. 74) shared_data%ss = held_ss
  end subroutine decode_again

  ! A decode-again request held for the period's last FT8 pass, now it has run.
  subroutine run_pending_again()
    do while (pending_again .gt. 0)
       pending_again = pending_again - 1
       call decode_again()
    end do
  end subroutine run_pending_again

  ! SuperFox Hound decodes single-threaded, as the GUI does.
  logical function superfox_hound()
    superfox_hound = iand(shared_data%params%nexp_decode, 7) .eq. 7 .and.   &
         shared_data%params%b_superfox
  end function superfox_hound

  ! copy(1:n) = source(1:n), growing copy as needed.
  subroutine keep(copy, source, n)
    integer(int16), allocatable, intent(inout) :: copy(:)
    integer(int16), intent(in) :: source(:)
    integer, intent(in) :: n
    if (allocated(copy)) then
       if (size(copy) .lt. n) deallocate(copy)
    end if
    if (.not. allocated(copy)) allocate(copy(max(n, 1)))
    copy(1:n) = source(1:n)
  end subroutine keep

  ! configure_range_error for a refused engine option: the key, the range in
  ! the mode, and where a value this frame did not set came from. A change
  ! of mode or period restores nfa and nfb from the command line.
  subroutine emit_range_error(problem, frame, in_mode, at_start)
    type(option_problem),   intent(in) :: problem
    type(configure_fields), intent(in) :: frame
    integer,                intent(in) :: in_mode
    logical,                intent(in) :: at_start
    character(len=:), allocatable :: key, name, detail
    logical :: carried
    key = trim(engine_range_key(problem%field, frame, carried))
    name = trim(streaming_emit_mode_name(in_mode))
    if (problem%field .eq. 'search_range') then
       detail = trim(problem%limit) // ' in ' // name
    else if (key .eq. 'depth') then
       ! The decoders read only the depth's low bits.
       detail = 'depth must hold a decoding depth ' // trim(problem%limit) // ' in ' // name
    else
       detail = key // ' must be ' // trim(problem%limit) // ' in ' // name
    end if
    if (at_start) then
       detail = detail // '; the command line set it'
    else if (carried .and. (key .eq. 'nfa' .or. key .eq. 'nfb') .and.            &
         (frame%mode_set .or. frame%trperiod_set)) then
       detail = detail // '; a change of mode or period restores it from the command line'
    else if (carried) then
       detail = detail // '; it was set before this frame'
    end if
    call streaming_emit_error_range(key, name, detail)
  end subroutine emit_range_error

  ! One FT8 pass over the first nz half-symbols, the rest zeroed.
  subroutine ft8_window_pass(nz)
    integer, intent(in) :: nz
    shared_data%params%nzhsym = nz
    id2a(1:nz*3456) = shared_data%id2(1:nz*3456)
    id2a(nz*3456+1:) = 0
    call report_pass(period%k)
    call multimode_decoder(shared_data%ss, id2a, shared_data%params, NFSAMPLE)
  end subroutine ft8_window_pass

  ! Each decode pass on stderr: the samples in when it ran, and how.
  subroutine report_pass(samples)
    integer, intent(in) :: samples
    character(len=16) :: delay
    write(delay, '(f16.1)') shared_data%params%emedelay
    write(error_unit, '(a,i0,a,i0,a,i0,a,i0,a,l1,a,l1,a,l1,2a)')            &
         'wsjt-codec --stream: pass nzhsym=', shared_data%params%nzhsym,     &
         ' samples=', samples, ' depth=', shared_data%params%ndepth,         &
         ' cycles=', shared_data%params%nft8cycles,                          &
         ' multithreaded=', shared_data%params%lmultift8,                    &
         ' live=', .not. shared_data%params%ndiskdat,                        &
         ' again=', shared_data%params%nagain,                               &
         ' emedelay=', trim(adjustl(delay))
  end subroutine report_pass

  ! Stop at the period boundary; body_left carries the remaining frame
  ! forward.
  subroutine consume_audio_body()
    integer :: k0, nacc
    do while (body_left .gt. 0 .and. .not. period_full(period) .and.       &
         (decoded .or. .not. period_ready(period)))
       take_samples = period_room(period, min(body_left / 2, size(chunk)))
       ! The read ends at the next pass's sample, so that pass runs on time.
       if (.not. decoded) take_samples = min(take_samples, max(1, next_pass_samples() - period%k))
       take_bytes   = 2 * take_samples
       call stdin_read(chunk, take_bytes, ios)
       if (ios /= 0) then
          eof_period = .true.; return
       end if
       body_left = body_left - take_bytes
       stream_samples = stream_samples + take_samples
       k0 = period%k
       call period_take(period, take_samples, nacc)
       if (nacc .gt. 0) then
          shared_data%id2(k0+1 : k0+nacc) = chunk(1:nacc)
          ! Incremental symspec for JT9-family at kstep boundaries.
          ! Cap at nhsym=181 to match the WAV-path exit condition
          ! (jt9.f90:442-443) — exceeding it overruns ss(184,*).
          do while ((period%k - 2048) / kstep .gt. nhsym0 .and. nhsym0 .lt. 181)
             nhsym0 = nhsym0 + 1
             if (nhsym0 .lt. 1) cycle
             nhsym = nhsym0
             if (mode .eq. 74) then
                call timer('symspec ', 0)
                call symspec(shared_data, nhsym*kstep + 2048, nsps,        &
                     ingain, bLowSidelobes, nminw, pxdb, s, df3,           &
                     ihsym, npts8, pxdbmax, npct_unused)
                call timer('symspec ', 1)
             end if
          end do
          ! The GUI's early FT8 passes run as soon as their audio is in.
          if (mode .eq. 8) call run_early_ft8(.false.)
       end if
    end do
  end subroutine consume_audio_body

  ! JTTY has no periods: the whole frame goes to the session.
  subroutine consume_jtty_body()
    do while (body_left .gt. 0)
       take_samples = min(body_left / 2, size(chunk))
       take_bytes   = 2 * take_samples
       call stdin_read(chunk, take_bytes, ios)
       if (ios /= 0) then
          eof_period = .true.; return
       end if
       body_left = body_left - take_bytes
       call jtty_audio(chunk(1:take_samples), stream_samples, shared_data%params)
       stream_samples = stream_samples + take_samples
    end do
  end subroutine consume_jtty_body

  ! Read exactly nbytes from stdin via the C helper; ios=0 on success,
  ! ios=1 on end-of-stream (short delivery — the producer closed the
  ! pipe). Every caller treats nonzero ios as end-of-stream, matching
  ! the old unformatted-stream-read iostat contract.
  subroutine stdin_read(buf, nbytes, ios)
    type(*), dimension(*), intent(inout) :: buf
    integer, intent(in)  :: nbytes
    integer, intent(out) :: ios
    ios = 0
    if (stream_stdin_read_fully(buf, int(nbytes, c_int64_t)) /=          &
         int(nbytes, c_int64_t)) ios = 1
  end subroutine stdin_read

end subroutine jt9_stream

logical function jt9_stream_available()
  jt9_stream_available = .true.
end function jt9_stream_available
