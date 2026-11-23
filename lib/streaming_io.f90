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
! {"t":"pack",...}, {"t":"render",...} and {"t":"heard",...}
! (lib/streaming_encode.f90), answered in any mode. See lib/streaming_control.f90 for the parser + schema.
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
!               once, after ready. JT9, JT4 and JT65 decode only
!               at a trperiod of 60, and fast JT9 (fast true: JT9 only,
!               submodes E to H) only at 5, 10, 15 or 30: a configure that
!               would leave the stream otherwise is declined with
!               configure_range_error (key fast, trperiod or submode, in that
!               order), and a session the command line starts in one of the
!               first three at another period (-9, -4 or -6, and a -p other
!               than 60) sends that error instead of ready and exits with
!               status 2 (-6 with -9 is refused before, on stderr). A
!               configure that carries mode sets fast false unless it carries
!               fast too.
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
!   cancel    : monitoring stops, as at the GUI's Stop or a half-duplex
!               transmission's start (its monitor(false)): a waiting final
!               is dropped (pass_skipped "cancelled"), and in JTTY the
!               session ends; a running decode, its lines and the period to
!               decode again stay. A client then sends no audio until
!               monitoring resumes, and a discontinuity before its first
!               sample.
!
! Receive frequency (configure's dial_frequency, Hz): a value other than the
! one in force, of any size, is the GUI's receive frequency changing
! (widgets/mainwindow_band.cpp). A waiting final is dropped, then and when
! the decoder frees if it was published under another frequency than the
! one in force at the free's place in the stream; live in JTTY the session
! ends; in JT65 the next decode clears the averages. Live, each
! decode pass is published under the GUI's period frequency, which is the
! dial at the session's first publication and follows it 0.6 TR after each
! change; once a pass's frequency is not the one in force its decode and
! side lines are dropped, from the next one, latched (lib/stream_decode.c,
! lib/streaming_emit.f90), and it leaves no period to decode again, as the
! GUI drops an obsolete decode's output and abandons its retained audio. Its
! decode_finished still comes. WSPR's, fast JT9's and MSK144's decodes, which
! the GUI decodes outside its worker, are never dropped. From a file a change
! applies in stream order and drops nothing.
!
! Decode-again (configure's nagain_flag) with no period to decode again
! decodes the period in flight, if it has samples and a decode has been
! triggered in the session, as the GUI's Decode button does: a new decode
! (newdat) of the audio in so far, zeros after it, on a new decoder input,
! under the period's label, at the mode's stop, with its own
! decode_finished, which becomes the period to decode again; the period
! goes on. Q65, FST4 and FST4W decode only a completed reception again, live
! FST4W none, fast JT9 only its last decode, and a live SuperFox even
! sequence (the kept period's, or the one in flight) nothing before
! half-symbol 50. The GUI's Echo, MSK144 and JTTY decodes count as a first
! decode.
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
! JT9, JT65 and JT4 periods of a minute or more HHMM, the others
! HHMMSS (widgets/mainwindow.cpp), and each decoder here gets the form it
! gets from the GUI whichever form the client sends: JT9 and JT65
! decode with HHMM, JT4 with HHMM00; their time and period_end are HHMM00.
! A nutc is kept as it was sent, and a later mode reads it again, except
! that JT4, Q65 and FST4 periods of a minute or more fix an HHMM one on
! arrival (times 100); so send a label with every period, after any mode
! change. A configure whose nutc or utc is not a time of day is declined
! (lib/streaming_control.f90), so every label in force is one.
!
! Live audio or a file (configure's live_audio, false at the start): from a
! file each period decodes on its whole audio once the period is in (FT4
! and multithreaded FT8 at their stop points; FT8's early passes still run,
! the multithreaded decoder's only at start 0 and 1, as lib/jt9a.f90's),
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
! (tx_audio_offset_hz for transmit); ntol follows WSJT-X's F Tol, which
! choosing fast JT9 puts back to JT9's (lib/streaming_apply.f90).
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
! rxfreq; ntol follows WSJT-X's F Tol (lib/streaming_apply.f90).
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
! end of input. ntol is
! JTTY's own F Tol (20 until set) and nfa and nfb the waterfall's range, kept
! from mode to mode; entering JTTY from another mode puts back the Rx and Tx
! Freq the modes that keep them last had, as WSJT-X does
! (lib/streaming_apply.f90). A configure that stays in JTTY keeps the
! session. trperiod is validated and unused. lib/streaming_jtty.f90 states
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
! on Linux each one's copy of the decoders' thread-local arrays (about 2.5 MB
! on arm64) comes out of that, leaving about 5.6 MiB of the usual 8 MiB.

subroutine jt9_stream(shared_data, mode, TRperiod)
  use, intrinsic :: iso_fortran_env, only: int8, int16, int64, error_unit
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use, intrinsic :: iso_c_binding, only: c_int64_t, c_double
  use prog_args, only: data_dir, result_files
  use ft8_decode, only: fox_list_reset
  use streaming_emit, only: streaming_emit_ready, streaming_emit_error,    &
       streaming_emit_error_code, streaming_emit_error_version,            &
       streaming_emit_error_type, time_form, streaming_emit_period_finished, &
       streaming_emit_error_range, streaming_emit_mode_name,               &
       streaming_emit_set_clock, streaming_emit_reply,                     &
       streaming_emit_capture, streaming_emit_captured,                   &
       streaming_emit_set_a7_hold
  use streaming_control, only: parse_control_frame, configure_fields,      &
       control_type_error, CTRL_CONFIGURE, CTRL_HALT, CTRL_PARSE_ERR,      &
       CTRL_DISCONTINUITY, CTRL_PACK, CTRL_RENDER, CTRL_FLUSH, MODE_JTTY,   &
       MODE_FREQCAL, MODE_ECHO, CTRL_REVIEW, MODE_WSPR, CTRL_CANCEL, CTRL_HEARD
  use streaming_apply, only: apply_configure_fields, engine_range_key,       &
       search_controls, start_search_controls, apply_search_controls
  use streaming_echo, only: echo_apply, echo_measure, echo_range_detail,    &
       ECHO_TRPERIOD, ECHO_STOP
  use decoder_engine, only: engine_params_problem, option_problem
  use streaming_jtty, only: jtty_audio, jtty_end, jtty_range_key,          &
       jtty_emit_range_error, jtty_release, jtty_note_station,             &
       jtty_spectrum_rows, jtty_prepared_audio
  use streaming_jtty_review, only: jtty_review_keep, jtty_review_request,  &
       jtty_review_idle, jtty_review_cancel, jtty_review_finish,           &
       jtty_review_release, jtty_review_live_audio
  use streaming_encode, only: stream_encode, stream_heard
  use streaming_period, only: period_state, period_begin, period_room,   &
       period_take, period_full, period_ready, period_set_stop,           &
       gui_hsym_stop, HSYM_SAMPLES, clock_start, fast_first_try, fast_last_try, &
       trigger_fire_time
  use streaming_apply, only: ft8_settings_change, settings_dialog_change,   &
       apply_msk144_fields
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
  ! ft8_threads and wspr_threads: the FT8 kernel's rule, which WSPR's decode
  ! shares (lib/wsprd/wspr_parallel.f90), runs a count above 12 on one thread.
  integer, parameter :: THREADS_MAX = 12
  character(len=*), parameter :: THREADS_LIMIT = 'from 0 to 12'
  ! The bits WSJT-X's decoding depth (m_ndepth) can hold: Fast, Normal or
  ! Deep in the low two (its exclusive DepthGroup), and its Decode menu's
  ! averaging (16), deep search (32), AP for the DX call (64) and automatic
  ! clear (128) items (widgets/mainwindow.cpp on_actionQuickDecode_toggled
  ! to on_actionAuto_Clear_Avg_toggled); never 4, 8 or above 128.
  integer, parameter :: DEPTH_BITS = 3 + 16 + 32 + 64 + 128
  ! (option_problem's limit holds 48 characters.)
  character(len=*), parameter :: DEPTH_BITS_LIMIT = 'from 1 to 3 and no bits but 16, 32, 64 and 128'
  ! 4-byte session magic "WSJT" (W=0x57 S=0x53 J=0x4A T=0x54)
  integer(int8), parameter :: MAGIC(4) = [                                 &
       int(z'57', int8), int(z'53', int8),                                 &
       int(z'4A', int8), int(z'54', int8) ]

  integer :: ios, fmt, ch, rate_khz
  integer(int8)  :: hdr(HDR_LEN), type_byte(1), len_bytes(4)
  integer(int64) :: frame_len_wide, max_audio_frame_bytes
  integer        :: frame_len

  integer :: nhsym
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
  ! The special operating activity the last configure that carried the
  ! contest group gave, uncollapsed (-1: none yet; the command line's -X
  ! gives a packed value, read as unknown).
  integer :: special_op
  ! WSPR's decode threads (wspr_threads; 0, from the CPU count, until set).
  integer :: wspr_threads
  ! The GUI's Reduce false decodes (reduce_false_decodes; off until set).
  logical :: reduce_false
  ! Its Active Stations window open (active_stations), and opened at all in
  ! the session: the GUI keeps the window once made, closed or open, and
  ! with ARRL Digi shows the decodes' points while it exists
  ! (MainWindow::readFromStdout's bDisplayPoints).
  logical :: active_stations, active_made
  ! WSJT-X's decode identity besides the frequency, as decode_identity gives
  ! it for what is in force.
  integer(int64) :: identity_now
  ! The activity in it: the command line's collapsed one (as 16 plus it),
  ! then the uncollapsed one a configure changes it to.
  integer :: identity_activity
  ! The receive dial frequency (configure's dial_frequency; -1: none yet),
  ! and the one WSJT-X attributes a live period's decodes to, which follows
  ! it 0.6 TR after its last change (the GUI's m_freqNominalPeriod; -1:
  ! none yet, the dial's then stands), at arrival dial_due.
  integer(int64) :: dial_now, dial_nominal
  real(8) :: dial_due, dial_at
  ! Where a configure starts WSJT-X's hold on a7 decodes, on the audio clock.
  real(8) :: hold_at
  logical :: dial_changed
  ! A decode has been triggered in the session (the GUI's m_dataAvailable):
  ! before one, its Decode button does nothing.
  logical :: published
  type(decode_op) :: again_op
  ! Audio samples consumed in any mode; JTTY's origin and times count them.
  integer(int64) :: stream_samples
  ! WSJT-X's F Tol and search range controls (lib/streaming_apply.f90), and a
  ! configure's copy, kept when it is accepted.
  type(search_controls) :: controls, next_controls
  ! Whether a configure changes a Settings item WSJT-X's decode reads: an
  ! accepted Settings change, which cancels a waiting FT8 final and sets the
  ! mode up again.
  logical :: settings_accept
  ! The period label as the client gave it, and whether as utc; each decoder
  ! gets it in the form the GUI gives that decoder, label_form.
  integer :: label_nutc
  logical :: label_iso
  type(time_form) :: label_form
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
     subroutine stream_decode_set_dial(hz) bind(C, name="stream_decode_set_dial")
       import :: c_int64_t
       integer(c_int64_t), value :: hz
     end subroutine stream_decode_set_dial
     subroutine stream_decode_set_identity(identity) bind(C, name="stream_decode_set_identity")
       import :: c_int64_t
       integer(c_int64_t), value :: identity
     end subroutine stream_decode_set_identity
     function stream_stdin_waited() result(w) bind(C, name="stream_stdin_waited")
       import :: c_double
       real(c_double) :: w
     end function stream_stdin_waited
  end interface


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
  special_op    = -1
  wspr_threads  = 0
  reduce_false  = .false.
  active_stations = .false.
  active_made   = .false.
  live_audio    = .false.
  fast_jt9      = .false.
  shared_data%params%ndiskdat = .not. live_audio
  shared_data%params%emedelay = merge(2.5, 0.0, eme_delay_on)

  ! The session's F Tol and search range, as WSJT-X's at its start in this
  ! mode (jt9.f90 leaves ntol NTOL_UNSET without -F).
  call start_search_controls(controls, mode, shared_data%params)
  ! A code no uncollapsed activity takes, so a configure that names one is a
  ! change only when the cancel rule says so, and every later change differs.
  identity_activity = 16 + iand(shared_data%params%nexp_decode, 7)
  identity_now = decode_identity()
  call stream_decode_set_identity(identity_now)
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
  ! On the stream the result files are the client's: the decoders write
  ! none of the worker's (lib/prog_args.f90 lists them), and their lines
  ! and events carry what those files hold.
  result_files = .false.
  ! Without the file, the Fox caller list starts at the first Fox pass as the
  ! worker's does when houndcallers.txt is missing (lib/ft8_engine_kernel.f90).
  fox_list_reset = .true.
  dial_now = -1
  dial_nominal = -1
  dial_due = huge(1.d0)
  published = .false.
  call decode_start()
  call streaming_emit_ready()
  ! Command-line starting values are checked once; the session still runs.
  problem = engine_params_problem(shared_data%params, .true.)
  call gui_range_problem(shared_data%params, TRperiod, problem)
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
                 ! unknown schema version -> decline this frame.
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
                 ! a key's JSON value type mismatched the schema.
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
              next_controls = controls
              call apply_configure_fields(cfg, next_mode, next_TRperiod, next_params)
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
              ! and Tx Freq, unless the frame sets them; a frame with mode also
              ! puts back JT9's F Tol, and a Settings change in fast JT9 does both
              ! again (apply_search_controls). A fast frame in fast JT9 without
              ! mode is no entry.
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
              if (cfg%dial_frequency_out_of_range) then
                 call route_begin()
                 call streaming_emit_error_range('dial_frequency', range_mode,    &
                      'dial_frequency must be a whole number of Hz from 0 to 2**53')
                 call route_end()
                 cycle
              end if
              ! A depth level no setting of WSJT-X's gives, in any mode.
              if (cfg%depth_level_out_of_range .or. cfg%q65_maxiters_level_out_of_range) then
                 range_key = 'depth_level'
                 if (.not. cfg%depth_level_out_of_range) range_key = 'q65_maxiters_level'
                 call route_begin()
                 if (len(range_mode) .gt. 0) then
                    call streaming_emit_error_range(range_key, range_mode,       &
                         range_key // ' must be from 1 to 3 in ' // range_mode)
                 else
                    call streaming_emit_error_range(range_key, '',               &
                         range_key // ' must be from 1 to 3')
                 end if
                 call route_end()
                 cycle
              end if
              if (cfg%contest_type_out_of_range) then
                 call route_begin()
                 call streaming_emit_error_range('contest_type', range_mode,      &
                      'contest_type must be none, na_vhf, eu_vhf, field_day, rtty, ' // &
                      'ww_digi, fox, hound, arrl_digi or q65_pileup')
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
              ! An accepted Settings change in WSJT-X: one of the Settings its
              ! decode reads, those in the params and those the stream keeps
              ! apart (Low sidelobes, Decode after EME delay whatever delay
              ! eme_delay_seconds gives, and the activity, which the packing
              ! collapses: WW Digi, ARRL Digi and Q65 Pileup as NA VHF).
              settings_accept = settings_dialog_change(shared_data%params, next_params) &
                   .or. (next_eme_on .neqv. eme_delay_on)
              if (cfg%low_sidelobes_set) settings_accept = settings_accept .or. &
                   (cfg%low_sidelobes .neqv. receive%low_sidelobes)
              if (cfg%nexp_decode_packed_set) settings_accept = settings_accept .or. &
                   (special_op .ge. 0 .and. cfg%special_op .ne. special_op)
              ! F Tol and the search range, as WSJT-X's controls give them; a
              ! mode key or an accepted Settings change sets the mode up again.
              call apply_search_controls(next_controls, cfg, mode, next_mode, next_params, &
                   cfg%mode_set .or. settings_accept, next_fast, fast_jt9)
              if (next_mode .eq. MODE_JTTY) then
                 range_key = jtty_range_key(next_params)
                 if (len(range_key) .gt. 0) then
                    call route_begin()
                    call jtty_emit_range_error(range_key)
                    call route_end()
                    cycle
                 end if
                 ! And a depth WSJT-X's cannot hold, as in every mode.
                 problem = option_problem()
                 call depth_problem(next_params, problem)
                 if (problem%field .ne. ' ') then
                    call route_begin()
                    call emit_range_error(problem, cfg, next_mode, .false.)
                    call route_end()
                    cycle
                 end if
              else
                 ! A frame that would leave the decoder refusing its decodes,
                 ! or with a value WSJT-X's controls cannot give.
                 problem = engine_params_problem(next_params, .true.)
                 call gui_range_problem(next_params, next_TRperiod, problem)
                 if (next_mode .eq. MODE_FREQCAL) then
                    problem = freqcal_problem(next_params%nfqso, next_params%ntol)
                    call depth_problem(next_params, problem)
                 end if
                 ! WSPR's decode threads, ft8_threads' range in FT8.
                 if (next_mode .eq. MODE_WSPR .and. problem%field .eq. ' ') then
                    if (cfg%wspr_threads_set) then
                       if (cfg%wspr_threads .lt. 0 .or. cfg%wspr_threads .gt. THREADS_MAX) &
                            problem = option_problem('wspr_threads', THREADS_LIMIT)
                    else if (wspr_threads .lt. 0 .or. wspr_threads .gt. THREADS_MAX) then
                       problem = option_problem('wspr_threads', THREADS_LIMIT)
                    end if
                 end if
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
                   ft8_settings_change(shared_data%params, next_params) .or.   &
                   settings_accept
              ! The activity in the decode identity changes where a change of it
              ! cancels (settings_accept's rule): a first configure that names
              ! an activity the command line gave collapsed changes nothing.
              if (cfg%nexp_decode_packed_set) then
                 if (iand(next_params%nexp_decode, 7) .ne.                       &
                      iand(shared_data%params%nexp_decode, 7) .or.               &
                      (special_op .ge. 0 .and. cfg%special_op .ne. special_op))   &
                      identity_activity = cfg%special_op
                 special_op = cfg%special_op
              end if
              controls = next_controls
              if (cfg%wspr_threads_set) wspr_threads = cfg%wspr_threads
              if (cfg%reduce_false_decodes_set) reduce_false = cfg%reduce_false_decodes
              if (cfg%active_stations_set) then
                 active_stations = cfg%active_stations
                 active_made = active_made .or. active_stations
              end if
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
              identity_now = decode_identity()
              call stream_decode_set_identity(identity_now)
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
              ! A change of the receive dial frequency, of any size, is WSJT-X's
              ! receive frequency changing: a waiting final is dropped, live
              ! decodes published under the old frequency (until WSJT-X's
              ! period frequency follows, 0.6 TR later) are dropped from their
              ! next line (make_op, lib/streaming_emit.f90), and in JT65 the next
              ! decode clears the averages. The first value sets it.
              dial_changed = .false.
              if (cfg%dial_frequency_set) then
                 ! On the audio clock, as the passes it compares with.
                 dial_at = stream_stdin_arrival()
                 if (anchored) dial_at = trigger_fire_time(dial_at, t0, stream_samples - 1)
                 call follow_dial(dial_at)
                 if (cfg%dial_frequency .ne. dial_now) then
                    dial_changed = dial_now .ge. 0
                    dial_now = cfg%dial_frequency
                    call stream_decode_set_dial(dial_now)
                    dial_due = dial_at + 0.6d0 * TRperiod
                    ! WSJT-X starts a new JTTY reception for the new
                    ! frequency, live.
                    if (dial_changed .and. mode .eq. MODE_JTTY .and. live_audio) call jtty_close()
                 end if
              end if
              ! WSJT-X holds a7 decodes from live audio for 1.5 periods after
              ! a mode set up, which a configure with mode is, the same mode
              ! too, and after a band change or a frequency picked from its
              ! list, which a7_hold tells (widgets/mainwindow_band.cpp,
              ! OperatingBand.cpp); a later start holds 1.5 periods from it.
              ! Its timer runs on its clock from the event; here on the audio
              ! clock, as the passes it compares with, from the configure's
              ! place (lib/streaming_emit.f90 compares each decode line).
              if (cfg%mode_set .or. (cfg%a7_hold_set .and. cfg%a7_hold)) then
                 hold_at = stream_stdin_arrival()
                 if (anchored) hold_at = trigger_fire_time(hold_at, t0, stream_samples - 1)
                 call streaming_emit_set_a7_hold(hold_at, hold_at + 1.5d0 * TRperiod)
              end if
              call make_op(op, OP_CONFIGURE)
              op%cancels = cancels .or. dial_changed
              op%new_period = mode .ne. prev_mode .or. TRperiod .ne. prev_TRperiod
              ! In ECHO a clear is Echo's (lib/streaming_echo.f90).
              op%clear_set = cfg%clear_average_set .and. mode .ne. MODE_ECHO
              op%clear_average = cfg%clear_average
              if (dial_changed .and. mode .eq. 65) then
                 op%clear_set = .true.
                 op%clear_average = .true.
              end if
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
                    ! With no period to decode again the decode side decodes
                    ! the period in flight, as the GUI does, from what is in
                    ! so far, at the mode's stop, once a decode has been
                    ! triggered; Q65, FST4, FST4W and fast JT9 never do.
                    call make_op(again_op, OP_AGAIN)
                    again_op%k = period%k
                    ! WSJT-X clears its FT8 duplicate keys 10 s into each
                    ! period (widgets/mainwindow.cpp, the seconds 10, 25, 40
                    ! and 55), on a file at its loading: until then they hold
                    ! the period before's.
                    again_op%keys = period_number
                    if (period%k .lt. 10 * 12000) again_op%keys = period_number - 1
                    if (published .and. period%k .gt. 0 .and. .not. fast_on() .and.      &
                         mode .ne. 66 .and. mode .ne. 240 .and. mode .ne. 241 .and. mode .ne. 242) then
                       again_op%params%nzhsym = current_stop()
                       again_op%k = period%k
                       call take_audio(again_op, period%k)
                    end if
                    call decode_submit(again_op)
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
           case (CTRL_CANCEL)
              ! WSJT-X's Stop and a half-duplex transmission's start
              ! (monitor(false)) drop a waiting final, as a configure that
              ! cancels does, and end a JTTY reception; a running decode, its
              ! lines and the period to decode again stay.
              call make_op(op, OP_CONFIGURE)
              op%cancels = .true.
              call decode_submit(op)
              ! Monitoring stopping ends WSJT-X's JTTY reception at once.
              if (mode .eq. MODE_JTTY) call jtty_close()
           case (CTRL_PACK, CTRL_RENDER)
              call streaming_emit_reply(.true.)
              call stream_encode(ctl_buf(1:frame_len), action .eq. CTRL_RENDER)
              call streaming_emit_reply(.false.)
           case (CTRL_HEARD)
              call streaming_emit_reply(.true.)
              call stream_heard(ctl_buf(1:frame_len))
              call streaming_emit_reply(.false.)
           case (CTRL_REVIEW)
              call streaming_emit_reply(.true.)
              call jtty_review_request(ctl_buf(1:frame_len), mode .eq. MODE_JTTY, &
                   live_audio, shared_data%params)
              call streaming_emit_reply(.false.)
           case (CTRL_PARSE_ERR)
              call route_begin()
              call streaming_emit_error_code('configure_parse_error',          &
                   'malformed control frame (not one JSON object with a string t)')
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

  ! JT9 (without fast), JT4 and JT65 decode only at 60 s, as the
  ! GUI runs them.
  pure logical function minute_mode(m)
    integer, intent(in) :: m
    minute_mode = any(m .eq. [4, 9, 65])
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
       ! The GUI's Echo, MSK144 and JTTY decodes count as its first decode too.
       published = .true.
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
    op%wspr_threads = wspr_threads
    op%reduce_false = reduce_false
    ! The GUI's points display, in FT4 and FT8 (bDisplayPoints).
    op%points = active_made .and. (mode .eq. 8 .or. mode .eq. 5) .and.       &
         (special_op .eq. 8 .or. active_stations)
    ! WSJT-X's single-threaded FT8 decode takes Rx 200 in Fox, whatever its
    ! Rx Freq says; its multithreaded decode takes the Rx Freq
    ! (MainWindow::decode). Hound's search to 1000 Hz is the decoder's
    ! (streaming_decode.f90 run_decoder).
    if (mode .eq. 8 .and. iand(op%params%nexp_decode, 7) .eq. 6 .and.        &
         .not. logical(op%params%lmultift8)) op%params%nfqso = 200
    ! The decode identity it is made under, live, as WSJT-X captures it when
    ! it publishes a decode (lib/stream_decode.c).
    op%identity = merge(identity_now, -1_int64, live_audio)
    op%params%ndepth = gui_decoder_depth(op%params, mode)
    ! JT9 windows the spectra it builds itself by bit 0x100 of ndepth; the
    ! bit comes from low_sidelobes alone, not from the depth a client sent.
    op%params%ndepth = iand(op%params%ndepth, not(256))
    if (mode .eq. 9 .and. receive%low_sidelobes) &
         op%params%ndepth = ior(op%params%ndepth, 256)
    op%form = label_form
    ! Live, the receive frequency it is published under: WSJT-X's period
    ! frequency, which catches up with the dial 0.6 TR after a change, as it
    ! stands when the operation's trigger fires (its last sample is due). From
    ! a file none: a change applies to the audio after it, in stream order.
    call follow_dial(trigger_fire_time(op%arrival, op%t0, op%last_sample))
    op%rx = dial_now
    ! A decode's publication sets the period frequency the first time.
    if (kind .eq. OP_PASS .or. (kind .eq. OP_AGAIN .and. published)) then
       if (dial_nominal .lt. 0) dial_nominal = dial_now
       published = .true.
    end if
    op%dial = -1
    if (live_audio) then
       op%dial = dial_now
       if (dial_nominal .ge. 0) op%dial = dial_nominal
    end if
  end subroutine make_op

  ! WSJT-X's decode identity besides the frequency (DecodeOperatingContext
  ! hasSameDecodeIdentity): the mode, the activity, the TR period, the
  ! submode, file input, multithreaded FT8, its start, SuperFox and my call,
  ! as one value below 2**31 (lib/stream_decode.c compares it at each decode
  ! line); the activity is identity_activity.
  integer(int64) function decode_identity()
    integer(int64), parameter :: m = 2147483647_int64
    integer(int64) :: h
    integer :: i
    h = mode
    h = mod(h * 131 + identity_activity, m)
    h = mod(h * 131 + nint(TRperiod * 1000.d0), m)
    h = mod(h * 131 + shared_data%params%nsubmode, m)
    h = mod(h * 131 + merge(1, 0, live_audio), m)
    h = mod(h * 131 + merge(1, 0, logical(shared_data%params%lmultift8)), m)
    h = mod(h * 131 + max(0, shared_data%params%ndecoderstart), m)
    h = mod(h * 131 + merge(1, 0, logical(shared_data%params%b_superfox)), m)
    do i = 1, size(shared_data%params%mycall)
       h = mod(h * 131 + iachar(shared_data%params%mycall(i)), m)
    end do
    decode_identity = h
  end function decode_identity

  ! WSJT-X's period frequency catches up with the dial once 0.6 TR has
  ! passed since its last change (its band-reporting timer), by arrival t.
  subroutine follow_dial(t)
    real(8), intent(in) :: t
    if (dial_due .le. t) then
       dial_nominal = dial_now
       dial_due = huge(1.d0)
    end if
  end subroutine follow_dial

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

  ! Copy the period's first n samples when the queue has room. If the
  ! allocation fails, drain the queue and retry once.
  subroutine take_audio(op, n)
    type(decode_op), intent(inout) :: op
    integer,         intent(in)    :: n
    logical :: ok
    call decode_room()
    call snapshot(op%audio, shared_data%id2, n, ok)
    if (.not. ok) then
       call decode_wait()
       call snapshot(op%audio, shared_data%id2, n, ok)
    end if
    op%memory = .not. ok
  end subroutine take_audio

  ! configure_range_error for a refused engine option: the key, the range in
  ! the mode (none in JTTY, whose range errors carry no mode), and where a
  ! value this frame did not set came from.
  subroutine emit_range_error(problem, frame, in_mode, at_start)
    type(option_problem),   intent(in) :: problem
    type(configure_fields), intent(in) :: frame
    integer,                intent(in) :: in_mode
    logical,                intent(in) :: at_start
    character(len=:), allocatable :: key, name, in_name, detail
    logical :: carried
    key = trim(engine_range_key(problem%field, frame, carried))
    name = ''
    in_name = ''
    if (in_mode .ne. MODE_JTTY) then
       name = trim(streaming_emit_mode_name(in_mode))
       in_name = ' in ' // name
    end if
    if (problem%field .eq. 'search_range') then
       detail = trim(problem%limit) // in_name
    else if (key .eq. 'depth') then
       ! WSJT-X's depth: a decoding level and its Decode menu's bits.
       detail = 'depth must hold a decoding depth ' // trim(problem%limit) // in_name
    else
       detail = key // ' must be ' // trim(problem%limit) // in_name
    end if
    if (at_start) then
       detail = detail // '; the command line set it'
    else if (carried) then
       detail = detail // '; it was set before this frame'
    end if
    call streaming_emit_error_range(key, name, detail)
  end subroutine emit_range_error

  ! problem, engine_params_problem's for params, held to what WSJT-X's
  ! controls give where the engine takes more: the depth (depth_problem),
  ! which the engine does not check in FT8 and checks elsewhere only in its
  ! low bits; Aggressive, a spin box from 0 to 10 (Configuration.ui
  ! sbAggressive) where the engine takes 11, checked where the engine checks
  ! it in JT65; and FT8's ft8_threads, Auto and
  ! 1 to 12 in its menu, where the engine takes up to 24, which the kernel
  ! runs on one thread above 12. And the EME delay, from 0 s to period, the
  ! TR period, in every mode but JTTY and FreqCal, which read none (a frame
  ! with mode sets the delay again), as the engine holds FST4's
  ! (fst4_option_problem), the one decoder that reads its size: it centres
  ! its start search 1 s past it (fst4_decode.f90 fst4_sync_search). FT8
  ! takes any delay but 0 as on (ft8_decode.f90, ft8_engine_kernel.f90), Q65
  ! one above 0 (q65.f90), the others none; WSJT-X's own is 0 or 2.5 s
  ! (MainWindow::decode).
  subroutine gui_range_problem(params, period, problem)
    type(params_block),   intent(in)    :: params
    real(8),              intent(in)    :: period
    type(option_problem), intent(inout) :: problem
    character(len=*), parameter :: AGGRESSIVE_LIMIT = 'from 0 to 10'
    logical :: jt65
    if (problem%field .eq. 'aggressiveness') problem%limit = AGGRESSIVE_LIMIT
    if (problem%field .eq. 'threads') problem%limit = THREADS_LIMIT
    if (problem%field .ne. ' ') return
    jt65 = params%nmode .eq. 65
    call depth_problem(params, problem)
    if (problem%field .ne. ' ') return
    if (params%nmode .eq. 8 .and. params%nmt .gt. THREADS_MAX) then
       problem = option_problem('threads', THREADS_LIMIT)
    else if (jt65 .and. params%naggressive .gt. 10) then
       problem = option_problem('aggressiveness', AGGRESSIVE_LIMIT)
    else if (.not. (params%emedelay .ge. 0 .and. params%emedelay .le. period)) then
       problem = option_problem('eme_delay_seconds', 'from 0 s to the TR period')
    end if
  end subroutine gui_range_problem

  ! problem, unless one is set, for a depth WSJT-X's cannot hold (DEPTH_BITS,
  ! a level from 1 to 3 among them), whatever the mode: JTTY and FreqCal,
  ! which have their own range checks and read no depth, too.
  subroutine depth_problem(params, problem)
    type(params_block),   intent(in)    :: params
    type(option_problem), intent(inout) :: problem
    if (problem%field .ne. ' ') return
    if (iand(params%ndepth, 7) .lt. 1 .or. iand(params%ndepth, 7) .gt. 3) then
       problem = option_problem('depth', 'from 1 to 3')
    else if (iand(params%ndepth, not(DEPTH_BITS)) .ne. 0) then
       problem = option_problem('depth', DEPTH_BITS_LIMIT)
    end if
  end subroutine depth_problem

  ! The depth WSJT-X's decode gives the decoder in mode (MainWindow::decode):
  ! the whole depth in FT8 with its multithreaded decoder chosen, its early
  ! passes and SuperFox Hound's decodes too (widgets/mainwindow.cpp, the
  ! multithreaded block's ndepth=m_ndepth), but for the second early pass
  ! from live audio at decoder start 1, which runs at depth 2 as the GUI's
  ! m_earlyDecode2 pass does (op%depth2, streaming_decode.f90's early
  ! passes); otherwise without the option
  ! bits whose Decode menu items the mode hides (depth &= ~16 to ~128). The
  ! items show only with VHF features: averaging and automatic clear in
  ! JT4, JT65 and Q65 (statusChanged), deep search in JT4 and JT65 (their
  ! setups' applyModeUiState); AP for the DX call in no mode.
  pure integer function gui_decoder_depth(params, mode) result(depth)
    type(params_block), intent(in) :: params
    integer,            intent(in) :: mode
    integer, parameter :: OPTION_BITS = 16 + 32 + 64 + 128
    integer :: shown
    depth = params%ndepth
    if (mode .eq. 8 .and. logical(params%lmultift8)) return
    shown = 0
    if (iand(params%nexp_decode, 64) .ne. 0) then
       if (mode .eq. 4 .or. mode .eq. 65) shown = 16 + 32 + 128
       if (mode .eq. 66) shown = 16 + 128
    end if
    depth = iand(depth, not(OPTION_BITS - shown))
  end function gui_decoder_depth

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
  ! stop (as the GUI's diskDat). A period has rows only from its first block.
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

  ! Advance the half-symbol count and run decodes whose audio is ready.
  subroutine block_work()
    nhsym = max(nhsym, min((period%k - 2048) / HSYM_SAMPLES, 181))
    ! The GUI's early FT8 passes run as soon as their audio is in.
    if (mode .eq. 8) call run_early_ft8(.false.)
    ! As do fast JT9's decode tries.
    call run_fast_tries()
    ! As does each MSK144 block, never refused for a busy decoder.
    ! It decodes on packjt77's own tables, which are the codec's transmit
    ! tables for FST4, FST4W, Q65 and MSK144, as WSJT-X's in-process decoder
    ! decodes on its own (lib/streaming_encode.f90 explains the set).
    if (mode .eq. 144) published = .true.
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
    published = .true.
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
