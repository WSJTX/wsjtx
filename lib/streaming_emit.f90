! Streaming NDJSON emitter.
!
! Hand-rolled JSON output for the streaming protocol.
! Zero-dependency (no Qt, no nlohmann). One line per message on stdout.
!
! Schema (v=1):
!   ready             {"v":1,"t":"ready","modes":[...],"encodes":[...],"protocol":1}
!                     modes are those configure decodes, encodes those pack
!                     and render take (lib/streaming_encode.f90).
!   decode            {"v":1,"t":"decode","mode":"FT8","time":"HHMMSS",
!                      "snr":-12,"dt":0.600000024,"freq":1500,"message":"CQ ...",
!                      "ap":0,"pass":50}
!                     time is the period's label (lib/streaming_io.f90 states
!                     when to send it), always six digits.
!                     dt (and spread) are the decoder's single-precision value
!                     to 9 significant digits, which read back as that value.
!                     jt9's line, which the GUI shows, prints dt as Fortran
!                     f5.1 and spread as f6.3 below 0.95 Hz and f6.2 from
!                     there, each rounded half to even on that single-precision
!                     value, as C's printf does on glibc and macOS (Qt's
!                     QString::number and JavaScript's toFixed round ties away
!                     from zero, and differ on about one FT8 decode in 50).
!                     DecodeEventLine (DecodeEventLine.cpp) renders an event as
!                     jt9's line, in the per-family layouts of
!                     lib/decoder_callbacks.f90, with the time as HHMM for
!                     periods of a minute or more.
!                     message keeps the text jt9 prints, ? and OOO included.
!                     After "message", what jt9's line for the decode shows,
!                     by family:
!                       FT8, FT4, FST4, FST4W: "ap", the N of jt9's aN (0: none).
!                       JT65: "sync" ("#", "#*" or "##") and "flags" (as jt9
!                       prints them, blanks trimmed: "", "f", "d5", "a13", ...).
!                       A candidate below min_sync, which jt9 prints without
!                       them, has "sync":"" and an empty message.
!                       JT4: "sync" ("*" or "#") and "flags"; a period with no
!                       sync, which jt9 prints as time, snr, dt, freq and "$",
!                       has "sync":"" and an empty message.
!                       Q65: "q" (the qN code), "avg" (periods averaged) and
!                       "copied" (jt9's #: the spare 78th bit, the sender
!                       copied our last message).
!                       FST4, FST4W, Q65: "period", the TR period in seconds.
!                       FST4, FST4W: "spread" (Hz) when the decoder measured it.
!                       SuperFox: "superfox":true and "text_width", and none of
!                       the above.
!                       A line of rx_messages.txt, which the FT8 decoder returns
!                       as its decodes (a test hook): "text_width".
!                       "text_width": the columns jt9's line gives the message,
!                       which ends the line; SuperFox pads some texts with
!                       blanks.
!                     Then "pass", the half-symbol stop of the decode pass that
!                     produced it (41, 46 or 47 for an early FT8 pass), on every
!                     decode but MSK144's.
!                     An MSK144 decode adds "training":true after "message"
!                     when configure's train is still true after its window,
!                     where the GUI marks the decode ^.
!   decode_finished   {"v":1,"t":"decode_finished","period_end":"HHMMSS",
!                      "synced":0,"decoded":14,"average":0}
!                     The counts of jt9's <DecodeFinished> for the pass that
!                     ended the period (Q65's average is 1000 x even + odd
!                     periods averaged), not a count of the decode events
!                     sent: an FT8 period's early passes are not in them, a
!                     decode again gives its own pass's, and they are all 0 for
!                     a final pass not run and for MSK144.
!   error             {"v":1,"t":"error","msg":"..."}
!   error (coded)     {"v":1,"t":"error","code":"configure_parse_error","detail":"..."}
!   error (version)   {"v":1,"t":"error","code":"unknown_schema_version","got":N}
!   error (type)      {"v":1,"t":"error","code":"configure_type_error",
!                      "key":"depth","expected":"int","got":"string"}
!   error (range)     {"v":1,"t":"error","code":"configure_range_error",
!                      "key":"rxfreq","mode":"JT65","detail":"..."}
!                     A configure whose result the decoder would refuse (for
!                     JT65+JT9, the stream's rule); the frame is declined
!                     whole, naming the first refused key in the decoder's
!                     order (a carried one when a mode change made it
!                     invalid). "mode" is the mode the frame would leave, and
!                     the stream stays in the one it had. Also sent once after
!                     ready for a command-line value. A configure whose
!                     phase_eq is a well-formed array of another length or
!                     with a non-finite number is declined with it too, and
!                     so, in every mode, is one whose nutc or utc is not a
!                     time of day ("key":"nutc" or "utc"). JTTY's has no
!                     "mode".
!   error (refused)   {"v":1,"t":"error","code":"decode_rejected",
!                      "operation":"decode","status":"invalid","mode":"JT65",
!                      "field":"depth","detail":"depth must be from 1 to 3"}
!                     One per decode attempt the engine refuses; "field" and
!                     "detail" name the decoder option when it has one, in the
!                     decoder's terms (search_low_hz, search_high_hz,
!                     search_range, receive_frequency_hz, tolerance_hz, depth,
!                     submode, minimum_width, trials, qso_progress, contest,
!                     threads, period_seconds, blanker_percent,
!                     eme_delay_seconds, max_drift, utc, ...).
!   pass_skipped      {"v":1,"t":"pass_skipped","mode":"FT8","period_end":"HHMMSS",
!                      "nzhsym":41,"stage":"early","reason":"busy"}
!                     Live audio only (configure's live_audio; a producer that
!                     does not send it gets every pass, however far behind, and
!                     up to 16 MiB of its input buffered): a decode pass the GUI
!                     would not run.
!                     nzhsym is the pass's half-symbol: 41, 46 or 47 for an
!                     early FT8 pass, the mode's stop point for a final (FT8
!                     48 to 50, FT4 21, ...). stage is "early" or "final".
!                     reason "busy": the decoder was busy when the pass's
!                     trigger fired, with a decode running or a final waiting.
!                     A trigger fires when its last sample arrives, or when
!                     that sample is due if later. Audio is due at 12 kHz from
!                     the arrival of the session's first sample; audio the
!                     stream had to wait for moves that start earlier, by no
!                     more than the time it waited and never later, and a
!                     discontinuity sets it again.
!                     "backoff": multithreaded FT8 skips early passes until it
!                     catches up. "replaced": a waiting final gave way to a
!                     later one. "cancelled": a waiting final was dropped as
!                     the GUI drops it: by a configure that changes the mode,
!                     the period, live_audio, or a decoder or station setting
!                     the GUI cancels on; or, when the final is taken up,
!                     because its decode range or submode changed while it
!                     waited. period_end is the label of the period's
!                     decode_finished, which a period whose final is skipped
!                     still sends: one per period, and one more per decode
!                     again (none for a period a mode or period change
!                     discards or a discontinuity drops short of its stop, or
!                     a SuperFox even-sequence period whose stop is below 50).
!                     An MSK144 period a change discards keeps the decodes it
!                     has already sent. Periods' events can
!                     interleave; group them by period_end.
!   msk144_trained    {"v":1,"t":"msk144_trained","call":"W9XYZ",
!                      "file":"./W9XYZ_261019_120512.pcoeff",
!                      "phase_eq":[1.2345678901234567E-001,...]}
!                     MSK144 training completed: the DX call it trained on
!                     (configure's his_call), the phase-response file it wrote
!                     and its five coefficients, which a configure's phase_eq
!                     applies. "file" is the -a directory as given, "/" and
!                     <call>_<yymmdd>_<hhmmss>.pcoeff in local time; it is
!                     null when the file could not be created (a call with
!                     "/" naming no subdirectory of -a, an -a no longer
!                     writable, a file for the call already written that
!                     second), and the coefficients come anyway. Training
!                     takes only decodes whose message holds his_call as
!                     configured, case and all, from its fourth character on.
!                     train:false stops it only before the first decode that
!                     contains his_call; after that it goes on until it
!                     completes, his_call changes, or more than ten frames
!                     average without a fit. Sent for every completed
!                     training, after the decode event, if any, of the window
!                     that completed it, before more input is read; train is
!                     false from then on.
!
!   error (memory)    {"v":1,"t":"error","code":"out_of_memory",
!                      "detail":"no memory to buffer the input"}
!                     The stream could not buffer its input: the session ends,
!                     exit 1.
!
! Activation: lib/jt9.f90 calls streaming_emit_set_enabled(.true.) when
! --stream is parsed; only wsjt-codec runs the stream. Decoder per-mode sites
! in lib/decoder.f90 query streaming_emit_enabled() and call
! streaming_emit_decode() instead of the legacy write(*,...) text format.
! When disabled, every routine in this module is a no-op so WAV and
! shared-memory decoding are unchanged.

module streaming_emit
  use, intrinsic :: iso_fortran_env, only: int64, output_unit
  implicit none
  private

  logical, save :: enabled_ = .false.
  logical, save :: utc_from_iso_ = .false.
  real(8), save :: trperiod_ = 60.d0
  ! decode_finished ends a period's decode; passes before the last hold it.
  logical, save :: finished_held_ = .false.
  ! The stream host's clock, and when it last sent decode_finished by it.
  abstract interface
     function emit_clock() result(t) bind(C)
       use, intrinsic :: iso_c_binding, only: c_double
       real(c_double) :: t
     end function emit_clock
  end interface
  procedure(emit_clock), pointer, save :: clock_ => null()
  real(8), save :: finished_at_ = -huge(1.d0)
  ! The half-symbol stop of the stream host's decode pass (0: none yet).
  integer, save :: pass_ = 0

  public :: streaming_emit_set_enabled
  public :: streaming_emit_set_time_form
  public :: streaming_emit_enabled
  public :: streaming_emit_utc_from_iso
  public :: streaming_emit_ready
  public :: streaming_emit_decode
  public :: streaming_emit_set_pass
  public :: streaming_emit_replay_line
  public :: streaming_emit_decode_finished
  public :: streaming_emit_hold_finished
  public :: streaming_emit_pass_skipped
  public :: streaming_emit_msk144_trained
  public :: streaming_emit_set_clock
  public :: streaming_emit_finished_at
  public :: streaming_emit_error
  public :: streaming_emit_error_code
  public :: streaming_emit_error_version
  public :: streaming_emit_error_type
  public :: streaming_emit_error_range
  public :: streaming_emit_decode_rejected
  public :: streaming_emit_mode_name
  public :: streaming_emit_json_escape

contains

  subroutine streaming_emit_set_enabled(flag)
    logical, intent(in) :: flag
    enabled_ = flag
  end subroutine streaming_emit_set_enabled

  ! Called after each applied configure. An ISO utc is HHMMSS; a numeric nutc
  ! is HHMMSS when the period is under a minute or the value exceeds 2359,
  ! and HHMM otherwise.
  subroutine streaming_emit_set_time_form(utc_set, nutc_set, trperiod)
    logical, intent(in) :: utc_set, nutc_set
    real(8), intent(in) :: trperiod
    if (utc_set) then
       utc_from_iso_ = .true.
    else if (nutc_set) then
       utc_from_iso_ = .false.
    end if
    trperiod_ = trperiod
  end subroutine streaming_emit_set_time_form

  ! Whether times are labelled from an ISO utc (else from a numeric nutc).
  logical function streaming_emit_utc_from_iso()
    streaming_emit_utc_from_iso = utc_from_iso_
  end function streaming_emit_utc_from_iso

  pure function streaming_emit_enabled() result(v)
    logical :: v
    v = enabled_
  end function streaming_emit_enabled

  subroutine streaming_emit_ready()
    if (.not. enabled_) return
    ! Full jt9-internal mode support (FT8/FT4/JT9/JT65/JT65+JT9/JT4/FST4/
    ! FST4W/Q65/MSK144) and JTTY. Modes outside this set (e.g., wsprd C2
    ! streaming, map65 I/Q) may extend this list later.
    write(output_unit, '(a)') &
       '{"v":1,"t":"ready","modes":["FT8","FT4","JT9","JT65","JT65+JT9","JT4","FST4","FST4W",' // &
       '"Q65","MSK144","JTTY"],' // &
       '"encodes":["FT8","FT4","JT4","JT9","JT65","Q65","FST4","FST4W","WSPR","MSK144","JTTY"],"protocol":1}'
    flush(output_unit)
  end subroutine streaming_emit_ready

  ! Emit one decode line.
  !
  !   mode_str  : "FT8" / "FT4" / "JT9" / etc.  (no escaping)
  !   nutc      : 4-digit (HHMM) or 6-digit (HHMMSS) integer time
  !   snr       : SNR in dB (integer; negative allowed)
  !   dt        : DT seconds (float)
  !   freq      : decode frequency offset in Hz (integer)
  !   msg       : decoded message text (gets JSON-escaped + trimmed)
  !   training  : an MSK144 decode made while training
  !   the rest  : what jt9's line shows beyond the decode (the schema above)
  subroutine streaming_emit_decode(mode_str, nutc, snr, dt, freq, msg, utc_is_hhmmss, &
       training, ap, sync, flags, q, avg, copied, period, spread, superfox, text_width)
    character(len=*), intent(in) :: mode_str
    integer,          intent(in) :: nutc
    integer,          intent(in) :: snr
    real,             intent(in) :: dt
    integer,          intent(in) :: freq
    character(len=*), intent(in) :: msg
    logical, optional, intent(in) :: utc_is_hhmmss
    logical, optional, intent(in) :: training
    integer, optional, intent(in) :: ap, q, avg, period, text_width
    character(len=*), optional, intent(in) :: sync, flags
    logical, optional, intent(in) :: copied, superfox
    real, optional, intent(in) :: spread

    character(len=8)   :: time_str
    character(len=320) :: msg_escaped
    character(len=24)  :: dt_str
    character(len=:), allocatable :: marks
    integer            :: msg_len

    if (.not. enabled_) return

    call format_utc_(nutc, time_str, utc_is_hhmmss)
    call streaming_emit_json_escape(msg, msg_escaped, msg_len)
    call format_real_(dt, dt_str)
    marks = ''
    if (present(training)) then
       if (training) marks = ',"training":true'
    end if
    if (present(superfox)) then
       if (superfox) marks = marks // ',"superfox":true'
    end if
    if (present(text_width)) marks = marks // ',"text_width":' // int_text_(text_width)
    if (present(ap)) marks = marks // ',"ap":' // int_text_(ap)
    if (present(sync)) marks = marks // ',"sync":"' // trim(sync) // '"'
    if (present(flags)) marks = marks // ',"flags":"' // trim(flags) // '"'
    if (present(q)) marks = marks // ',"q":' // int_text_(q)
    if (present(avg)) marks = marks // ',"avg":' // int_text_(avg)
    if (present(copied)) marks = marks // ',"copied":' // trim(merge('true ', 'false', copied))
    if (present(period)) marks = marks // ',"period":' // int_text_(period)
    if (present(spread)) marks = marks // ',"spread":' // real_text_(spread)
    if (pass_ .gt. 0 .and. .not. present(training)) marks = marks // ',"pass":' // int_text_(pass_)

    write(output_unit, '(5a,i0,3a,i0,5a)')                                 &
         '{"v":1,"t":"decode","mode":"', trim(mode_str),                   &
         '","time":"', trim(time_str),                                     &
         '","snr":', snr,                                                  &
         ',"dt":', trim(dt_str),                                           &
         ',"freq":', freq,                                                 &
         ',"message":"', msg_escaped(1:msg_len),                           &
         '"', marks, '}'
    flush(output_unit)
  end subroutine streaming_emit_decode

  ! The half-symbol stop of the decode pass the stream host runs next.
  subroutine streaming_emit_set_pass(nzhsym)
    integer, intent(in) :: nzhsym
    pass_ = nzhsym
  end subroutine streaming_emit_set_pass

  ! A line of rx_messages.txt, which the FT8 decoder returns as its decodes
  ! (lib/ft8_engine_kernel.f90): read as jt9's FT8 line, a decode event, else
  ! stderr, so that stdout stays JSON.
  subroutine streaming_emit_replay_line(line)
    use, intrinsic :: iso_fortran_env, only: error_unit
    character(len=*), intent(in) :: line
    character(len=64) :: padded
    integer :: utc, snr, freq, ios
    real :: dt
    padded = line
    read(padded, '(i6,i4,f5.1,i5)', iostat=ios) utc, snr, dt, freq
    if (ios .ne. 0 .or. padded(21:24) .ne. ' ~  ') then
       write(error_unit, '(a)') trim(line)
       return
    end if
    call streaming_emit_decode('FT8', utc, snr, dt, freq, padded(25:60),     &
         .true., text_width=len_trim(padded(25:60)))
  end subroutine streaming_emit_replay_line

  subroutine streaming_emit_hold_finished(hold)
    logical, intent(in) :: hold
    finished_held_ = hold
  end subroutine streaming_emit_hold_finished

  subroutine streaming_emit_set_clock(clock)
    procedure(emit_clock) :: clock
    clock_ => clock
  end subroutine streaming_emit_set_clock

  real(8) function streaming_emit_finished_at()
    streaming_emit_finished_at = finished_at_
  end function streaming_emit_finished_at

  ! counts: jt9's <DecodeFinished> synced, decoded and average (absent: 0).
  subroutine streaming_emit_decode_finished(nutc, utc_is_hhmmss, counts)
    integer, intent(in) :: nutc
    logical, optional, intent(in) :: utc_is_hhmmss
    integer, optional, intent(in) :: counts(3)
    character(len=8)    :: time_str
    integer             :: n(3)
    if (.not. enabled_ .or. finished_held_) return
    ! Stamped before the line goes out, so no answer to it can precede it.
    if (associated(clock_)) finished_at_ = clock_()
    call format_utc_(nutc, time_str, utc_is_hhmmss)
    n = 0
    if (present(counts)) n = counts
    write(output_unit, '(3a,i0,a,i0,a,i0,a)')                              &
         '{"v":1,"t":"decode_finished","period_end":"', trim(time_str),    &
         '","synced":', n(1), ',"decoded":', n(2), ',"average":', n(3), '}'
    flush(output_unit)
  end subroutine streaming_emit_decode_finished

  ! nzhsym is the pass's half-symbol; stage and reason are protocol tokens.
  subroutine streaming_emit_pass_skipped(mode, nutc, utc_is_hhmmss, nzhsym, stage, reason)
    integer,          intent(in) :: mode, nutc, nzhsym
    logical,          intent(in) :: utc_is_hhmmss
    character(len=*), intent(in) :: stage, reason
    character(len=8)             :: time_str
    if (.not. enabled_) return
    call format_utc_(nutc, time_str, utc_is_hhmmss)
    write(output_unit, '(5a,i0,5a)')                                       &
         '{"v":1,"t":"pass_skipped","mode":"', trim(streaming_emit_mode_name(mode)), &
         '","period_end":"', trim(time_str), '","nzhsym":', nzhsym,        &
         ',"stage":"', stage, '","reason":"', reason, '"}'
    flush(output_unit)
  end subroutine streaming_emit_pass_skipped

  ! Each coefficient with 17 significant digits, which keeps every double; a
  ! value JSON cannot hold is null, as is a blank file.
  subroutine streaming_emit_msk144_trained(dx_call, file, terms)
    use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
    character(len=*), intent(in) :: dx_call, file
    real(8),          intent(in) :: terms(5)
    character(len=80)   :: call_escaped
    character(len=3200) :: file_escaped
    character(len=:), allocatable :: file_value
    character(len=24)   :: value(5)
    integer             :: call_len, file_len, i
    if (.not. enabled_) return
    call streaming_emit_json_escape(dx_call, call_escaped, call_len)
    call streaming_emit_json_escape(file, file_escaped, file_len)
    file_value = 'null'
    if (file_len .gt. 0) file_value = '"' // file_escaped(1:file_len) // '"'
    do i = 1, 5
       value(i) = 'null'
       if (ieee_is_finite(terms(i))) write(value(i), '(es24.16e3)') terms(i)
    end do
    write(output_unit, '(15a)')                                            &
         '{"v":1,"t":"msk144_trained","call":"', call_escaped(1:call_len), &
         '","file":', file_value, ',"phase_eq":[',                        &
         (trim(adjustl(value(i))), ',', i = 1, 4), trim(adjustl(value(5))), ']}'
    flush(output_unit)
  end subroutine streaming_emit_msk144_trained

  subroutine streaming_emit_error(msg)
    character(len=*), intent(in) :: msg
    character(len=320)           :: msg_escaped
    integer                      :: msg_len
    if (.not. enabled_) return
    call streaming_emit_json_escape(msg, msg_escaped, msg_len)
    write(output_unit, '(3a)')                                             &
         '{"v":1,"t":"error","msg":"', msg_escaped(1:msg_len), '"}'
    flush(output_unit)
  end subroutine streaming_emit_error

  ! Structured configure-frame error (RFC v1 §4.1). Machine-readable `code`
  ! plus a free-text `detail` (JSON-escaped). Used for configure_parse_error.
  !   {"v":1,"t":"error","code":"<code>","detail":"<detail>"}
  subroutine streaming_emit_error_code(code, detail)
    character(len=*), intent(in) :: code
    character(len=*), intent(in) :: detail
    character(len=320)           :: d_escaped
    integer                      :: d_len
    if (.not. enabled_) return
    call streaming_emit_json_escape(detail, d_escaped, d_len)
    write(output_unit, '(5a)')                                             &
         '{"v":1,"t":"error","code":"', trim(code),                        &
         '","detail":"', d_escaped(1:d_len), '"}'
    flush(output_unit)
  end subroutine streaming_emit_error_code

  ! Unknown schema-version error (RFC v1 §3/§4.1). `got` is the rejected
  ! schema version the producer sent in the configure frame.
  !   {"v":1,"t":"error","code":"unknown_schema_version","got":<n>}
  subroutine streaming_emit_error_version(got)
    integer, intent(in) :: got
    if (.not. enabled_) return
    write(output_unit, '(a,i0,a)')                                         &
         '{"v":1,"t":"error","code":"unknown_schema_version","got":',      &
         got, '}'
    flush(output_unit)
  end subroutine streaming_emit_error_version

  ! Per-key type-mismatch error (RFC v1 §4.1). The producer sent `key` with a
  ! JSON value of type `got` where the schema expects `expected`; the whole
  ! configure frame is declined (no partial application). `key`/`expected`/`got`
  ! are controlled-vocabulary ASCII tokens (no escaping needed).
  !   {"v":1,"t":"error","code":"configure_type_error","key":"<k>",
  !    "expected":"<e>","got":"<g>"}
  subroutine streaming_emit_error_type(key, expected, got)
    character(len=*), intent(in) :: key, expected, got
    if (.not. enabled_) return
    write(output_unit, '(7a)')                                             &
         '{"v":1,"t":"error","code":"configure_type_error","key":"',       &
         trim(key), '","expected":"', trim(expected),                      &
         '","got":"', trim(got), '"}'
    flush(output_unit)
  end subroutine streaming_emit_error_type

  ! A configure the decoder would refuse; the whole frame is declined. `key`
  ! is a configure key name and `mode` a mode name (no escaping needed), or
  ! blank for none.
  !   {"v":1,"t":"error","code":"configure_range_error","key":"<k>",
  !    "mode":"<m>","detail":"<d>"}
  subroutine streaming_emit_error_range(key, mode, detail)
    character(len=*), intent(in) :: key, mode, detail
    character(len=320)           :: d_escaped
    integer                      :: d_len
    if (.not. enabled_) return
    call streaming_emit_json_escape(detail, d_escaped, d_len)
    if (len_trim(mode) .eq. 0) then
       write(output_unit, '(5a)')                                          &
            '{"v":1,"t":"error","code":"configure_range_error","key":"',   &
            trim(key), '","detail":"', d_escaped(1:d_len), '"}'
    else
       write(output_unit, '(7a)')                                          &
            '{"v":1,"t":"error","code":"configure_range_error","key":"',   &
            trim(key), '","mode":"', trim(mode), '","detail":"',           &
            d_escaped(1:d_len), '"}'
    end if
    flush(output_unit)
  end subroutine streaming_emit_error_range

  ! A decode the engine refused. `operation`, `status` and `field` are the
  ! engine's controlled-vocabulary tokens; `field` and `limit` are blank when
  ! no decoder option is at fault.
  !   {"v":1,"t":"error","code":"decode_rejected","operation":"<o>",
  !    "status":"<s>","mode":"<m>"[,"field":"<f>","detail":"<f> must be <l>"]}
  subroutine streaming_emit_decode_rejected(operation, status, mode, field, limit)
    character(len=*), intent(in) :: operation, status, field, limit
    integer, intent(in)          :: mode
    character(len=:), allocatable :: rule
    if (.not. enabled_) return
    ! The search range's limit is already a whole rule.
    rule = trim(field) // ' must be ' // trim(limit)
    if (field .eq. 'search_range') rule = trim(limit)
    if (len_trim(field) .eq. 0) then
       write(output_unit, '(7a)')                                          &
            '{"v":1,"t":"error","code":"decode_rejected","operation":"',   &
            trim(operation), '","status":"', trim(status), '","mode":"',   &
            trim(streaming_emit_mode_name(mode)), '"}'
    else
       write(output_unit, '(11a)')                                         &
            '{"v":1,"t":"error","code":"decode_rejected","operation":"',   &
            trim(operation), '","status":"', trim(status), '","mode":"',   &
            trim(streaming_emit_mode_name(mode)), '","field":"',           &
            trim(field), '","detail":"', trim(rule), '"}'
    end if
    flush(output_unit)
  end subroutine streaming_emit_decode_rejected

  ! The protocol's name for a mode number, or the number.
  function streaming_emit_mode_name(mode) result(name)
    integer, intent(in) :: mode
    character(len=12)   :: name
    select case (mode)
    case (4);       name = 'JT4'
    case (5);       name = 'FT4'
    case (8);       name = 'FT8'
    case (9);       name = 'JT9'
    case (65);      name = 'JT65'
    case (66);      name = 'Q65'
    case (74);      name = 'JT65+JT9'
    case (144);     name = 'MSK144'
    case (240);     name = 'FST4'
    case (241, 242); name = 'FST4W'
    case default
       write(name, '(i0)') mode
    end select
  end function streaming_emit_mode_name

  ! Internals -------------------------------------------------------------

  ! A single-precision value as a plain JSON number of 9 significant digits,
  ! which reads back as the same value, and a client renders as jt9's Fw.d
  ! does without rounding twice. At least one fraction digit, so that a
  ! negative zero stays a real.
  subroutine format_real_(x, str)
    use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
    real,             intent(in)  :: x
    character(len=*), intent(out) :: str
    character(len=24) :: e
    character(len=9)  :: digits
    character(len=:), allocatable :: whole, fraction
    integer :: ex, last
    if (.not. ieee_is_finite(x)) then
       str = 'null'
       return
    end if
    write(e, '(es16.8e3)') abs(x)
    e = adjustl(e)
    digits = e(1:1) // e(3:10)
    read(e(12:15), '(i4)') ex
    if (x .eq. 0.0) ex = 0
    if (ex .ge. 0) then
       whole = digits(1:min(9, ex + 1)) // repeat('0', max(0, ex + 1 - 9))
       fraction = digits(min(9, ex + 1) + 1:)
    else
       whole = '0'
       fraction = repeat('0', -ex - 1) // digits
    end if
    last = len_trim(fraction)
    do while (last .gt. 1)
       if (fraction(last:last) .ne. '0') exit
       last = last - 1
    end do
    if (last .eq. 0) then
       fraction = '0'
    else
       fraction = fraction(1:max(1, last))
    end if
    str = whole // '.' // fraction
    if (sign(1.0, x) .lt. 0.0) str = '-' // str
  end subroutine format_real_

  function real_text_(x) result(text)
    real, intent(in) :: x
    character(len=:), allocatable :: text
    character(len=24) :: s
    call format_real_(x, s)
    text = trim(s)
  end function real_text_

  function int_text_(n) result(text)
    integer, intent(in) :: n
    character(len=:), allocatable :: text
    character(len=12) :: s
    write(s, '(i0)') n
    text = trim(s)
  end function int_text_

  ! Format the integer nutc time as 6 chars. 4-digit HHMM gets
  ! zero-extended to HHMM00 so consumers always see a 6-char field.
  subroutine format_utc_(nutc, str, utc_is_hhmmss)
    integer,          intent(in)  :: nutc
    character(len=*), intent(out) :: str
    logical, optional, intent(in) :: utc_is_hhmmss
    integer :: hh, mm, ss
    logical :: full_time
    full_time=utc_from_iso_.or.trperiod_.lt.60.d0.or.nutc.gt.2359
    if(present(utc_is_hhmmss)) full_time=full_time.or.utc_is_hhmmss
    str = '000000'
    if (nutc .lt. 0) then
       return
    else if (full_time) then
       hh = nutc / 10000
       mm = mod(nutc/100, 100)
       ss = mod(nutc, 100)
       write(str, '(i2.2,i2.2,i2.2)') hh, mm, ss
    else
       hh = nutc / 100
       mm = mod(nutc, 100)
       write(str, '(i2.2,i2.2,a2)') hh, mm, '00'
    end if
  end subroutine format_utc_

  ! Minimal RFC-8259 JSON string escape covering the chars that can
  ! appear in WSJT decode payloads:
  !   - "  ->  \"
  !   - \  ->  \\
  !   - \b \t \n \f \r  ->  \b \t \n \f \r
  !   - other 0..0x1F   ->  \u00xx
  !   - everything else (incl. printable ASCII, UTF-8 bytes) passes through
  !
  ! Trailing trim: the input is silently trimmed to its non-blank length
  ! before escaping (decoder messages are right-padded fixed-width strings).
  ! Output buffer must be large enough for worst case 6x input len.
  subroutine streaming_emit_json_escape(in_str, out_str, out_len)
    character(len=*), intent(in)  :: in_str
    character(len=*), intent(out) :: out_str
    integer,          intent(out) :: out_len

    integer :: i, n, code
    character :: c

    n = len_trim(in_str)
    out_len = 0
    out_str = ' '

    do i = 1, n
       c = in_str(i:i)
       code = iachar(c)
       select case (code)
       case (iachar('"'))
          call append_pair_('\', '"', out_str, out_len)
       case (iachar('\'))
          call append_pair_('\', '\', out_str, out_len)
       case (8)   ! BS
          call append_pair_('\', 'b', out_str, out_len)
       case (9)   ! HT
          call append_pair_('\', 't', out_str, out_len)
       case (10)  ! LF
          call append_pair_('\', 'n', out_str, out_len)
       case (12)  ! FF
          call append_pair_('\', 'f', out_str, out_len)
       case (13)  ! CR
          call append_pair_('\', 'r', out_str, out_len)
       case (0:7, 11, 14:31)
          call append_unicode_(code, out_str, out_len)
       case default
          out_len = out_len + 1
          out_str(out_len:out_len) = c
       end select
    end do
  end subroutine streaming_emit_json_escape

  subroutine append_pair_(a, b, buf, idx)
    character,        intent(in)    :: a, b
    character(len=*), intent(inout) :: buf
    integer,          intent(inout) :: idx
    idx = idx + 1; buf(idx:idx) = a
    idx = idx + 1; buf(idx:idx) = b
  end subroutine append_pair_

  subroutine append_unicode_(code, buf, idx)
    integer,          intent(in)    :: code
    character(len=*), intent(inout) :: buf
    integer,          intent(inout) :: idx
    character(len=4) :: hex4
    write(hex4, '(z4.4)') code
    idx = idx + 1; buf(idx:idx) = '\'
    idx = idx + 1; buf(idx:idx) = 'u'
    buf(idx+1:idx+4) = hex4
    idx = idx + 4
  end subroutine append_unicode_

end module streaming_emit
