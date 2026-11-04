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
! {"t":"flush"}, {"t":"discontinuity"}, and the encode requests
! {"t":"pack",...} and {"t":"render",...} (lib/streaming_encode.f90), answered
! in any mode. See lib/streaming_control.f90 for the parser + schema.
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
!               once, after ready. JT9 decodes only at a trperiod of 60:
!               a configure that would leave it at another is declined
!               with configure_range_error (key trperiod), and a session
!               the command line starts so (-9 and a -p other than 60)
!               sends that error instead of ready and exits with status 2.
!   halt      : drain the current period (decode if it has samples), exit.
!   flush     : as halt, without ending the session: a final deferred
!               while the decoder was busy runs, the period in flight
!               decodes now, once, if it has samples, and the next audio
!               sample starts a new period. For a WAV file's last, short
!               period, or a decode of what has come in. It runs the
!               period's remaining passes at their usual half-symbols on
!               the audio that is in, so a decode's "pass" is that
!               half-symbol, not how much audio there was. In JTTY it ends
!               the JTTY session, and the stream goes on.
!
! A period is TRperiod * 12 kHz samples (lib/streaming_period.f90). Keep a
! frame remainder for the next period; retain only the first npts samples
! for decoding. A period starts with the session's first audio sample, and
! with the first after a configure that changes the mode or the period (a
! configure that only labels does not), a discontinuity or a flush. Send
! every sample.
!
! Labels: each period's nutc or utc (lib/streaming_control.f90) goes in a
! configure after the previous period's last sample and before this
! period's first decode pass (MSK144: before its first 7,168 samples); split
! an audio frame at the period boundary to put it there. The label in force
! when a pass runs labels its decodes and its decode_finished, and it is
! decoder input: a change of label starts a new decoder input, so FT8's
! next pass forgets the period's earlier passes and sends their decodes
! again, and JT65 averages odd and even minutes apart by it. The GUI labels
! JT9, JT65, JT65+JT9 and JT4 periods of a minute or more HHMM, the others
! HHMMSS (widgets/mainwindow.cpp), and each decoder here gets the form it
! gets from the GUI whichever form the client sends: JT9, JT65 and JT65+JT9
! decode with HHMM, JT4 with HHMM00; their time and period_end are HHMM00.
! A nutc is kept as it was sent, and a later mode reads it again, except
! that JT4, Q65 and FST4 periods of a minute or more fix an HHMM one on
! arrival (times 100); so send a label with every period, after any mode
! change. A configure whose nutc or utc is not a time of day is declined
! (lib/streaming_control.f90), so every label in force is one.
!
! Live audio or a file (configure's live_audio, false at the start): from a
! file each period decodes on its whole audio once the period is in (FT4
! and multithreaded FT8 at their stop points; FT8's early passes still run),
! no pass is ever skipped and nothing reads the clock, and a flush decodes a
! file's last, short period. Live, each period decodes at the GUI's stop
! points for the mode (lib/streaming_period.f90), a pass the GUI's busy
! decoder would not run is skipped (pass_skipped), and FT8's passes stop on
! the wall clock as the GUI's live passes do (lib/ft8_decode.f90): pass 41
! when the UTC 15 s period is 13.4 s in, and pass 47's subtraction step at
! 14.3 s; the final pass is not cut short. So a live client streams in real
! time, starts each period on its UTC boundary (a discontinuity there
! re-anchors it, below), and labels it; audio out of step with UTC loses FT8
! decodes. A final the busy decoder cannot run is deferred only by the
! multithreaded FT8 decoder, as the GUI's is, and runs when the decoder comes
! free unless a later final replaces it first (pass_skipped "replaced");
! every other mode but MSK144 skips it ("busy").
!
! MSK144 decodes each 3584-sample block of a period as soon as it is read,
! as the GUI does (lib/decode_msk144.f90), and only in the period's first 30 s,
! the GUI's longest MSK144 period. The period's end, or a halt or the end of
! input within it, sends its decode_finished; a partial block is never
! decoded. Each decode carries the label (utc or nutc) in force when its block
! is decoded, so a period's label belongs before its first 7,168 samples; a
! label changed within the period resets the decoder's duplicate memory and
! may send that period's pings again. A configure that enters MSK144 or JTTY
! starts it at once, as the GUI does: the previous mode's remaining decode
! lines can follow the new mode's (group them by period_end).
!
! Discontinuity: in the period modes it re-anchors the period, as the GUI
! starts a new input at each UTC period and on a restart. Every pass whose
! audio is in has run, and a deferred final still runs; a period in flight
! that has not reached its stop (from a file, the whole period, or FT4's and
! multithreaded FT8's stop) ends without its decode or decode_finished (the
! decodes its early FT8 passes or MSK144 blocks sent stand); and the next
! audio sample starts a new period, its arrival restarting the busy clock.
! Decode-again requests waiting for the period's last FT8 pass run first. At
! a period's start it changes nothing.
!
! JTTY ({"mode":"JTTY"}; no integer code or command-line flag selects it) has
! no periods: its audio goes to lib/streaming_jtty.f90, which states its
! output. A JTTY session ends at a discontinuity, a flush, an odd-length
! audio frame, a configure that leaves JTTY, halt and end of input. Entering
! JTTY, rxfreq carries over, ntol becomes 20 and nfa and nfb their values at
! the start of the stream (-L and -H, 200 and 4000 Hz by default; with -9
! and an -S other than 2700, nfa is -S) unless the frame sets them. Any
! configure that carries mode or trperiod and stays in JTTY keeps the
! session but resets in the same way whichever of nfa, nfb and ntol it does
! not set. trperiod is validated and unused. lib/streaming_jtty.f90 states
! which frequency values a configure in JTTY is declined for
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
! stream_set_stdin_binary (lib/stream_setmode.c; no-op on POSIX). On every
! platform a reader thread drains fd 0 while the stream decodes; a request
! returns short only at the end of input, so a pipe short-read is never
! mistaken for EOF.
!
! Run the codec with OMP_STACKSIZE set as the GUI sets it for its decoder
! (10M). Without it the decoders' OpenMP threads get the default stack, and
! on Linux each one's copy of the decoders' thread-local arrays (about 6 MB)
! comes out of that, leaving about 2 MiB of the usual 8 MiB.

subroutine jt9_stream(shared_data, mode, TRperiod)
  use, intrinsic :: iso_fortran_env, only: int8, int16, int64, error_unit
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use, intrinsic :: iso_c_binding, only: c_int64_t, c_double
  use prog_args, only: data_dir
  use streaming_emit, only: streaming_emit_ready, streaming_emit_error,    &
       streaming_emit_error_code, streaming_emit_error_version,            &
       streaming_emit_error_type, time_form, streaming_emit_period_finished, &
       streaming_emit_error_range, streaming_emit_mode_name,               &
       streaming_emit_set_clock, streaming_emit_reply,                     &
       streaming_emit_capture, streaming_emit_captured
  use streaming_control, only: parse_control_frame, configure_fields,      &
       control_type_error, CTRL_CONFIGURE, CTRL_HALT, CTRL_PARSE_ERR,      &
       CTRL_DISCONTINUITY, CTRL_PACK, CTRL_RENDER, CTRL_FLUSH, MODE_JTTY
  use streaming_apply, only: apply_configure_fields, engine_range_key
  use decoder_engine, only: engine_params_problem, option_problem
  use streaming_jtty, only: jtty_audio, jtty_end, jtty_range_key,          &
       jtty_emit_range_error, jtty_release, jtty_note_station
  use streaming_encode, only: stream_encode
  use streaming_period, only: period_state, period_begin, period_room,   &
       period_take, period_full, period_ready, period_set_stop,           &
       gui_hsym_stop, HSYM_SAMPLES, clock_start
  use streaming_apply, only: ft8_settings_change, apply_msk144_fields
  use streaming_decode, only: decode_op, decode_submit, decode_drain,      &
       snapshot, superfox_even, superfox_hound, backpressure, hhmm_label,  &
       report_pass_line, OP_PASS, OP_CONFIGURE, OP_AGAIN, OP_DISCONTINUITY, &
       OP_FREE, OP_END, OP_LINE, OP_STOP, EARLY_ONE, EARLY_TWO, FINAL_STAGE, &
       NO_DECODED_PERIOD, decode_start, decode_threaded, decode_join,      &
       decode_read, decode_room, decode_wait, decode_has_work
  use msk144_stream, only: msk144_settings, msk144_window, MSK144_BLOCK

  include 'jt9com.f90'

  type(dec_data) :: shared_data
  integer        :: mode
  real(8)        :: TRperiod

  integer, parameter :: HDR_LEN = 8
  integer, parameter :: NFSAMPLE = 12000
  integer, parameter :: FRAME_AUDIO   = 1
  integer, parameter :: FRAME_CONTROL = 2
  integer, parameter :: CTL_BUF_LEN = 262144
  ! The stream has slow JT9's decoder only, not fast JT9's.
  character(len=*), parameter :: JT9_PERIOD_RULE = 'trperiod must be 60 s in JT9'
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
  ! This period's decode has been triggered.
  logical :: decoded
  ! The last FT8 pass before the stop point already triggered this period,
  ! as the half-symbol it covers (0: none).
  integer :: ft8_done
  ! The GUI's "Decode after EME delay", and live audio rather than a file.
  logical :: eme_delay_on, live_audio
  integer :: ingain, nminw, ihsym, npts8
  real    :: pxdb, df3, pxdbmax, s(NSMAX)
  integer :: npct_unused
  integer :: body_left, take_bytes, take_samples
  integer(int16) :: chunk(4096)
  integer(int8)  :: byte_sink(4096)
  character(len=CTL_BUF_LEN), save :: ctl_buf     !Keep this big buffer off the stack
  integer(int8), save :: ctl_bytes(CTL_BUF_LEN)
  integer :: i_ctl, prev_mode, next_mode
  real(8) :: prev_TRperiod, next_TRperiod
  type(params_block) :: next_params
  character(len=:), allocatable :: range_key, range_mode
  type(option_problem) :: problem
  type(configure_fields) :: no_keys
  logical :: next_eme_on, next_live, cancels
  ! Audio samples consumed in any mode; JTTY's origin and times count them.
  integer(int64) :: stream_samples
  ! Session-baseline nfa/nfb stashed after jt9.f90's init_streaming_extra_fields
  ! returned. apply_configure_fields restores these on mode change with no
  ! explicit consumer override; passed in as dummy args.
  integer :: baseline_nfa, baseline_nfb
  ! The period label as the client gave it, and whether as utc; each decoder
  ! gets it in the form the GUI gives that decoder, label_form.
  integer :: label_nutc
  logical :: label_iso
  type(time_form) :: label_form
  logical :: bLowSidelobes, eof_period, halt_req
  type(configure_fields)   :: cfg
  type(control_type_error) :: terr
  integer :: action
  type(decode_op) :: op
  ! Periods begun.
  integer(int64) :: period_number
  ! Live audio's busy decoder, the GUI's at a decode trigger: a trigger fires
  ! when its last byte arrived or its last sample is due, if later (stream
  ! sample k is due at t0 + k/12000; t0 is set by the first sample's arrival,
  ! and the first after a discontinuity, and moves only earlier, read_audio),
  ! and the decoder is busy until the last decode ended, and while a final is
  ! deferred (lib/streaming_decode.f90).
  ! ending: halt or end of input, where no pass is skipped.
  real(8) :: t0
  logical :: anchored, ending
  ! MSK144's receive options and calls, as accepted configures set them.
  type(msk144_settings) :: msk144

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
     function stream_stdin_now() result(t) bind(C, name="stream_stdin_now")
       import :: c_double
       real(c_double) :: t
     end function stream_stdin_now
     function stream_stdin_arrival() result(t) bind(C, name="stream_stdin_arrival")
       import :: c_double
       real(c_double) :: t
     end function stream_stdin_arrival
     function stream_stdin_waited() result(w) bind(C, name="stream_stdin_waited")
       import :: c_double
       real(c_double) :: w
     end function stream_stdin_waited
  end interface

  bLowSidelobes = .false.
  ingain        = 0
  nminw         = 1

  ! Read the framed byte stream from stdin via the C helper (stdin_read
  ! below) — no Fortran unit is opened. Binary mode must be forced before
  ! the first read on Windows or the CRT stops at 0x1A and rewrites CRLF.
  call stream_set_stdin_binary()

  ! Every stdin_read reads the busy model's state.
  call streaming_emit_set_clock(stream_stdin_now)
  period_number = 0
  t0 = 0
  anchored = .false.
  ending = .false.

  call stdin_read(hdr, HDR_LEN, ios)
  if (ios /= 0) then
     call streaming_emit_error('short header (need 8 bytes)')
     write(error_unit, '(a)') 'wsjt-codec: short header (need 8 bytes)'
     stop 1
  end if

  if (any(hdr(1:4) /= MAGIC)) then
     call streaming_emit_error('bad magic (expected ''WSJT'')')
     write(error_unit, '(a)') 'wsjt-codec: bad magic (expected ''WSJT'')'
     stop 1
  end if

  fmt      = iand(int(hdr(5)), 255)
  ch       = iand(int(hdr(6)), 255)
  rate_khz = iand(int(hdr(7)), 255) + ishft(iand(int(hdr(8)), 255), 8)

  if (fmt /= 0) then
     call streaming_emit_error('unsupported format (only fmt=0 int16 PCM supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'wsjt-codec: unsupported fmt=', fmt, ' (only int16 PCM supported)'
     stop 1
  end if

  if (ch /= 1) then
     call streaming_emit_error('unsupported channel count (only mono supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'wsjt-codec: unsupported channels=', ch, ' (only mono supported)'
     stop 1
  end if

  if (rate_khz /= 12) then
     call streaming_emit_error('unsupported sample rate (only 12 kHz supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'wsjt-codec: unsupported rate=', rate_khz, ' kHz (only 12 kHz supported)'
     stop 1
  end if

  write(error_unit, '(a,i0,a,i0,a,i0,a)')                                  &
       'wsjt-codec: header ok (fmt=', fmt, ' ch=', ch,                     &
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
  ! the stream's periods start on the UTC period boundaries, and a pass the
  ! GUI's busy decoder would not run is skipped (pass_skipped).
  eme_delay_on  = .false.
  live_audio    = .false.
  shared_data%params%ndiskdat = .not. live_audio
  shared_data%params%emedelay = merge(2.5, 0.0, eme_delay_on)

  ! Stash session-baseline nfa/nfb. apply_configure_fields
  ! restores these on mode change with no explicit consumer override —
  ! mirrors WAV semantics where each invocation starts from CLI defaults.
  baseline_nfa = shared_data%params%nfa
  baseline_nfb = shared_data%params%nfb
  label_nutc = shared_data%params%nutc
  label_iso = .false.

  if (mode .eq. 9 .and. TRperiod .ne. 60.d0) then
     call streaming_emit_error_range('trperiod', 'JT9',                    &
          JT9_PERIOD_RULE // '; the command line set it')
     write(error_unit, '(a)') 'wsjt-codec: ' // JT9_PERIOD_RULE // '; the command line set it'
     stop 2
  end if
  call decode_start()
  call streaming_emit_ready()
  ! Command-line starting values are checked once; the session still runs.
  problem = engine_params_problem(shared_data%params, .true.)
  if (problem%field .ne. ' ') then
     call route_begin()
     call emit_range_error(problem, no_keys, mode, .true.)
     call route_end()
  end if
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
           call fatal_begin()
           call streaming_emit_error_code('frame_too_large',                    &
                'frame length exceeds the streaming sample-buffer capacity')
           call fatal_end()
           write(error_unit, '(a,i0,a,i0)') 'wsjt-codec: frame length ', &
                frame_len_wide, ' exceeds capacity ', max_audio_frame_bytes
           stop 1
        end if
        frame_len = int(frame_len_wide)

        if (iand(int(type_byte(1)), 255) .eq. FRAME_AUDIO) then
           if (mod(frame_len, 2) /= 0) then
              call route_begin()
              call streaming_emit_error_code('odd_audio_frame',                &
                   'audio frame length must contain whole int16 samples')
              call route_end()
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
              call route_begin()
              call streaming_emit_error_code('control_frame_too_large',        &
                   'control frame exceeds the configured buffer capacity')
              call route_end()
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
                 call route_begin()
                 call streaming_emit_error_version(cfg%version)
                 call route_end()
                 cycle
              end if
              if (cfg%phase_eq_malformed) then
                 call route_begin()
                 call streaming_emit_error_code('configure_parse_error',          &
                      'phase_eq is not well-formed JSON')
                 call route_end()
                 cycle
              end if
              if (terr%present) then
                 ! RFC v1 §4.1: a key's JSON value type mismatched the schema.
                 ! Decline the ENTIRE frame — no field is applied (the apply
                 ! below is gated on reaching it; cycle skips it AND the npts/
                 ! buffer-discard logic, so a co-present valid key never takes
                 ! effect).
                 call route_begin()
                 call streaming_emit_error_type(trim(terr%key),             &
                      trim(terr%expected), trim(terr%got))
                 call route_end()
                 cycle
              end if
              if (cfg%mode_invalid) then
                 call route_begin()
                 call streaming_emit_error_code('unknown_mode',                 &
                      'configure frame contains an unsupported mode')
                 call route_end()
                 cycle
              end if
              if (cfg%trperiod_set .and.                                      &
                  (.not. ieee_is_finite(cfg%trperiod) .or.                     &
                   cfg%trperiod .le. 0.d0 .or.                                &
                   cfg%trperiod .gt. dble(NTMAX))) then
                 call route_begin()
                 call streaming_emit_error_code('invalid_trperiod',             &
                      'trperiod must be finite and between 0 and 1800 seconds')
                 call route_end()
                 cycle
              end if
              ! Applied to a copy, so that a declined frame changes nothing.
              next_mode     = mode
              next_TRperiod = TRperiod
              next_params   = shared_data%params
              call apply_configure_fields(cfg, next_mode, next_TRperiod,   &
                   next_params, baseline_nfa, baseline_nfb)
              ! JTTY's range errors carry no mode.
              range_mode = ''
              if (next_mode .ne. MODE_JTTY)                                      &
                   range_mode = trim(streaming_emit_mode_name(next_mode))
              if (next_mode .eq. 9 .and. next_TRperiod .ne. 60.d0) then
                 call route_begin()
                 call streaming_emit_error_range('trperiod', 'JT9', JT9_PERIOD_RULE)
                 call route_end()
                 cycle
              end if
              if (cfg%phase_eq_out_of_range) then
                 call route_begin()
                 call streaming_emit_error_range('phase_eq', range_mode,          &
                      'phase_eq must be five finite numbers')
                 call route_end()
                 cycle
              end if
              if (cfg%nutc_out_of_range .or. cfg%utc_out_of_range) then
                 range_key = 'nutc'
                 if (cfg%utc_out_of_range) range_key = 'utc'
                 call route_begin()
                 if (len(range_mode) .gt. 0) then
                    call streaming_emit_error_range(range_key, range_mode,       &
                         range_key // ' must be a valid time of day in ' // range_mode)
                 else
                    call streaming_emit_error_range(range_key, '',               &
                         range_key // ' must be a valid time of day')
                 end if
                 call route_end()
                 cycle
              end if
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
                    call route_begin()
                    call jtty_emit_range_error(range_key)
                    call route_end()
                    cycle
                 end if
              else
                 ! A frame that would leave the decoder refusing its decodes.
                 problem = engine_params_problem(next_params, .true.)
                 if (problem%field .ne. ' ') then
                    call route_begin()
                    call emit_range_error(problem, cfg, next_mode, .false.)
                    call route_end()
                    cycle
                 end if
              end if
              ! Where the GUI cancels its multithreaded FT8 backpressure, the
              ! stream does too, dropping a waiting final; the dropped final's
              ! period ends first, under its own labels.
              next_live = live_audio
              if (cfg%live_audio_set) next_live = cfg%live_audio
              cancels = next_mode .ne. mode .or. next_TRperiod .ne. TRperiod .or. &
                   (next_live .neqv. live_audio) .or.                          &
                   ft8_settings_change(shared_data%params, next_params)
              if (mode .eq. MODE_JTTY .and. next_mode .ne. MODE_JTTY)       &
                   call jtty_end(shared_data%params)
              prev_mode          = mode
              prev_TRperiod      = TRperiod
              mode               = next_mode
              TRperiod           = next_TRperiod
              shared_data%params = next_params
              call jtty_note_station(cfg)
              if (cfg%utc_set .or. cfg%nutc_set) then
                 label_nutc = shared_data%params%nutc
                 label_iso = cfg%utc_set
              end if
              shared_data%params%nutc = label_nutc
              label_form = time_form(label_iso, TRperiod)
              if (hhmm_label(mode, TRperiod)) then
                 if (label_iso .or. label_nutc .gt. 2359) shared_data%params%nutc = label_nutc / 100
                 label_form = time_form(.false., TRperiod)
              else if (mode .eq. 4 .and. TRperiod .ge. 60.d0) then
                 shared_data%params%nutc = label_nutc / 100 * 100
              end if
              eme_delay_on = next_eme_on
              if (cfg%live_audio_set) live_audio = cfg%live_audio
              shared_data%params%ndiskdat = .not. live_audio
              call apply_msk144_fields(cfg, msk144)
              ! A configure that changes the applied mode or period starts a
              ! new period; what was in flight is discarded. Otherwise the
              ! period in flight decodes at the stop point the new settings give.
              if (mode .ne. prev_mode .or. TRperiod .ne. prev_TRperiod) then
                 call begin_period()
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
              call make_op(op, OP_CONFIGURE)
              op%cancels = cancels
              op%new_period = mode .ne. prev_mode .or. TRperiod .ne. prev_TRperiod
              op%clear_set = cfg%clear_average_set
              op%clear_average = cfg%clear_average
              op%events_set = cfg%busy_events_set
              op%events = cfg%busy_events
              call decode_submit(op)
              ! nagain_flag asks once for the last decoded period again;
              ! MSK144 and JTTY keep none.
              if (cfg%nagain_flag_set .and. cfg%nagain_flag) then
                 if (mode .eq. 144 .or. mode .eq. MODE_JTTY) then
                    call streaming_emit_error_code('decode_again_unavailable', &
                         NO_DECODED_PERIOD)
                 else
                    call hand_op(OP_AGAIN)
                 end if
              end if
              shared_data%params%nagain = .false.
              if (mode .ne. MODE_JTTY .and. .not. decoded) then
                 call run_early_ft8(.false.)
                 if (period_ready(period)) call run_period_decode()
              end if
           case (CTRL_DISCONTINUITY)
              anchored = .false.
              if (mode .eq. MODE_JTTY) then
                 call jtty_end(shared_data%params)
              else if (period%k .gt. 0) then
                 if (mode .ne. 144) call hand_op(OP_DISCONTINUITY)
                 call begin_period()
              end if
           case (CTRL_FLUSH)
              call flush_period()
           case (CTRL_PACK, CTRL_RENDER)
              call streaming_emit_reply(.true.)
              call stream_encode(ctl_buf(1:frame_len), action .eq. CTRL_RENDER)
              call streaming_emit_reply(.false.)
           case (CTRL_PARSE_ERR)
              call route_begin()
              call streaming_emit_error_code('configure_parse_error',          &
                   'malformed control frame (missing or invalid t field)')
              call route_end()
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
     ! decodes what it has — once, after a deferred final, and is not skipped.
     if (halt_req .or. eof_period) then
        ending = .true.
        call hand_op(OP_FREE)
     end if
     if (.not. decoded .and. period%k .gt. 0) call run_period_decode()

     if (halt_req .or. eof_period) then
        call end_decodes(OP_END)
        if (mode .eq. MODE_JTTY) call jtty_end(shared_data%params)
        call jtty_release()
        return
     end if
  end do

contains

  ! Start a period: live, its decode at the GUI's stop point for the settings;
  ! from a file, on the whole period as the GUI's file decode reads it.
  subroutine begin_period()
    call period_begin(period, TRperiod, mode, size(shared_data%id2), current_stop(), &
         file_reads_whole())
    period_number = period_number + 1
    shared_data%id2 = 0
    nhsym    = 0
    nhsym0   = 0       ! seed at 0 so the symspec catch-up loop is O(nhsym)
    decoded  = .false.
    ft8_done = 0
  end subroutine begin_period

  ! halt's decode of the period in flight, after a deferred final, without
  ! ending the session.
  subroutine flush_period()
    if (mode .eq. MODE_JTTY) then
       call jtty_end(shared_data%params)
       return
    end if
    ending = .true.
    call hand_op(OP_FREE)
    if (.not. decoded .and. period%k .gt. 0) call run_period_decode()
    ending = .false.
    if (period%k .gt. 0) call begin_period()
  end subroutine flush_period

  ! From a file the GUI's decode at the stop reads the whole period, as far as
  ! the engine takes it: FT4 72576 samples, multithreaded FT8 the stop
  ! (lib/decoder_engine.f90 analysis_extent and its FT8 cut), the rest whole.
  logical function file_reads_whole()
    file_reads_whole = .not. live_audio .and. mode .ne. 5 .and.              &
         .not. (mode .eq. 8 .and. shared_data%params%lmultift8 .and.          &
         .not. superfox_hound(shared_data%params))
  end function file_reads_whole

  integer function current_stop()
    current_stop = 0
    if (mode .eq. MODE_JTTY) return
    current_stop = gui_hsym_stop(mode, TRperiod, eme_delay_on,               &
         shared_data%params%lmultift8 .and. .not. superfox_hound(shared_data%params), &
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
  ! and 47. On SuperFox's even sequence the GUI decodes nothing before 50,
  ! though its multithreaded backpressure sees the live early triggers.
  subroutine ft8_early_schedule(h, n, depth2)
    integer, intent(out) :: h(2), n
    logical, intent(out) :: depth2
    h = 0
    n = 0
    depth2 = .false.
    if (superfox_even(shared_data%params) .and.                             &
         .not. backpressure(live_audio, ending, mode, shared_data%params)) then
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
         .not. superfox_hound(shared_data%params)) then
       if (shared_data%params%ndecoderstart .lt. 2) then
          h = [41, 46]
          n = 2
       end if
    else
       h = [41, 47]
       n = 2
    end if
  end subroutine ft8_early_schedule

  ! The sample count at which the period's next pass is due.
  integer function next_pass_samples()
    integer :: h(2), n, i
    logical :: depth2
    next_pass_samples = period%npts
    if (mode .eq. 144) next_pass_samples = min(period%npts,                 &
         (period%k / MSK144_BLOCK + 1) * MSK144_BLOCK)
    if (mode .ne. 8) return
    call ft8_early_schedule(h, n, depth2)
    do i = 1, n
       if (h(i) .gt. ft8_done .and. h(i) * HSYM_SAMPLES .gt. period%k) then
          next_pass_samples = min(next_pass_samples, h(i) * HSYM_SAMPLES)
          return
       end if
    end do
  end function next_pass_samples

  ! Trigger each FT8 pass before the stop point whose audio is in, or with
  ! every, each one still to run (a period cut short decodes what it has).
  subroutine run_early_ft8(every)
    logical, intent(in) :: every
    integer :: h(2), n, i
    logical :: depth2
    type(decode_op) :: op
    if (mode .ne. 8 .or. decoded) return
    call ft8_early_schedule(h, n, depth2)
    do i = 1, n
       if (h(i) .le. ft8_done) cycle
       if (.not. every .and. period%k .lt. h(i) * HSYM_SAMPLES) exit
       call make_op(op, OP_PASS)
       op%stage = merge(EARLY_ONE, EARLY_TWO, i .eq. 1)
       op%nzhsym = h(i)
       op%depth2 = i .eq. 2 .and. depth2
       op%k = period%k
       call take_audio(op, h(i) * HSYM_SAMPLES)
       call decode_submit(op)
       ft8_done = h(i)
    end do
  end subroutine run_early_ft8

  ! This period's decode, on the `npts` samples in `id2`. Triggered once per
  ! period: when the decoder's share is in, or from the drain when the
  ! period ends short of it.
  subroutine run_period_decode()
    integer :: nstop
    logical :: at_stop
    type(decode_op) :: op
    nstop = current_stop()
    ! The GUI's nzhsym is its stop point, for a decode at it, live or from a
    ! file; a period cut short decodes what it has. FT8 is tuned for its stop
    ! (jt9.f90:552).
    at_stop = period_ready(period) .and. nstop .gt. 0 .and.                 &
         nstop * HSYM_SAMPLES .le. period%nperiod
    if (mode .eq. 8 .and. superfox_even(shared_data%params) .and. nstop .lt. 50) then
       ! On SuperFox's even sequence the GUI decodes nothing before 50.
       call make_op(op, OP_PASS)
       op%stage = FINAL_STAGE
       op%nstop = nstop
       op%k = period%k
       call decode_submit(op)
       decoded = .true.
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
    if (mode .eq. 144) then
       ! Its blocks decoded as they came in (consume_audio_body).
       call report_pass_line(shared_data%params, period%k)
       call streaming_emit_period_finished(shared_data%params%nutc, label_form)
       return
    end if
    call make_op(op, OP_PASS)
    op%stage = FINAL_STAGE
    op%nstop = nstop
    op%k = period%k
    call take_audio(op, period%k)
    call decode_submit(op)
  end subroutine run_period_decode

  ! An operation at the frame loop's place in the stream, with the stream's
  ! state as it stands.
  subroutine make_op(op, kind)
    type(decode_op), intent(out) :: op
    integer,         intent(in)  :: kind
    op%kind = kind
    op%arrival = stream_stdin_arrival()
    op%t0 = t0
    if (anchored) op%last_sample = stream_samples - 1
    op%period = period_number
    op%mode = mode
    op%live = live_audio
    op%ending = ending
    op%params = shared_data%params
    op%form = label_form
  end subroutine make_op

  ! An operation that carries nothing more, handed over at once.
  subroutine hand_op(kind)
    integer, intent(in) :: kind
    type(decode_op) :: op
    call make_op(op, kind)
    call decode_submit(op)
  end subroutine hand_op

  ! The last operation (end, or stop after a fatal error): the decode side
  ! finishes everything before it.
  subroutine end_decodes(kind)
    integer, intent(in) :: kind
    type(decode_op) :: op
    call make_op(op, kind)
    call decode_join(op)
  end subroutine end_decodes

  ! A line the frame loop writes at its place in the stream: in a period
  ! mode, while the decode side has work, behind that work
  ! (lib/streaming_decode.f90); otherwise now. route_begin, the line,
  ! route_end.
  subroutine route_begin()
    if (mode .ne. 144 .and. mode .ne. MODE_JTTY .and. decode_has_work())     &
         call streaming_emit_capture()
  end subroutine route_begin

  subroutine route_end()
    character(len=:), allocatable :: lines
    type(decode_op) :: op
    call streaming_emit_captured(lines)
    if (len(lines) .eq. 0) return
    call make_op(op, OP_LINE)
    op%line = lines
    call decode_submit(op)
  end subroutine route_end

  ! A fatal error's line, behind every line before it; then the decode
  ! thread leaves. fatal_begin, the line, fatal_end, then stop.
  subroutine fatal_begin()
    if (decode_threaded()) call streaming_emit_capture()
  end subroutine fatal_begin

  subroutine fatal_end()
    call route_end()
    call end_decodes(OP_STOP)
  end subroutine fatal_end

  ! A pass's audio, the period's first n samples (for JT65+JT9 with its
  ! spectra), once the queued audio leaves room; tried again once the
  ! decode side has nothing queued or running; a pass that gets none is
  ! flagged memory.
  subroutine take_audio(op, n)
    type(decode_op), intent(inout) :: op
    integer,         intent(in)    :: n
    logical :: ok
    call decode_room()
    call snapshot(op%audio, shared_data, n, mode .eq. 74, ok)
    if (.not. ok) then
       call decode_wait()
       call snapshot(op%audio, shared_data, n, mode .eq. 74, ok)
    end if
    op%memory = .not. ok
  end subroutine take_audio

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
       call read_audio(chunk, take_samples, ios)
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
                call symspec(shared_data, nhsym*kstep + 2048, nsps,        &
                     ingain, bLowSidelobes, nminw, pxdb, s, df3,           &
                     ihsym, npts8, pxdbmax, npct_unused)
             end if
          end do
          ! The GUI's early FT8 passes run as soon as their audio is in.
          if (mode .eq. 8) call run_early_ft8(.false.)
          ! As does each MSK144 block, never refused for a busy decoder.
          if (mode .eq. 144) call msk144_window(shared_data%id2, period%k,   &
               shared_data%params%nutc, shared_data%params%ntol,              &
               shared_data%params%nfqso, shared_data%params%ndepth, msk144,   &
               data_dir, label_form)
       end if
    end do
  end subroutine consume_audio_body

  ! JTTY has no periods: the whole frame goes to the session.
  subroutine consume_jtty_body()
    do while (body_left .gt. 0)
       take_samples = min(body_left / 2, size(chunk))
       take_bytes   = 2 * take_samples
       call read_audio(chunk, take_samples, ios)
       if (ios /= 0) then
          eof_period = .true.; return
       end if
       body_left = body_left - take_bytes
       call jtty_audio(chunk(1:take_samples), stream_samples, shared_data%params)
       stream_samples = stream_samples + take_samples
    end do
  end subroutine consume_jtty_body

  ! Read exactly nbytes from stdin via the C helper; ios=0 on success,
  ! ios=1 at the end of input (short delivery: the producer closed the
  ! pipe). Every caller treats nonzero ios as end-of-stream. With no memory
  ! left to buffer the input the stream stops.
  ! last_sample is the stream sample index of the last audio sample read.
  subroutine stdin_read(buf, nbytes, ios, last_sample)
    type(*), dimension(*), intent(inout) :: buf
    integer, intent(in)  :: nbytes
    integer, intent(out) :: ios
    integer(int64), intent(in), optional :: last_sample
    integer(int64) :: k_last
    integer(c_int64_t) :: nread
    k_last = -1
    if (present(last_sample) .and. anchored) k_last = last_sample
    call decode_drain(int(nbytes, int64), t0, k_last)
    ios = 0
    nread = stream_stdin_read_fully(buf, int(nbytes, c_int64_t))
    call decode_read(t0, k_last)
    if (nread .lt. 0) then
       call fatal_begin()
       call streaming_emit_error_code('out_of_memory', 'no memory to buffer the input')
       call fatal_end()
       write(error_unit, '(a)') 'wsjt-codec: no memory to buffer the input'
       stop 1
    end if
    if (nread /= int(nbytes, c_int64_t)) ios = 1
  end subroutine stdin_read

  ! Read n audio samples. The session's first, and the first after a
  ! discontinuity, set the audio clock by their arrival; after that, the
  ! time the stream waits for input can only move it earlier (clock_start).
  subroutine read_audio(samples, n, ios)
    integer,        intent(in)    :: n
    integer(int16), intent(inout) :: samples(n)
    integer,        intent(out)   :: ios
    integer :: first
    real(8) :: wait
    first = 0
    ios = 0
    if (.not. anchored .and. n .gt. 0) then
       call stdin_read(samples, 2, ios)
       if (ios /= 0) return
       t0 = stream_stdin_arrival() - dble(stream_samples) / NFSAMPLE
       wait = stream_stdin_waited()
       anchored = .true.
       first = 1
    end if
    if (n .gt. first) then
       call stdin_read(samples(first+1:n), 2 * (n - first), ios, stream_samples + n - 1)
       wait = stream_stdin_waited()
       if (ios .eq. 0) t0 = clock_start(t0, stream_stdin_arrival(), stream_samples + n - 1, wait)
    end if
  end subroutine read_audio

end subroutine jt9_stream

logical function jt9_stream_available()
  jt9_stream_available = .true.
end function jt9_stream_available
