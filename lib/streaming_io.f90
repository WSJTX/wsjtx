! Streaming I/O — framed audio + control on stdin.
! Adopter reference: doc/codec/. A change to this interface updates it in the same pull request (CONTRIBUTING.md).
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
!               once, after ready. JT9, JT4, JT65 and JT65+JT9 decode only
!               at a trperiod of 60, and fast JT9 (fast true: JT9 only,
!               submodes E to H) only at 5, 10, 15 or 30: a configure that
!               would leave the stream otherwise is declined with
!               configure_range_error (key fast, trperiod or submode, in that
!               order), and a session the command line starts in one of the
!               first four at another period (-9, -4, -6, or -6 with -9, and
!               a -p other than 60) sends that error instead of ready and
!               exits with status 2. A configure that carries mode sets fast
!               false unless it carries fast too.
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
! A configure that enters fast JT9 (mode JT9 with fast true, or fast turning
! on) sets the receive and transmit frequencies to 700 unless it sets rxfreq
! (tx_audio_offset_hz for transmit), and in fast JT9 ntol's default is 50
! (slow JT9 keeps 1000).
! Fast JT9 decodes once a period, as the GUI's fastSink does, at 3456-sample
! block ends: live, at the first with less than 0.35 s of the period left
! (k >= (trperiod - 0.35) * 12000: 58,752, 117,504, 176,256 or 355,968
! samples), or, while the decoder is busy, at each later whole block, the
! last busy one skipping the period (pass_skipped "busy"); from a file, at
! the first at or past the period's samples less 3584 (179,712 in 15 s). It
! decodes on the period's samples up to that block end. Its decodes carry
! the period and the block (pass, k / 3456); its Fast Graph is fast_spectrum
! rows at each block, and it has no spectrum rows. A try without its audio
! waits for the next, as a busy one does. A period cut short (flush, halt,
! end of input) before its last try decodes as the GUI decodes a file of the
! samples it has, at the first block end at or past them less 3584; with
! fewer than one block, which the GUI never decodes, on all of them.
!
! Echo (ECHO, 3 s periods) measures each period at its 9th 3456-sample
! block, live and from a file, on the stream's own thread, and a period short
! of it not at all (lib/streaming_echo.f90).
!
! WSPR ({"mode":"WSPR"}, case aside; no integer code selects it) takes a
! trperiod of 120 s, its default, and decodes each period once on the decode
! thread as the other period modes do: live at the GUI's stop, 396
! half-symbols (1,368,576 samples), from a file at the period's end; its
! decodes carry "pass":396. It keeps no period to decode again
! (decode_again_unavailable), and a configure's wspr_hash_clear is applied
! on the decode side in stream order (lib/streaming_wspr.f90). A configure
! that would leave WSPR at another trperiod is declined with
! configure_range_error (key trperiod), and a session the command line starts
! in WSPR at another period sends that error instead of ready and exits with
! status 2.
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
! FreqCal ({"mode":"FREQCAL"}, case aside; no integer code or command-line
! flag selects it) takes a trperiod of 5, 10, 15 or 30 and measures at every
! 8th 3456-sample block end of a period from the 16th up to the GUI's stop,
! on this thread, as each block is read; its audio is read in those blocks
! whatever spectrum says, and it decodes nothing (lib/streaming_freqcal.f90).
! A configure that carries "mode":"FREQCAL" sets rxfreq to 1500 unless it sets
! rxfreq, and in FreqCal ntol's default is 50.
!
! Discontinuity: in the period modes it re-anchors the period, as the GUI
! starts a new input at each UTC period and on a restart. Every pass whose
! audio is in has run, and a deferred final still runs; a period in flight
! that has not reached its stop (from a file, the whole period, or FT4's and
! multithreaded FT8's stop; in fast JT9 the try that decodes the period, its
! last at most) ends without its decode or decode_finished (the
! decodes its early FT8 passes or MSK144 blocks sent stand); and the next
! audio sample starts a new period, its arrival restarting the busy clock.
! Decode-again requests waiting for the period's last FT8 pass run first. At
! a period's start it changes nothing.
!
! JTTY ({"mode":"JTTY"}; no integer code or command-line flag selects it) has
! no periods: its audio goes to lib/streaming_jtty.f90, which states its
! output. A JTTY session ends at a discontinuity, a flush, an odd-length
! audio frame, a configure that leaves JTTY or changes live_audio, halt and
! end of input. Entering
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
       CTRL_DISCONTINUITY, CTRL_PACK, CTRL_RENDER, CTRL_FLUSH, MODE_JTTY,   &
       MODE_FREQCAL, MODE_ECHO, CTRL_REVIEW, MODE_WSPR
  use streaming_apply, only: apply_configure_fields, engine_range_key
  use streaming_echo, only: echo_apply, echo_measure, echo_range_detail,    &
       ECHO_TRPERIOD, ECHO_STOP
  use decoder_engine, only: engine_params_problem, option_problem
  use streaming_jtty, only: jtty_audio, jtty_end, jtty_range_key,          &
       jtty_emit_range_error, jtty_release, jtty_note_station,             &
       jtty_spectrum_rows, jtty_prepared_audio
  use streaming_jtty_review, only: jtty_review_keep, jtty_review_request,  &
       jtty_review_idle, jtty_review_cancel, jtty_review_finish,           &
       jtty_review_release, jtty_review_live_audio
  use streaming_encode, only: stream_encode
  use streaming_period, only: period_state, period_begin, period_room,   &
       period_take, period_full, period_ready, period_set_stop,           &
       gui_hsym_stop, HSYM_SAMPLES, clock_start, fast_first_try, fast_last_try
  use streaming_apply, only: ft8_settings_change, apply_msk144_fields
  use streaming_decode, only: decode_op, decode_submit, decode_drain,      &
       snapshot, superfox_even, superfox_hound, backpressure, hhmm_label,  &
       report_pass_line, OP_PASS, OP_CONFIGURE, OP_AGAIN, OP_DISCONTINUITY, &
       OP_FREE, OP_END, OP_LINE, OP_STOP, EARLY_ONE, EARLY_TWO, FINAL_STAGE, &
       FAST_STAGE,                                                         &
       NO_DECODED_PERIOD, decode_start, decode_threaded, decode_join,      &
       decode_read, decode_room, decode_wait, decode_has_work
  use streaming_decode, only: rows_start, rows_end
  use msk144_stream, only: msk144_settings, msk144_window, MSK144_BLOCK,  &
       msk144_fast_spectrum, msk144_fast_reset
  use streaming_receive, only: receive_configure, receive_blocks,          &
       receive_equalizes, receive_equalize
  use streaming_receive, only: receive
  use streaming_emit, only: streaming_emit_spectrum, streaming_emit_set_live,  &
       streaming_emit_quiet, streaming_emit_prepared_audio
  use four2a_patience, only: patience_override
  use spectrum_rows, only: wide_rows, wide_begin, wide_take, wide_row, WIDE_BINS, WIDE_DF
  use streaming_freqcal, only: freqcal_period_ok, freqcal_stop, freqcal_problem,  &
       freqcal_block, FREQCAL_PERIOD_RULE

  include 'jt9com.f90'

  type(dec_data) :: shared_data
  integer        :: mode
  real(8)        :: TRperiod

  integer, parameter :: HDR_LEN = 8
  integer, parameter :: NFSAMPLE = 12000
  integer, parameter :: FRAME_AUDIO   = 1
  integer, parameter :: FRAME_CONTROL = 2
  integer, parameter :: CTL_BUF_LEN = 262144
  ! Fast JT9's periods and submodes, and the mode it belongs to.
  character(len=*), parameter :: FAST_PERIOD_RULE = 'trperiod must be 5, 10, 15 or 30 s in fast JT9'
  character(len=*), parameter :: FAST_SUBMODE_RULE = 'submode must be E to H in fast JT9'
  character(len=*), parameter :: FAST_MODE_RULE = 'fast applies to JT9'
  character(len=*), parameter :: WSPR_PERIOD_RULE = 'trperiod must be 120 s in WSPR'
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
  ! Fast JT9, as accepted configures set it; and the last of this period's
  ! decode tries handed over, as its block end (0: none).
  logical :: fast_jt9, next_fast
  integer :: fast_tried
  integer :: ingain, nminw, ihsym, npts8
  real    :: pxdb, df3, pxdbmax, s(NSMAX)
  integer :: npct_unused
  integer :: body_left, take_bytes, take_samples
  ! The most samples one read takes.
  integer, parameter :: READ_SAMPLES = 4096
  integer(int16) :: chunk(READ_SAMPLES)
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
  logical(1) :: bLowSidelobes
  logical :: eof_period, halt_req
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
  ! The GUI block in progress, MSK144's 3584 samples or the other modes'
  ! 3456 from the period's start: blk(1:nblk), from period sample blk_from
  ! on. blk_first: the receive chain has had none of the period's audio.
  integer(int16) :: blk(MSK144_BLOCK)
  integer :: nblk, blk_from
  logical :: blk_first
  ! The period's rows, fed every block from its first: rows_k samples.
  type(wide_rows) :: rows
  integer :: rows_k
  ! The period's prepared audio, sent every block from its first: through
  ! period sample prepared_k.
  integer :: prepared_k
  ! The open JTTY session's first stream sample, and the samples read for
  ! it and not yet handed on, jblk(1:njblk) from stream sample jblk_from on.
  logical :: jtty_open
  integer(int64) :: jtty_origin, jblk_from
  integer(int16) :: jblk(READ_SAMPLES)
  integer :: njblk

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
  npct_unused   = 0

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
  fast_jt9      = .false.
  shared_data%params%ndiskdat = .not. live_audio
  shared_data%params%emedelay = merge(2.5, 0.0, eme_delay_on)

  ! Stash session-baseline nfa/nfb. apply_configure_fields
  ! restores these on mode change with no explicit consumer override —
  ! mirrors WAV semantics where each invocation starts from CLI defaults.
  baseline_nfa = shared_data%params%nfa
  baseline_nfb = shared_data%params%nfb
  label_nutc = shared_data%params%nutc
  label_iso = .false.

  if (minute_mode(mode) .and. TRperiod .ne. 60.d0) then
     call streaming_emit_error_range('trperiod', trim(streaming_emit_mode_name(mode)), &
          minute_rule(mode) // '; the command line set it')
     write(error_unit, '(a)') 'wsjt-codec: ' // minute_rule(mode) // '; the command line set it'
     stop 2
  end if
  if (mode .eq. MODE_WSPR .and. TRperiod .ne. 120.d0) then
     call streaming_emit_error_range('trperiod', 'WSPR', WSPR_PERIOD_RULE // '; the command line set it')
     write(error_unit, '(a)') 'wsjt-codec: ' // WSPR_PERIOD_RULE // '; the command line set it'
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
  jtty_open = .false.
  jtty_origin = 0
  jblk_from = 0
  njblk = 0

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
        ! A JTTY review's steps run while no input waits.
        if (mode .eq. MODE_JTTY) call jtty_review_idle()
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
              if (mode .eq. MODE_JTTY .and. njblk .gt. 0) call jtty_deliver()
              call route_begin()
              call streaming_emit_error_code('odd_audio_frame',                &
                   'audio frame length must contain whole int16 samples')
              call route_end()
              ! The skipped samples are a hole in the JTTY session.
              if (mode .eq. MODE_JTTY) call jtty_close()
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
              ! A mode key sets fast false unless the frame sets it.
              next_fast = fast_jt9
              if (cfg%mode_set) next_fast = .false.
              if (cfg%fast_set) next_fast = cfg%fast
              ! Fast JT9's defaults. Entering it (mode JT9 with fast true, as the GUI's
              ! JT9 choice with Fast on, or fast turning on, as its Fast check box)
              ! sets the receive and transmit frequencies to 700, as the GUI's Rx
              ! and Tx Freq, unless the frame sets them; its ntol default is 50,
              ! JT9's F Tol, under the reset rule of every mode (mode or trperiod).
              ! A fast frame in fast JT9 without mode is no entry.
              if (next_fast .and. next_mode .eq. 9) then
                 if (cfg%mode_set .or. .not. fast_jt9) then
                    if (.not. cfg%rxfreq_set) next_params%nfqso = 700
                    if (.not. (cfg%rxfreq_set .or. cfg%tx_audio_offset_hz_set))          &
                         next_params%nftx = 700
                 end if
                 if ((cfg%mode_set .or. cfg%trperiod_set) .and. .not. cfg%ntol_set)      &
                      next_params%ntol = 50
              end if
              if (next_fast .and. next_mode .ne. 9) then
                 call route_begin()
                 call streaming_emit_error_range('fast', range_mode, FAST_MODE_RULE)
                 call route_end()
                 cycle
              end if
              if (next_fast .and. .not. any(next_TRperiod .eq. [5.d0, 10.d0, 15.d0, 30.d0])) then
                 call route_begin()
                 call streaming_emit_error_range('trperiod', 'JT9', FAST_PERIOD_RULE)
                 call route_end()
                 cycle
              end if
              if (.not. next_fast .and. minute_mode(next_mode) .and.            &
                   next_TRperiod .ne. 60.d0) then
                 call route_begin()
                 call streaming_emit_error_range('trperiod', range_mode, minute_rule(next_mode))
                 call route_end()
                 cycle
              end if
              if (next_mode .eq. MODE_FREQCAL .and. .not. freqcal_period_ok(next_TRperiod)) then
                 call route_begin()
                 call streaming_emit_error_range('trperiod', range_mode, FREQCAL_PERIOD_RULE)
                 call route_end()
                 cycle
              end if
              if (next_mode .eq. MODE_ECHO .and. next_TRperiod .ne. ECHO_TRPERIOD) then
                 call route_begin()
                 call streaming_emit_error_range('trperiod', range_mode, 'trperiod must be 3 s in ECHO')
                 call route_end()
                 cycle
              end if
              if (next_mode .eq. MODE_WSPR .and. next_TRperiod .ne. 120.d0) then
                 call route_begin()
                 call streaming_emit_error_range('trperiod', range_mode, WSPR_PERIOD_RULE)
                 call route_end()
                 cycle
              end if
              if (next_fast .and. (next_params%nsubmode .lt. 4 .or.               &
                   next_params%nsubmode .gt. 7)) then
                 call route_begin()
                 call streaming_emit_error_range('submode', 'JT9', FAST_SUBMODE_RULE)
                 call route_end()
                 cycle
              end if
              if (cfg%echo_problem .ne. ' ') then
                 call route_begin()
                 call streaming_emit_error_range(trim(cfg%echo_problem), range_mode, &
                      echo_range_detail(cfg%echo_problem))
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
              if (cfg%ref_spectrum_out_of_range) then
                 call route_begin()
                 call streaming_emit_error_range('ref_spectrum', range_mode,      &
                      'ref_spectrum must be off, measure or apply')
                 call route_end()
                 cycle
              end if
              if (cfg%q65_callers_remove_out_of_range) then
                 call route_begin()
                 call streaming_emit_error_range('q65_callers_remove', range_mode, &
                      'q65_callers_remove holds at most 50 calls, the list''s capacity')
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
                 if (next_mode .eq. MODE_FREQCAL)                                &
                      problem = freqcal_problem(next_params%nfqso, next_params%ntol)
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
              if (mode .eq. MODE_JTTY .and. next_mode .ne. MODE_JTTY) then
                 call jtty_review_cancel()
                 call jtty_close()
              end if
              prev_mode          = mode
              prev_TRperiod      = TRperiod
              fast_jt9           = next_fast
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
              ! A change of live_audio ends a JTTY review in progress.
              ! A change of live_audio ends the open JTTY session, as WSJT-X's
              ! opening a file starts a new one (beginJttyDisk).
              if (cfg%live_audio_set .and. (cfg%live_audio .neqv. live_audio)) then
                 call jtty_review_live_audio(cfg%live_audio)
                 if (mode .eq. MODE_JTTY) call jtty_close()
              end if
              if (cfg%live_audio_set) live_audio = cfg%live_audio
              call streaming_emit_set_live(live_audio)
              shared_data%params%ndiskdat = .not. live_audio
              call apply_msk144_fields(cfg, msk144)
              call echo_apply(cfg, mode)
              call route_begin()
              call receive_configure(cfg)
              call route_end()
              if (receive_blocks()) call rows_start()
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
              ! In ECHO a clear is Echo's (lib/streaming_echo.f90).
              op%clear_set = cfg%clear_average_set .and. mode .ne. MODE_ECHO
              op%clear_average = cfg%clear_average
              ! The Q65 caller list's edits, on the decode side in stream order.
              op%q65_clear = cfg%q65_callers_clear_set .and. cfg%q65_callers_clear
              op%q65_remove = cfg%q65_callers_remove(1:cfg%q65_callers_remove_count)
              op%fox_clear = cfg%fox_callers_clear_set .and. cfg%fox_callers_clear
              ! And the WSPR hash table's clear.
              op%wspr_clear = cfg%wspr_hash_clear_set .and. cfg%wspr_hash_clear
              op%events_set = cfg%busy_events_set
              op%events = cfg%busy_events
              call decode_submit(op)
              ! nagain_flag asks once for the last decoded period again;
              ! MSK144, Echo, JTTY, FreqCal and WSPR keep none (the GUI's
              ! Decode button does nothing in WSPR).
              if (cfg%nagain_flag_set .and. cfg%nagain_flag) then
                 if (mode .eq. 144 .or. mode .eq. MODE_JTTY .or. mode .eq. MODE_ECHO .or. &
                      mode .eq. MODE_FREQCAL .or. mode .eq. MODE_WSPR) then
                    call streaming_emit_error_code('decode_again_unavailable', &
                         NO_DECODED_PERIOD)
                 else
                    call hand_op(OP_AGAIN)
                 end if
              end if
              shared_data%params%nagain = .false.
              if (mode .ne. MODE_JTTY .and. .not. decoded) then
                 call run_early_ft8(.false.)
                 call run_fast_tries()
                 if (period_ready(period)) call run_period_decode()
              end if
           case (CTRL_DISCONTINUITY)
              anchored = .false.
              if (mode .eq. MODE_JTTY) then
                 call jtty_close()
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
           case (CTRL_REVIEW)
              call streaming_emit_reply(.true.)
              call jtty_review_request(ctl_buf(1:frame_len), mode .eq. MODE_JTTY, &
                   live_audio, shared_data%params)
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
        call jtty_review_finish()
        if (mode .eq. MODE_JTTY) call jtty_close()
        call jtty_review_release()
        call jtty_release()
        call rows_end()
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
    nblk = 0
    blk_from = 0
    blk_first = .true.
    rows_k = 0
    prepared_k = 0
    fast_tried = 0
    call msk144_fast_reset()
  end subroutine begin_period

  ! halt's decode of the period in flight, after a deferred final, without
  ! ending the session.
  subroutine flush_period()
    if (mode .eq. MODE_JTTY) then
       call jtty_close()
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
    file_reads_whole = .not. live_audio .and. mode .ne. 5 .and. mode .ne. MODE_ECHO .and. &
         .not. (mode .eq. 8 .and. shared_data%params%lmultift8 .and.          &
         .not. superfox_hound(shared_data%params))
  end function file_reads_whole

  ! JT9 (without fast), JT4, JT65 and JT65+JT9 decode only at 60 s, as the
  ! GUI runs them.
  pure logical function minute_mode(m)
    integer, intent(in) :: m
    minute_mode = any(m .eq. [4, 9, 65, 74])
  end function minute_mode

  function minute_rule(m) result(rule)
    integer, intent(in) :: m
    character(len=:), allocatable :: rule
    rule = 'trperiod must be 60 s in ' // trim(streaming_emit_mode_name(m))
  end function minute_rule

  integer function current_stop()
    current_stop = 0
    if (mode .eq. MODE_JTTY .or. fast_on()) return
    if (mode .eq. MODE_FREQCAL) then
       current_stop = freqcal_stop(TRperiod)
       return
    end if
    current_stop = gui_hsym_stop(mode, TRperiod, eme_delay_on,               &
         shared_data%params%lmultift8 .and. .not. superfox_hound(shared_data%params), &
         shared_data%params%ndecoderstart)
    if (mode .eq. MODE_ECHO) current_stop = ECHO_STOP
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
    if (fast_on()) then
       if (fast_next_try() .gt. 0) next_pass_samples = min(period%npts, fast_next_try())
       return
    end if
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
    ! The passes take the period's audio through its last sample; MSK144's
    ! period end takes none.
    if (mode .ne. 144) call block_cut()
    ! FreqCal measures as its blocks are read and publishes no decode, as
    ! MainWindow::dataSink returns before decode() in FreqCal.
    if (mode .eq. MODE_FREQCAL) then
       decoded = .true.
       return
    end if
    if (mode .eq. MODE_ECHO) then
       ! Measured on the main thread at its stop, as the GUI's Echo block;
       ! a period short of it is not (lib/streaming_echo.f90).
       decoded = .true.
       if (period%k .ge. ECHO_STOP * HSYM_SAMPLES) call echo_measure(shared_data%id2, &
            period%k, live_audio, shared_data%params%nutc, label_form)
       call streaming_emit_period_finished(shared_data%params%nutc, label_form)
       return
    end if
    if (fast_on()) then
       ! A period cut short before its last try decodes as the GUI decodes a
       ! file of that many samples: at the first block end at or past them
       ! less 3584, on the samples to there (with fewer than one block, the
       ! codec's own rule: on all of them).
       call run_fast_tries()
       if (.not. decoded) call fast_try(min(period%k,                       &
            fast_first_try(TRperiod, period%k, .false.)), .true.)
       return
    end if
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
    ! Non-FT8/FST4/Q65/WSPR: cap at 181 to match WAV-path exit (ss array
    ! bound); WSPR's decodes carry its stop, 396.
    if (mode .ne. 8   .and. mode .ne. 240 .and. mode .ne. 241 .and.    &
        mode .ne. 242 .and. mode .ne. 66  .and. mode .ne. MODE_WSPR .and. &
        shared_data%params%nzhsym .gt. 181) then
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
    ! JT9 windows the spectra it builds itself by bit 0x100 of ndepth; the
    ! bit comes from low_sidelobes alone, not from the depth a client sent.
    op%params%ndepth = iand(op%params%ndepth, not(256))
    if (mode .eq. 9 .and. receive%low_sidelobes) &
         op%params%ndepth = ior(op%params%ndepth, 256)
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
  ! MSK144, FreqCal, Echo and JTTY write their own lines in order, on this thread.
  subroutine route_begin()
    if (mode .ne. 144 .and. mode .ne. MODE_JTTY .and. mode .ne. MODE_FREQCAL &
         .and. mode .ne. MODE_ECHO .and. decode_has_work()) call streaming_emit_capture()
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
    call streaming_emit_quiet()
    if (decode_threaded()) call streaming_emit_capture()
  end subroutine fatal_begin

  subroutine fatal_end()
    call route_end()
    call end_decodes(OP_STOP)
    call rows_end()
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
       ! And at the GUI block's end, which the receive chain takes whole and
       ! FreqCal measures at.
       if (receive_blocks() .or. mode .eq. MODE_FREQCAL)                     &
            take_samples = min(take_samples,                                 &
            block_size() - mod(period%kperiod, block_size()))
       take_bytes   = 2 * take_samples
       call read_audio(chunk, take_samples, ios)
       if (ios /= 0) then
          eof_period = .true.; return
       end if
       body_left = body_left - take_bytes
       stream_samples = stream_samples + take_samples
       k0 = period%k
       call period_take(period, take_samples, nacc)
       if (nacc .gt. 0) shared_data%id2(k0+1 : k0+nacc) = chunk(1:nacc)
       call block_add(chunk(1:take_samples))
       if (nacc .gt. 0 .and. .not. receive_blocks()) call block_work()
    end do
  end subroutine consume_audio_body

  ! The GUI's block in the mode (MainWindow::initializeFFT): MSK144's 3584
  ! samples, the other modes' 3456.
  integer function block_size()
    block_size = HSYM_SAMPLES
    if (mode .eq. 144) block_size = MSK144_BLOCK
  end function block_size

  ! The samples just taken into the period, which now holds period%kperiod,
  ! into the block in progress; each block they end goes to the receive
  ! chain when it takes blocks.
  subroutine block_add(samples)
    integer(int16), intent(in) :: samples(:)
    integer :: i, n, at
    i = 1
    do while (i .le. size(samples))
       at = period%kperiod - size(samples) + i - 1
       n = min(size(samples) - i + 1, block_size() - mod(at, block_size()))
       blk(nblk + 1:nblk + n) = samples(i:i + n - 1)
       nblk = nblk + n
       i = i + n
       if (mod(at + n, block_size()) .eq. 0) then
          if (receive_blocks()) then
             call block_deliver()
          else
             nblk = 0
             blk_from = at + n
          end if
       end if
    end do
  end subroutine block_add

  ! The block in progress, as far as it is in: equalized, then sent back,
  ! then its decoder's share into id2, then its rows, then the decodes its
  ! audio triggers.
  subroutine block_deliver()
    integer :: last
    call receive_equalize(blk(1:nblk), live_audio, blk_first)
    blk_first = .false.
    call period_prepared()
    last = min(blk_from + nblk, period%k)
    if (last .gt. blk_from) shared_data%id2(blk_from + 1:last) = blk(1:last - blk_from)
    if (mode .eq. 144) then
       call msk144_rows()
    else if (fast_on()) then
       call fast_jt9_rows()
    else
       call period_rows()
    end if
    if (last .gt. blk_from) call block_work()
    blk_from = blk_from + nblk
    nblk = 0
  end subroutine block_deliver

  ! A pass about to run on the period's audio takes the block in progress
  ! as far as it is in, and every decode its audio triggers before.
  subroutine block_cut()
    if (nblk .eq. 0) return
    if (receive_blocks()) then
       call block_deliver()
    else
       call block_work()
    end if
  end subroutine block_cut

  ! A period mode's rows for the block delivered, blk(1:nblk) from period
  ! sample blk_from on, after its equalization and before its decodes: a row
  ! at each whole block's end, live through the period, from a file up to the
  ! stop (as the GUI's diskDat), or through the period in JT65+JT9, which has
  ! none. A period has rows only from its first block.
  subroutine period_rows()
    integer :: i, taken, nstop
    logical :: ready
    real :: power, peak, bins(WIDE_BINS)
    if (.not. receive%spectrum .or. blk_from .ne. rows_k) return
    nstop = current_stop()
    if (.not. live_audio .and. nstop .gt. 0 .and. blk_from .ge. nstop * HSYM_SAMPLES) return
    if (blk_from .eq. 0) call wide_begin(rows)
    i = 1
    do while (i .le. nblk)
       call wide_take(rows, blk(i:nblk), taken, ready)
       i = i + taken
       if (ready) then
          call wide_row(rows, receive%low_sidelobes, power, peak, bins)
          call streaming_emit_spectrum(blk_from + i - 1,                         &
               (blk_from + i - 1) / HSYM_SAMPLES, power, peak, WIDE_DF, bins)
       end if
    end do
    rows_k = blk_from + nblk
  end subroutine period_rows

  ! The block delivered, blk(1:nblk) from period sample blk_from on, after
  ! the receive chain, with prepared_audio on live audio: a period has these
  ! lines only from its first block, and none after a block without one.
  subroutine period_prepared()
    if (.not. (receive%prepared_audio .and. live_audio)) return
    if (blk_from .ne. prepared_k) return
    call streaming_emit_prepared_audio(blk_from + nblk, blk(1:nblk))
    prepared_k = blk_from + nblk
  end subroutine period_prepared

  ! MSK144's rows for the block delivered, its samples in id2 through
  ! blk_from + nblk, before its decode.
  subroutine msk144_rows()
    if (mod(blk_from + nblk, MSK144_BLOCK) .ne. 0) return
    call msk144_fast_spectrum(shared_data%id2, blk_from + nblk, int(TRperiod), MSK144_BLOCK)
  end subroutine msk144_rows

  ! Fast JT9's Fast Graph for the block delivered, its samples in id2
  ! through blk_from + nblk, before its decode tries: hspec's columns at
  ! each 3456-sample block, as the GUI's fastSink draws them.
  subroutine fast_jt9_rows()
    if (mod(blk_from + nblk, HSYM_SAMPLES) .ne. 0) return
    call msk144_fast_spectrum(shared_data%id2, blk_from + nblk, int(TRperiod), HSYM_SAMPLES)
  end subroutine fast_jt9_rows

  ! The stream decodes fast JT9.
  logical function fast_on()
    fast_on = fast_jt9 .and. mode .eq. 9
  end function fast_on

  ! The block end of this period's next fast JT9 decode try (0: none left).
  integer function fast_next_try()
    integer :: first, last
    fast_next_try = 0
    if (decoded) return
    first = fast_first_try(TRperiod, period%nperiod, live_audio)
    last = fast_last_try(period%nperiod, live_audio, first)
    fast_next_try = first
    if (fast_tried .gt. 0) fast_next_try = fast_tried + HSYM_SAMPLES
    if (fast_next_try .gt. last) fast_next_try = 0
  end function fast_next_try

  ! Each fast JT9 decode try whose block end is in, the period's last
  ! marked so (lib/streaming_decode.f90 decides which one decodes).
  subroutine run_fast_tries()
    integer :: k, first
    if (.not. fast_on()) return
    do
       k = fast_next_try()
       if (k .eq. 0 .or. period%k .lt. k) exit
       first = fast_first_try(TRperiod, period%nperiod, live_audio)
       call fast_try(k, k .eq. fast_last_try(period%nperiod, live_audio, first))
    end do
  end subroutine run_fast_tries

  ! A fast JT9 decode try at sample k of the period, on its first k samples.
  subroutine fast_try(k, last)
    integer, intent(in) :: k
    logical, intent(in) :: last
    type(decode_op) :: op
    shared_data%params%ntr = int(TRperiod)
    shared_data%params%nagain = .false.
    call make_op(op, OP_PASS)
    op%stage = FAST_STAGE
    op%last_try = last
    op%k = k
    call take_audio(op, k)
    call decode_submit(op)
    fast_tried = k
    if (last) decoded = .true.
  end subroutine fast_try

  ! The period's audio is in through period%k: JT65+JT9's symbol spectra,
  ! FT8's early passes and MSK144's block decode.
  subroutine block_work()
    integer :: patience
    ! Incremental symspec for JT9-family at kstep boundaries.
    ! Cap at nhsym=181 to match the WAV-path exit condition
    ! (jt9.f90:442-443) — exceeding it overruns ss(184,*).
    do while ((period%k - 2048) / kstep .gt. nhsym0 .and. nhsym0 .lt. 181)
       nhsym0 = nhsym0 + 1
       if (nhsym0 .lt. 1) cycle
       nhsym = nhsym0
       if (mode .eq. 74) then
          ! Planned at patience 0, as the GUI's process plans them, and
          ! windowed by low_sidelobes, as the GUI's Low sidelobes windowed
          ! the spectra its JT9 decoder read: JT65+JT9's JT9 decoder reads
          ! these.
          bLowSidelobes = receive%low_sidelobes
          patience = patience_override
          patience_override = 0
          call symspec(shared_data, nhsym*kstep + 2048, nsps,        &
               ingain, bLowSidelobes, nminw, pxdb, s, df3,           &
               ihsym, npts8, pxdbmax, npct_unused)
          patience_override = patience
       end if
    end do
    ! The GUI's early FT8 passes run as soon as their audio is in.
    if (mode .eq. 8) call run_early_ft8(.false.)
    ! As do fast JT9's decode tries.
    call run_fast_tries()
    ! As does each MSK144 block, never refused for a busy decoder.
    if (mode .eq. 144) call msk144_window(shared_data%id2, period%k,   &
         shared_data%params%nutc, shared_data%params%ntol,              &
         shared_data%params%nfqso, shared_data%params%ndepth, msk144,   &
         data_dir, label_form)
    ! And each FreqCal measurement.
    if (mode .eq. MODE_FREQCAL) call freqcal_block(shared_data%id2, period%k, &
         TRperiod, shared_data%params%nfqso, shared_data%params%ntol,      &
         shared_data%params%nutc, label_form)
  end subroutine block_work

  ! JTTY has no periods: the whole frame goes to the session, in GUI blocks
  ! from the session's first sample while the receive chain equalizes.
  subroutine consume_jtty_body()
    do while (body_left .gt. 0)
       take_samples = min(body_left / 2, size(chunk))
       if (receive_equalizes(live_audio)) take_samples = min(take_samples, jtty_room())
       take_bytes   = 2 * take_samples
       call read_audio(chunk, take_samples, ios)
       if (ios /= 0) then
          eof_period = .true.; return
       end if
       body_left = body_left - take_bytes
       call jtty_add(chunk(1:take_samples))
       stream_samples = stream_samples + take_samples
    end do
  end subroutine consume_jtty_body

  ! Samples to the end of the JTTY session's block in progress.
  integer function jtty_room()
    jtty_room = HSYM_SAMPLES
    if (jtty_open) jtty_room = HSYM_SAMPLES -                                &
         int(mod(stream_samples - jtty_origin, int(HSYM_SAMPLES, int64)))
  end function jtty_room

  ! JTTY samples read from stream sample stream_samples on: on to the
  ! session at once, or, while the receive chain equalizes, a block at a
  ! time.
  subroutine jtty_add(samples)
    integer(int16), intent(in) :: samples(:)
    if (.not. jtty_open) then
       jtty_open = .true.
       jtty_origin = stream_samples
    end if
    if (.not. receive_equalizes(live_audio) .and. njblk .gt. 0) call jtty_deliver()
    if (njblk .eq. 0) jblk_from = stream_samples
    jblk(njblk + 1:njblk + size(samples)) = samples
    njblk = njblk + size(samples)
    if (.not. receive_equalizes(live_audio) .or. jtty_room() .eq. size(samples)) &
         call jtty_deliver()
  end subroutine jtty_add

  ! The JTTY samples held, through the receive chain, back to the client
  ! with prepared_audio on live audio, then to the rows and the session.
  subroutine jtty_deliver()
    if (receive_blocks()) call receive_equalize(jblk(1:njblk), live_audio,    &
         jblk_from .eq. jtty_origin)
    if (receive%prepared_audio .and. live_audio)                             &
         call jtty_prepared_audio(jblk(1:njblk), jblk_from)
    call jtty_rows()
    call jtty_audio(jblk(1:njblk), jblk_from, shared_data%params)
    call jtty_review_keep(jblk(1:njblk), jblk_from)
    njblk = 0
  end subroutine jtty_deliver

  ! JTTY's rows for the samples delivered, jblk(1:njblk) from stream sample
  ! jblk_from on, before the session decodes them.
  subroutine jtty_rows()
    call jtty_spectrum_rows(jblk(1:njblk), jblk_from)
  end subroutine jtty_rows

  ! The JTTY session ends, after what it holds.
  subroutine jtty_close()
    if (njblk .gt. 0) call jtty_deliver()
    call jtty_end(shared_data%params)
    jtty_open = .false.
  end subroutine jtty_close

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
