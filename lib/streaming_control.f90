! Streaming control-frame parser.
!
! Parses control JSON frames received on stdin via the streaming
! framing. These frame types are recognized:
!
!   {"t":"configure","mode":"FT8","depth":3,"rxfreq":1500,
!                    "mycall":"KJ5HST","mygrid":"EM18","trperiod":15}
!   {"t":"halt"}
!   {"t":"discontinuity"}   the next audio sample does not follow the last
!                           one; lib/streaming_io.f90 states what it ends.
!   {"t":"pack",...}, {"t":"render",...}   JTTY encode requests, read by
!                           parse_encode_request and parse_render_request;
!                           lib/streaming_jtty.f90 states them.
!
! Hand-rolled flat-JSON parser. Callers should treat unknown "t" values
! as forward-compat extensions (mirrors the consumer-ignores-unknown
! discipline from the ready handshake).
!
! Restrictions (deliberate scope):
!   - Flat objects only; no nested objects, and no arrays except
!     configure's phase_eq.
!   - Numeric values must be plain decimal (no scientific notation),
!     except where a key's rule says otherwise (render's freq and each
!     phase_eq element are JSON numbers). Configure's integer keys must be
!     plain JSON integers: a fraction, an exponent or another form is not
!     reported as an error, and is ignored or misread.
!   - String values are JSON strings: \" \\ \/ \b \f \n \r \t and \uXXXX
!     (a surrogate pair for a character above U+FFFF) decode to UTF-8. A
!     string with a malformed escape, a lone surrogate or no closing quote
!     is unreadable: a configure key is left unset (not a type error; an
!     unreadable "mode" is unknown_mode), and an unreadable "t" is a parse
!     error.

module streaming_control
  use, intrinsic :: iso_fortran_env, only: error_unit
  implicit none
  private

  ! Action returned by parse_control_frame.
  integer, parameter, public :: CTRL_UNKNOWN   = 0
  integer, parameter, public :: CTRL_CONFIGURE = 1
  integer, parameter, public :: CTRL_HALT      = 2
  integer, parameter, public :: CTRL_PARSE_ERR = 3
  integer, parameter, public :: CTRL_DISCONTINUITY = 4
  integer, parameter, public :: CTRL_PACK      = 5
  integer, parameter, public :: CTRL_RENDER    = 6
  ! JTTY is selected by name only: no jt9 mode number, so valid_mode_int_
  ! rejects it and "mode":<int> cannot select it.
  integer, parameter, public :: MODE_JTTY = 1000
  ! downsam9 expands npts8 by 8 before filling its NFFT1 input buffer.
  integer, parameter, public :: NPTS_C0_ARRAY_MIN = 1
  integer, parameter, public :: NPTS_C0_ARRAY_MAX = 81648

  type, public :: configure_fields
     ! Each "*_set" flag indicates whether the JSON contained that key.
     ! Callers apply only the fields that were set so partial updates work.
     logical                :: mode_set     = .false.
     integer                :: mode         = 0
     logical                :: mode_invalid = .false.
     logical                :: depth_set    = .false.
     integer                :: depth        = 0
     logical                :: rxfreq_set   = .false.
     integer                :: rxfreq       = 0
     logical                :: trperiod_set = .false.
     real(8)                :: trperiod     = 0.d0
     logical                :: submode_set  = .false.
     integer                :: submode      = 0
     logical                :: mycall_set   = .false.
     character(len=12)      :: mycall       = ' '
     logical                :: mygrid_set   = .false.
     character(len=6)       :: mygrid       = ' '
     ! The same two values for the transmitter, which sends them whole
     ! (mycall and mygrid keep the decoder's widths). *_tx_long: the value
     ! is longer than 64 characters, so no transmission may use it.
     ! *_unreadable: the key is a string that does not decode, which leaves
     ! the key unset but must stop the transmitter using the earlier value.
     character(len=64)      :: mycall_tx      = ' '
     logical                :: mycall_tx_long = .false.
     logical                :: mycall_unreadable = .false.
     character(len=64)      :: mygrid_tx      = ' '
     logical                :: mygrid_tx_long = .false.
     logical                :: mygrid_unreadable = .false.
     ! Decode-bandwidth fields. Without these, the streaming
     ! default ntol=20 confines decoding to ±20 Hz around audio offset 0,
     ! missing the entire FT8 200-3000 Hz passband. Apply BEFORE the per-mode
     ! policy block in apply_configure_fields so explicit values aren't capped by
     ! min(ntol, 1000).
     logical                :: ntol_set     = .false.
     integer                :: ntol         = 0
     logical                :: nfa_set      = .false.
     integer                :: nfa          = 0
     logical                :: nfb_set      = .false.
     integer                :: nfb          = 0
     ! Per-period UTC as HHMMSS integer; in periods of a minute or more a
     ! value up to 2359 is read as HHMM. Threaded into
     ! params%nutc so streaming_emit_decode can format the time field
     ! correctly. Producer should send a fresh nutc in each period's
     ! configure frame (mirrors WAV-path filename-derived per-invocation
     ! UTC). MSK144 labels each decode when its block is decoded, so its
     ! period's label belongs before the period's first 7,168 samples
     ! (lib/streaming_io.f90).
     logical                :: nutc_set     = .false.
     integer                :: nutc         = 0
     ! Body schema version (RFC v1 §3). Absent => default 1 (forward-compat
     ! with un-versioned producers). The receiver declines a frame whose
     ! version it does not recognize (streaming_io.f90 CTRL_CONFIGURE arm).
     logical                :: version_set  = .false.
     integer                :: version      = 1
     ! Phase 4 (RFC v1 §2): operator identity + frequency-window fields, flat
     ! 1:1 onto params_block. Char widths match the bind(C) struct exactly
     ! (his_call/my_b_call/his_b_call=12, his_grid=6) — applied via transfer()
     ! in apply_configure_fields. Over-length callsign/grid keeps the current silent
     ! truncation (consistent with mycall/mygrid; stricter validation is
     ! out of scope). my_call_standard/his_call_standard are the
     ! schema's first BOOLEAN keys (-> logical(c_bool) lmycallstd/lhiscallstd).
     logical                :: his_call_set          = .false.
     character(len=12)      :: his_call              = ' '
     logical                :: his_grid_set          = .false.
     character(len=6)       :: his_grid              = ' '
     logical                :: my_b_call_set         = .false.
     character(len=12)      :: my_b_call             = ' '
     logical                :: his_b_call_set        = .false.
     character(len=12)      :: his_b_call            = ' '
     logical                :: my_call_standard_set  = .false.
     logical                :: my_call_standard      = .false.
     logical                :: his_call_standard_set = .false.
     logical                :: his_call_standard     = .false.
     ! Frequency-window ints. tx_audio_offset_hz -> nftx DECOUPLES the tx side
     ! from rxfreq (which sets both nfqso AND nftx), so it must apply AFTER the
     ! rxfreq block. jt65_jt9_split_hz -> nfsplit (a mid-stream mode switch
     ! does not auto-replay init-time fsplit).
     ! max_drift_hz -> max_drift (Q65 drift search; default 0).
     logical                :: tx_audio_offset_hz_set = .false.
     integer                :: tx_audio_offset_hz     = 0
     logical                :: jt65_jt9_split_hz_set  = .false.
     integer                :: jt65_jt9_split_hz      = 0
     logical                :: max_drift_hz_set       = .false.
     integer                :: max_drift_hz           = 0
     ! Phase 5 (RFC v1 §2): decoder-tuning basic fields, flat 1:1 onto
     ! params_block. dt_tolerance_seconds/eme_delay_seconds are the schema's
     ! first real(8) members; they narrow to real(c_float) dttol/emedelay at
     ! apply (§4 — narrowing documented in streaming_apply.f90 apply_configure_fields).
     ! robust_mode/nagain_flag/clear_average are BOOLEANS -> logical(c_bool)
     ! nrobust/nagain/nclearave (reuse the Phase 4 get_bool_/accept_bool path).
     ! tx_mode is an enum (JT9->9 / JT65->65, or a raw int) -> ntxmode, mirroring
     ! the "mode" parse. The stream host sets kin and nzhsym before each decode
     ! (streaming_io.f90), so kin_samples/nzhsym_per_period do not reach the
     ! decoder. The host sets emedelay as the GUI does (2.5 with
     ! decode_after_eme_delay, else 0): 0 at start, and again when a frame sets
     ! the mode, the TR period or that key, unless the frame gives
     ! eme_delay_seconds. min_width is JT4's; min_sync JT4's, JT65's and
     ! JT65+JT9's; n_2pass JT65's and JT65+JT9's; clear_average JT4's, JT65's,
     ! JT65+JT9's and Q65's; robust_mode, tx_mode and npts_c0_array reach only
     ! JT65+JT9; no decoder reads dt_tolerance_seconds. nagain_flag asks once to
     ! decode the last period again (streaming_io.f90). With live audio it is
     ! refused while the decoder is busy (decode_again_unavailable, detail "the
     ! decoder is busy"), as the GUI's Decode button is; send it again after a
     ! decode_finished.
     logical                :: dt_tolerance_seconds_set = .false.
     real(8)                :: dt_tolerance_seconds     = 0.d0
     logical                :: eme_delay_seconds_set    = .false.
     real(8)                :: eme_delay_seconds        = 0.d0
     logical                :: kin_samples_set          = .false.
     integer                :: kin_samples              = 0
     logical                :: nzhsym_per_period_set    = .false.
     integer                :: nzhsym_per_period        = 0
     logical                :: npts_c0_array_set        = .false.
     integer                :: npts_c0_array            = 0
     logical                :: min_width_set            = .false.
     integer                :: min_width                = 0
     logical                :: min_sync_set             = .false.
     integer                :: min_sync                 = 0
     logical                :: n_2pass_set              = .false.
     integer                :: n_2pass                  = 0
     logical                :: robust_mode_set          = .false.
     logical                :: robust_mode              = .false.
     logical                :: nagain_flag_set          = .false.
     logical                :: nagain_flag              = .false.
     logical                :: tx_mode_set              = .false.
     integer                :: tx_mode                  = 0
     logical                :: clear_average_set        = .false.
     logical                :: clear_average            = .false.
     ! The GUI's "Decode after EME delay" (later stop points, 2.5 s EME delay)
     ! and its live-audio/WAV-file distinction; the stream host keeps both.
     ! Live audio also skips the passes the GUI's busy decoder would
     ! (pass_skipped, lib/streaming_emit.f90); a file never does. A live
     ! producer must send live_audio: without it every pass runs, however far
     ! behind, and up to 16 MiB of input is buffered.
     logical                :: decode_after_eme_delay_set = .false.
     logical                :: decode_after_eme_delay     = .false.
     logical                :: live_audio_set           = .false.
     logical                :: live_audio               = .false.
     ! MSK144's receive options, in every mode, each in force until another
     ! configure sets it: the GUI's "Sh" (short_messages) and "SWL" check
     ! boxes, each independent of the other, its "Measure phase response"
     ! (train; the decoder sets it false when training completes,
     ! msk144_trained in lib/streaming_emit.f90), and the phase-equalization
     ! coefficients its Equalization Tools apply (phase_eq: an array of exactly
     ! five finite JSON numbers, exponents allowed; all zeros, the default, is
     ! no equalization). A phase_eq that is not JSON's grammar declines the
     ! frame with configure_parse_error, one that is not an array of numbers
     ! with configure_type_error (expected "array of numbers"), and an array of
     ! another length or with a number too large to hold with
     ! configure_range_error. lib/decode_msk144.f90 states the decode.
     logical                :: short_messages_set       = .false.
     logical                :: short_messages           = .false.
     logical                :: swl_set                  = .false.
     logical                :: swl                      = .false.
     logical                :: train_set                = .false.
     logical                :: train                    = .false.
     logical                :: phase_eq_set             = .false.
     real(8)                :: phase_eq(5)              = 0.d0
     logical                :: phase_eq_malformed       = .false.
     logical                :: phase_eq_out_of_range    = .false.
     ! Phase 6 (RFC v1 §2): FT8/FT4 specifics cluster — 19 bools + 5 ints, flat
     ! 1:1 onto params_block; apply_configure_fields (streaming_apply.f90) says
     ! which decoder reads each. The parse checks types only; the stream
     ! declines a configure the decoder would refuse (streaming_io.f90).
     ! nagain_filter/stop_hint/hint_mode map to
     ! logical(c_bool) nagainfil/nstophint/nhint despite the n-prefix. Bools
     ! reuse the Phase 4/5 get_bool_/accept_bool path; the 5 ints use get_int_.
     logical                :: multithreaded_ft8_set     = .false.
     logical                :: multithreaded_ft8         = .false.
     logical                :: ft8_cycles_set            = .false.
     integer                :: ft8_cycles                = 0
     logical                :: ft8_rxf_sensitivity_set   = .false.
     integer                :: ft8_rxf_sensitivity       = 0
     logical                :: ft8_threads_set           = .false.
     integer                :: ft8_threads               = 0
     logical                :: ft8_decoder_start_set     = .false.
     integer                :: ft8_decoder_start         = 0
     logical                :: ft8_low_threshold_set     = .false.
     logical                :: ft8_low_threshold         = .false.
     logical                :: ft8_subpass_set           = .false.
     logical                :: ft8_subpass               = .false.
     logical                :: ft8_ap_on_set             = .false.
     logical                :: ft8_ap_on                 = .false.
     logical                :: ap_cq_only_set            = .false.
     logical                :: ap_cq_only                = .false.
     logical                :: ap_my_call_set            = .false.
     logical                :: ap_my_call                = .false.
     logical                :: jt65_ap_on_set            = .false.
     logical                :: jt65_ap_on                = .false.
     logical                :: ap_width_hz_set           = .false.
     integer                :: ap_width_hz               = 0
     logical                :: hide_ft8_duplicates_set   = .false.
     logical                :: hide_ft8_duplicates       = .false.
     logical                :: common_ft8b_set           = .false.
     logical                :: common_ft8b               = .false.
     logical                :: enable_dxc_search_set     = .false.
     logical                :: enable_dxc_search         = .false.
     logical                :: wide_dxc_search_set       = .false.
     logical                :: wide_dxc_search           = .false.
     logical                :: superfox_mode_set         = .false.
     logical                :: superfox_mode             = .false.
     logical                :: even_sequence_set         = .false.
     logical                :: even_sequence             = .false.
     logical                :: hound_mode_set            = .false.
     logical                :: hound_mode                = .false.
     logical                :: multi_instance_set        = .false.
     logical                :: multi_instance            = .false.
     logical                :: skip_tx1_set              = .false.
     logical                :: skip_tx1                  = .false.
     logical                :: nagain_filter_set         = .false.
     logical                :: nagain_filter             = .false.
     logical                :: stop_hint_set             = .false.
     logical                :: stop_hint                 = .false.
     logical                :: hint_mode_set             = .false.
     logical                :: hint_mode                 = .false.
     ! Phase 7 (RFC v1 §2): diagnostic / sequencing fields — 2 bools + 4 ints,
     ! flat 1:1 onto params_block; apply_configure_fields (streaming_apply.f90)
     ! says which decoder reads each. The parse checks types only; the stream
     ! declines a configure the decoder would refuse (streaming_io.f90). The 4
     ! ints use get_int_; the 2 bools (currently_txing/mode_changed,
     ! logical(c_bool) in jt9com.f90) reuse the get_bool_/accept_bool path.
     !
     ! OMITTED Phase 7 wire keys (intentionally NOT exposed in v1):
     !   utc_list / utc_list_0..9 / nlist_count — map to listutc/nlist, which
     !     no decoder reads.
     !   datetime — the decoder copies it into a local it never uses; only
     !     date->yymmdd carries time (Phase 8, §6.1).
     !   from_wav_file (ndiskdat) / new_data_flag (newdat) — receiver-managed:
     !     the receiver sets newdat per pass, and ndiskdat from live_audio.
     logical                :: last_tx_seconds_ago_set   = .false.
     integer                :: last_tx_seconds_ago       = 0
     logical                :: currently_txing_set       = .false.
     logical                :: currently_txing           = .false.
     logical                :: mode_changed_set          = .false.
     logical                :: mode_changed              = .false.
     logical                :: qso_progress_state_set    = .false.
     integer                :: qso_progress_state        = 0
     logical                :: sec_band_changed_set      = .false.
     integer                :: sec_band_changed          = 0
     logical                :: delay_units_set           = .false.
     integer                :: delay_units               = 0
     ! Phase 8 (RFC v1 §2/§6): string & scaled / derived fields. Unlike Phases
     ! 4-7 (flat value-extract), these REWRITE the wire value into the legacy
     ! encoding AT PARSE, so the member holds the DERIVED legacy int, not the raw
     ! wire form:
     !   utc   ("HH:MM:SS")   -> utc_nutc      = hh*10000+mm*100+ss   (§6.3)
     !   date  ("YYYY-MM-DD") -> date_yymmdd   = (yr-2000)*10000+mo*100+day (§6.4)
     !   n_trials (int)       -> n_trials_nranera = encode(n_trials)   (§6.6)
     ! parse_iso_time_/parse_iso_date_/ntrials_to_nranera_ do the work; a
     ! malformed ISO string leaves the key UNSET (value-domain silent-skip, like a
     ! bad mode name — type validation only, §4.1). TWO keys carry a PARSE-TIME
     ! VALUE check that emits configure_type_error (the schema's first value-domain
     ! errors, both RFC-mandated): n_trials NOT in {10^N, 3*10^N} (§6.6 — else a
     ! bad nranera silently corrupts ntrials at decoder.f90:100-102) and date with
     ! year >= 2100 (§6.4 — the 2000+offset YYMMDD encoding caps at 2099).
     !
     ! PRECEDENCE (§4.1): utc is a SECOND route to nutc alongside the legacy int
     ! key nutc (kept for the legacy subset). utc_nutc is stored SEPARATELY from
     ! nutc; apply_configure_fields applies nutc THEN utc_nutc, so utc WINS when a
     ! frame carries both (richer ISO form). Order-independent at apply (the apply
     ! sequence encodes precedence, not the parse order).
     !
     ! apply_configure_fields (streaming_apply.f90) says which decoder reads
     ! each. None is an array index.
     ! candthin_threshold/dt_center_seconds are real(8) members scaled at apply
     ! (§4 — declare real(8), nint(wire*100), do NOT pre-store as int).
     logical                :: utc_set                   = .false.
     integer                :: utc_nutc                  = 0
     logical                :: date_set                  = .false.
     integer                :: date_yymmdd               = 0
     logical                :: n_trials_set              = .false.
     integer                :: n_trials_nranera          = 0
     logical                :: candthin_threshold_set    = .false.
     real(8)                :: candthin_threshold        = 0.d0
     logical                :: dt_center_seconds_set     = .false.
     real(8)                :: dt_center_seconds         = 0.d0
     ! ===== Phase 9 (RFC v1 §2.4/§2.5/§6.1/§6.2): bit-packed fields ===========
     ! ndepth + nexp_decode are PACKED at parse (the post-pass pack_phase9_, called
     ! after all keys are read) into the two derived *_packed members below, so
     ! apply stays flat (mirrors Phase 8's derive-at-parse). The 9 unpacked keys +
     ! their _set flags drive the pack; the post-pass also enforces the two
     ! atomic-decline errors (the legacy depth full-int co-present with ANY unpacked
     ! ndepth key -> conflicting encodings; depth_level vs q65_maxiters_level
     ! inconsistency). Only depth_level is decode-observable on FT8/JT9 (the fixtures
     ! run at depth=3 -> ndepth=3; depth_level=1 -> ndepth=1 changes the FT8 pass
     ! structure ft8_decode.f90:178/182 and the JT9 iand(ndepth,7) tests
     ! jt9_decode.f90:88,92) — the other 8 are Q65/JT65/FST4/JT4 or contest-branch
     ! only (use_averaging q65_decode.f90:221/477+jt65_decode.f90:258;
     ! deep_ap_search jt4_decode.f90:393+extract.f90:182; q65_auto_clear_average
     ! q65_decode.f90:337/449; single_decode/vhf_features/noise_blanker reach
     ! FST4/JT65 not the single-pass FT8 my_ft8%decode) -> parse-unit +
     ! apply-routing-unit + no-regression (like Phase 4/6/7). NONE indexes an array
     ! raw, so no out-of-range apply guard is needed; the pack instead
     ! MASKS depth_level to 3 bits / q65_maxiters to 2 bits and CLAMPS the NB byte to
     ! [0,255], so an out-of-range producer value cannot corrupt a sibling bit.
     ! contest_type holds the COLLAPSED ncontest int (0-7) resolved at parse by
     ! contest_string_to_int_ (SpecOp 5/8/9 -> 1; Configuration.hpp:318 enum).
     ! -- ndepth group --
     logical                :: depth_level_set            = .false.
     integer                :: depth_level                = 0
     logical                :: q65_maxiters_level_set     = .false.
     integer                :: q65_maxiters_level         = 0
     logical                :: use_averaging_set          = .false.
     logical                :: use_averaging              = .false.
     logical                :: deep_ap_search_set         = .false.
     logical                :: deep_ap_search             = .false.
     logical                :: q65_auto_clear_average_set = .false.
     logical                :: q65_auto_clear_average     = .false.
     ! -- nexp_decode group --
     logical                :: contest_type_set           = .false.
     integer                :: contest_type               = 0   ! collapsed ncontest (0-7)
     logical                :: single_decode_set          = .false.
     logical                :: single_decode              = .false.
     logical                :: vhf_features_set           = .false.
     logical                :: vhf_features               = .false.
     logical                :: noise_blanker_level_set    = .false.
     integer                :: noise_blanker_level        = 0
     ! -- derived packed ints (computed by pack_phase9_; apply routes these flat) --
     logical                :: ndepth_packed_set          = .false.
     integer                :: ndepth_packed              = 0
     logical                :: nexp_decode_packed_set     = .false.
     integer                :: nexp_decode_packed         = 0
  end type configure_fields

  ! Per-key type-validation result (RFC v1 §4.1, Phase 2). When a configure
  ! frame carries a key whose JSON value type mismatches the schema (e.g.
  ! {"depth":"abc"}), parse_control_frame records the FIRST such error here and
  ! the caller declines the ENTIRE frame with configure_type_error (no partial
  ! application). `present=.false.` means the frame is well-typed.
  type, public :: control_type_error
     logical            :: present  = .false.
     character(len=24)  :: key      = ' '   ! offending key (no quotes)
     character(len=16)  :: expected = ' '   ! "int"/"real"/"string"/"string|int"
     character(len=8)   :: got      = ' '   ! "string"/"number"/"bool"/"null"
  end type control_type_error

  ! An encode request. id_ok is .false. unless "id" is an integer
  ! from -2147483648 to 2147483647. problem is '' for a well-formed request,
  ! else why it is not, with the key at fault in problem_key ('' for none).
  ! serial and report have no default: *_given says whether the request
  ! carries them.
  type, public :: encode_request
     logical :: id_ok        = .false.
     integer :: id           = 0
     logical :: is_template  = .false.
     character(len=:), allocatable :: text      ! text or template, UTF-8
     logical :: final        = .true.
     integer :: profile      = 0                ! none 0, field_day 1, rtty 2
     character(len=:), allocatable :: his_call, exchange
     logical :: serial_given = .false.
     integer :: serial       = 0
     logical :: report_given = .false.
     integer :: report       = 0
     real(8) :: freq         = 0.d0             ! render only
     integer :: rate         = 0                ! render only
     character(len=:), allocatable :: problem, problem_key
  end type encode_request

  public :: parse_control_frame
  public :: mode_string_to_int
  public :: parse_encode_request
  public :: parse_render_request

contains

  ! Parse a UTF-8 JSON line into an action and (for CONFIGURE) fields.
  !
  ! Type validation (RFC v1 §4.1, Phase 2): every configure key is checked
  ! against its schema JSON type BEFORE its value is parsed. A key present with
  ! a mismatched JSON type (e.g. {"depth":"abc"}) records the FIRST such error in
  ! `terr`; the caller (streaming_io.f90 CTRL_CONFIGURE arm) then declines the
  ! ENTIRE frame with configure_type_error — no partial application. Value-domain
  ! issues (unknown mode name, over-length callsign, a fractional value for an
  ! int key) are NOT type errors: they preserve the current silent behavior
  ! (the key is simply left unset).
  subroutine parse_control_frame(buf, action, cfg, terr)
    character(len=*),         intent(in)  :: buf
    integer,                  intent(out) :: action
    type(configure_fields),   intent(out) :: cfg
    type(control_type_error), intent(out) :: terr

    character(len=64)  :: t_value
    character(len=128) :: str_val
    character(len=8)   :: category
    integer            :: int_val, mode_value_start, mode_buffer_length
    real(8)            :: real_val
    logical            :: lval
    logical            :: ok, proceed, mode_present
    integer            :: hh, mm, ss, yr, mo, dy, enc   ! Phase 8 ISO/encode temps
    logical            :: vok                           ! Phase 8 value-parse ok
    character(len=8)   :: numbers_got
    character(len=5)   :: numbers_problem

    action       = CTRL_UNKNOWN
    terr%present = .false.

    ! Required: "t":<string>
    call get_string_(buf, '"t"', t_value, ok)
    if (.not. ok) then
       action = CTRL_PARSE_ERR
       return
    end if

    if (trim(t_value) .eq. 'halt') then
       action = CTRL_HALT
       return
    end if

    if (trim(t_value) .eq. 'discontinuity') then
       action = CTRL_DISCONTINUITY
       return
    end if

    if (trim(t_value) .eq. 'pack') then
       action = CTRL_PACK
       return
    end if

    if (trim(t_value) .eq. 'render') then
       action = CTRL_RENDER
       return
    end if

    if (trim(t_value) .ne. 'configure') then
       action = CTRL_UNKNOWN   ! forward-compat: ignore unknown "t"
       return
    end if

    action = CTRL_CONFIGURE

    ! "mode" accepts a mode-name string ("FT8") or an integer code (8).
    call check_type_(buf, '"mode"', 'mode', 'string|int', .true., .true., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"mode"', str_val, ok)
       if (ok) then
          int_val = mode_string_to_int(trim(str_val))
          if (upper_(trim(str_val)) .eq. 'JTTY') int_val = MODE_JTTY
          if (int_val .ge. 0) then
             cfg%mode_set = .true.
             cfg%mode     = int_val
          else
             cfg%mode_invalid = .true.
          end if
       else
          call get_int_(buf, '"mode"', int_val, ok)
          if (ok .and. valid_mode_int_(int_val)) then
             cfg%mode_set = .true.
             cfg%mode     = int_val
          else if (ok) then
             cfg%mode_invalid = .true.
          else
             call find_key_(buf, '"mode"', mode_value_start,                 &
                  mode_buffer_length, mode_present)
             cfg%mode_invalid = mode_present
          end if
       end if
    end if

    call check_type_(buf, '"depth"', 'depth', 'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"depth"', int_val, ok)
       if (ok) then
          cfg%depth_set = .true.
          cfg%depth     = int_val
       end if
    end if

    call check_type_(buf, '"rxfreq"', 'rxfreq', 'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"rxfreq"', int_val, ok)
       if (ok) then
          cfg%rxfreq_set = .true.
          cfg%rxfreq     = int_val
       end if
    end if

    call check_type_(buf, '"trperiod"', 'trperiod', 'real', .false., .true., terr, proceed)
    if (proceed) then
       call get_real_(buf, '"trperiod"', real_val, ok)
       if (ok) then
          cfg%trperiod_set = .true.
          cfg%trperiod     = real_val
       end if
    end if

    ! "submode" accepts integer (0..n) or single-letter string ("A".."G").
    call check_type_(buf, '"submode"', 'submode', 'string|int', .true., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"submode"', int_val, ok)
       if (ok) then
          cfg%submode_set = .true.
          cfg%submode     = int_val
       else
          call get_string_(buf, '"submode"', str_val, ok)
          if (ok .and. len_trim(str_val) .ge. 1) then
             cfg%submode_set = .true.
             cfg%submode     = iachar(str_val(1:1)) - iachar('A')
             if (cfg%submode .lt. 0) cfg%submode = 0
          end if
       end if
    end if

    call check_type_(buf, '"mycall"', 'mycall', 'string', .true., .false., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"mycall"', str_val, ok)
       if (ok) then
          cfg%mycall_set = .true.
          cfg%mycall     = str_val(:min(len(str_val), 12))
          call station_value_(buf, '"mycall"', cfg%mycall_tx, cfg%mycall_tx_long)
       else
          call value_category_(buf, '"mycall"', category)
          cfg%mycall_unreadable = trim(category) .eq. 'string'
       end if
    end if

    call check_type_(buf, '"mygrid"', 'mygrid', 'string', .true., .false., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"mygrid"', str_val, ok)
       if (ok) then
          cfg%mygrid_set = .true.
          cfg%mygrid     = str_val(:min(len(str_val), 6))
          call station_value_(buf, '"mygrid"', cfg%mygrid_tx, cfg%mygrid_tx_long)
       else
          call value_category_(buf, '"mygrid"', category)
          cfg%mygrid_unreadable = trim(category) .eq. 'string'
       end if
    end if

    call check_type_(buf, '"ntol"', 'ntol', 'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"ntol"', int_val, ok)
       if (ok) then
          cfg%ntol_set = .true.
          cfg%ntol     = int_val
       end if
    end if

    call check_type_(buf, '"nfa"', 'nfa', 'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"nfa"', int_val, ok)
       if (ok) then
          cfg%nfa_set = .true.
          cfg%nfa     = int_val
       end if
    end if

    call check_type_(buf, '"nfb"', 'nfb', 'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"nfb"', int_val, ok)
       if (ok) then
          cfg%nfb_set = .true.
          cfg%nfb     = int_val
       end if
    end if

    call check_type_(buf, '"nutc"', 'nutc', 'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"nutc"', int_val, ok)
       if (ok) then
          cfg%nutc_set = .true.
          cfg%nutc     = int_val
       end if
    end if

    ! ===== Phase 4: operator identity + frequency-window fields (RFC v1 §2) ===
    ! his_call/his_grid/my_b_call/his_b_call: strings (like mycall/mygrid),
    ! truncated to the struct char width at assignment (silent truncation).
    call check_type_(buf, '"his_call"', 'his_call', 'string', .true., .false., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"his_call"', str_val, ok)
       if (ok) then
          cfg%his_call_set = .true.
          cfg%his_call     = str_val(:min(len(str_val), 12))
       end if
    end if

    call check_type_(buf, '"his_grid"', 'his_grid', 'string', .true., .false., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"his_grid"', str_val, ok)
       if (ok) then
          cfg%his_grid_set = .true.
          cfg%his_grid     = str_val(:min(len(str_val), 6))
       end if
    end if

    call check_type_(buf, '"my_b_call"', 'my_b_call', 'string', .true., .false., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"my_b_call"', str_val, ok)
       if (ok) then
          cfg%my_b_call_set = .true.
          cfg%my_b_call     = str_val(:min(len(str_val), 12))
       end if
    end if

    call check_type_(buf, '"his_b_call"', 'his_b_call', 'string', .true., .false., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"his_b_call"', str_val, ok)
       if (ok) then
          cfg%his_b_call_set = .true.
          cfg%his_b_call     = str_val(:min(len(str_val), 12))
       end if
    end if

    ! my_call_standard/his_call_standard: booleans (true/false). accept_bool
    ! widens check_type_ to accept the 'bool' category for these two keys only.
    call check_type_(buf, '"my_call_standard"', 'my_call_standard', 'bool',     &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"my_call_standard"', lval, ok)
       if (ok) then
          cfg%my_call_standard_set = .true.
          cfg%my_call_standard     = lval
       end if
    end if

    call check_type_(buf, '"his_call_standard"', 'his_call_standard', 'bool',    &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"his_call_standard"', lval, ok)
       if (ok) then
          cfg%his_call_standard_set = .true.
          cfg%his_call_standard     = lval
       end if
    end if

    ! Frequency-window ints (Hz).
    call check_type_(buf, '"tx_audio_offset_hz"', 'tx_audio_offset_hz', 'int',   &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"tx_audio_offset_hz"', int_val, ok)
       if (ok) then
          cfg%tx_audio_offset_hz_set = .true.
          cfg%tx_audio_offset_hz     = int_val
       end if
    end if

    call check_type_(buf, '"jt65_jt9_split_hz"', 'jt65_jt9_split_hz', 'int',     &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"jt65_jt9_split_hz"', int_val, ok)
       if (ok) then
          cfg%jt65_jt9_split_hz_set = .true.
          cfg%jt65_jt9_split_hz     = int_val
       end if
    end if

    call check_type_(buf, '"max_drift_hz"', 'max_drift_hz', 'int',               &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"max_drift_hz"', int_val, ok)
       if (ok) then
          cfg%max_drift_hz_set = .true.
          cfg%max_drift_hz     = int_val
       end if
    end if

    ! ===== Phase 5: decoder-tuning basic fields (RFC v1 §2) =================
    ! Two real keys (parsed as real(8); narrowed to real(c_float) at apply, §4).
    call check_type_(buf, '"dt_tolerance_seconds"', 'dt_tolerance_seconds',      &
         'real', .false., .true., terr, proceed)
    if (proceed) then
       call get_real_(buf, '"dt_tolerance_seconds"', real_val, ok)
       if (ok) then
          cfg%dt_tolerance_seconds_set = .true.
          cfg%dt_tolerance_seconds     = real_val
       end if
    end if

    call check_type_(buf, '"eme_delay_seconds"', 'eme_delay_seconds',            &
         'real', .false., .true., terr, proceed)
    if (proceed) then
       call get_real_(buf, '"eme_delay_seconds"', real_val, ok)
       if (ok) then
          cfg%eme_delay_seconds_set = .true.
          cfg%eme_delay_seconds     = real_val
       end if
    end if

    ! Six int keys.
    call check_type_(buf, '"kin_samples"', 'kin_samples', 'int',                 &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"kin_samples"', int_val, ok)
       if (ok) then
          cfg%kin_samples_set = .true.
          cfg%kin_samples     = int_val
       end if
    end if

    call check_type_(buf, '"nzhsym_per_period"', 'nzhsym_per_period', 'int',     &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"nzhsym_per_period"', int_val, ok)
       if (ok) then
          cfg%nzhsym_per_period_set = .true.
          cfg%nzhsym_per_period     = int_val
       end if
    end if

    call check_type_(buf, '"npts_c0_array"', 'npts_c0_array', 'int',             &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"npts_c0_array"', int_val, ok)
       if (ok .and. int_val .ge. NPTS_C0_ARRAY_MIN .and.                   &
            int_val .le. NPTS_C0_ARRAY_MAX) then
          cfg%npts_c0_array_set = .true.
          cfg%npts_c0_array     = int_val
       end if
    end if

    call check_type_(buf, '"min_width"', 'min_width', 'int',                     &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"min_width"', int_val, ok)
       if (ok) then
          cfg%min_width_set = .true.
          cfg%min_width     = int_val
       end if
    end if

    call check_type_(buf, '"min_sync"', 'min_sync', 'int',                       &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"min_sync"', int_val, ok)
       if (ok) then
          cfg%min_sync_set = .true.
          cfg%min_sync     = int_val
       end if
    end if

    call check_type_(buf, '"n_2pass"', 'n_2pass', 'int',                         &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"n_2pass"', int_val, ok)
       if (ok) then
          cfg%n_2pass_set = .true.
          cfg%n_2pass     = int_val
       end if
    end if

    ! Three bool keys (reuse the Phase 4 get_bool_ / accept_bool path).
    call check_type_(buf, '"robust_mode"', 'robust_mode', 'bool',                &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"robust_mode"', lval, ok)
       if (ok) then
          cfg%robust_mode_set = .true.
          cfg%robust_mode     = lval
       end if
    end if

    call check_type_(buf, '"nagain_flag"', 'nagain_flag', 'bool',                &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"nagain_flag"', lval, ok)
       if (ok) then
          cfg%nagain_flag_set = .true.
          cfg%nagain_flag     = lval
       end if
    end if

    call check_type_(buf, '"clear_average"', 'clear_average', 'bool',            &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"clear_average"', lval, ok)
       if (ok) then
          cfg%clear_average_set = .true.
          cfg%clear_average     = lval
       end if
    end if

    call check_type_(buf, '"decode_after_eme_delay"', 'decode_after_eme_delay', 'bool', &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"decode_after_eme_delay"', lval, ok)
       if (ok) then
          cfg%decode_after_eme_delay_set = .true.
          cfg%decode_after_eme_delay     = lval
       end if
    end if

    call check_type_(buf, '"live_audio"', 'live_audio', 'bool',                  &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"live_audio"', lval, ok)
       if (ok) then
          cfg%live_audio_set = .true.
          cfg%live_audio     = lval
       end if
    end if

    call check_type_(buf, '"short_messages"', 'short_messages', 'bool',          &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"short_messages"', lval, ok)
       if (ok) then
          cfg%short_messages_set = .true.
          cfg%short_messages     = lval
       end if
    end if

    call check_type_(buf, '"swl"', 'swl', 'bool',                                &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"swl"', lval, ok)
       if (ok) then
          cfg%swl_set = .true.
          cfg%swl     = lval
       end if
    end if

    call check_type_(buf, '"train"', 'train', 'bool',                            &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"train"', lval, ok)
       if (ok) then
          cfg%train_set = .true.
          cfg%train     = lval
       end if
    end if

    call get_numbers_(buf, '"phase_eq"', cfg%phase_eq, cfg%phase_eq_set,         &
         numbers_problem, numbers_got)
    cfg%phase_eq_malformed = numbers_problem .eq. 'text'
    cfg%phase_eq_out_of_range = numbers_problem .eq. 'range'
    if (numbers_problem .eq. 'type')                                             &
         call set_type_error_(terr, 'phase_eq', 'array of numbers', numbers_got)

    ! tx_mode: enum string ("JT9"/"JT65") or int (9/65) -> ntxmode. Mirrors the
    ! "mode" block (string|int via mode_string_to_int). A negative map (unknown
    ! name) leaves tx_mode unset, like "mode".
    call check_type_(buf, '"tx_mode"', 'tx_mode', 'string|int', .true., .true.,  &
         terr, proceed)
    if (proceed) then
       call get_string_(buf, '"tx_mode"', str_val, ok)
       if (ok) then
          int_val = mode_string_to_int(trim(str_val))
          if (int_val .ge. 0) then
             cfg%tx_mode_set = .true.
             cfg%tx_mode     = int_val
          end if
       else
          call get_int_(buf, '"tx_mode"', int_val, ok)
          if (ok) then
             cfg%tx_mode_set = .true.
             cfg%tx_mode     = int_val
          end if
       end if
    end if

    ! ===== Phase 6: FT8/FT4 specifics cluster (RFC v1 §2) ==================
    ! 19 bools + 5 ints, flat 1:1. Bools reuse the get_bool_/accept_bool path;
    ! ints use get_int_. ap_width_hz is a wire int stored RAW (decode halves it).
    call check_type_(buf, '"multithreaded_ft8"', 'multithreaded_ft8', 'bool',  &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"multithreaded_ft8"', lval, ok)
       if (ok) then
          cfg%multithreaded_ft8_set = .true.
          cfg%multithreaded_ft8     = lval
       end if
    end if

    call check_type_(buf, '"ft8_cycles"', 'ft8_cycles', 'int',                 &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"ft8_cycles"', int_val, ok)
       if (ok) then
          cfg%ft8_cycles_set = .true.
          cfg%ft8_cycles     = int_val
       end if
    end if

    call check_type_(buf, '"ft8_rxf_sensitivity"', 'ft8_rxf_sensitivity',      &
         'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"ft8_rxf_sensitivity"', int_val, ok)
       if (ok) then
          cfg%ft8_rxf_sensitivity_set = .true.
          cfg%ft8_rxf_sensitivity     = int_val
       end if
    end if

    call check_type_(buf, '"ft8_threads"', 'ft8_threads', 'int',               &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"ft8_threads"', int_val, ok)
       if (ok) then
          cfg%ft8_threads_set = .true.
          cfg%ft8_threads     = int_val
       end if
    end if

    call check_type_(buf, '"ft8_decoder_start"', 'ft8_decoder_start', 'int',   &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"ft8_decoder_start"', int_val, ok)
       if (ok) then
          cfg%ft8_decoder_start_set = .true.
          cfg%ft8_decoder_start     = int_val
       end if
    end if

    call check_type_(buf, '"ft8_low_threshold"', 'ft8_low_threshold', 'bool',  &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"ft8_low_threshold"', lval, ok)
       if (ok) then
          cfg%ft8_low_threshold_set = .true.
          cfg%ft8_low_threshold     = lval
       end if
    end if

    call check_type_(buf, '"ft8_subpass"', 'ft8_subpass', 'bool',              &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"ft8_subpass"', lval, ok)
       if (ok) then
          cfg%ft8_subpass_set = .true.
          cfg%ft8_subpass     = lval
       end if
    end if

    call check_type_(buf, '"ft8_ap_on"', 'ft8_ap_on', 'bool',                  &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"ft8_ap_on"', lval, ok)
       if (ok) then
          cfg%ft8_ap_on_set = .true.
          cfg%ft8_ap_on     = lval
       end if
    end if

    call check_type_(buf, '"ap_cq_only"', 'ap_cq_only', 'bool',                &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"ap_cq_only"', lval, ok)
       if (ok) then
          cfg%ap_cq_only_set = .true.
          cfg%ap_cq_only     = lval
       end if
    end if

    call check_type_(buf, '"ap_my_call"', 'ap_my_call', 'bool',               &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"ap_my_call"', lval, ok)
       if (ok) then
          cfg%ap_my_call_set = .true.
          cfg%ap_my_call     = lval
       end if
    end if

    call check_type_(buf, '"jt65_ap_on"', 'jt65_ap_on', 'bool',               &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"jt65_ap_on"', lval, ok)
       if (ok) then
          cfg%jt65_ap_on_set = .true.
          cfg%jt65_ap_on     = lval
       end if
    end if

    call check_type_(buf, '"ap_width_hz"', 'ap_width_hz', 'int',               &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"ap_width_hz"', int_val, ok)
       if (ok) then
          cfg%ap_width_hz_set = .true.
          cfg%ap_width_hz     = int_val
       end if
    end if

    call check_type_(buf, '"hide_ft8_duplicates"', 'hide_ft8_duplicates',      &
         'bool', .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"hide_ft8_duplicates"', lval, ok)
       if (ok) then
          cfg%hide_ft8_duplicates_set = .true.
          cfg%hide_ft8_duplicates     = lval
       end if
    end if

    call check_type_(buf, '"common_ft8b"', 'common_ft8b', 'bool',              &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"common_ft8b"', lval, ok)
       if (ok) then
          cfg%common_ft8b_set = .true.
          cfg%common_ft8b     = lval
       end if
    end if

    call check_type_(buf, '"enable_dxc_search"', 'enable_dxc_search', 'bool',  &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"enable_dxc_search"', lval, ok)
       if (ok) then
          cfg%enable_dxc_search_set = .true.
          cfg%enable_dxc_search     = lval
       end if
    end if

    call check_type_(buf, '"wide_dxc_search"', 'wide_dxc_search', 'bool',      &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"wide_dxc_search"', lval, ok)
       if (ok) then
          cfg%wide_dxc_search_set = .true.
          cfg%wide_dxc_search     = lval
       end if
    end if

    call check_type_(buf, '"superfox_mode"', 'superfox_mode', 'bool',          &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"superfox_mode"', lval, ok)
       if (ok) then
          cfg%superfox_mode_set = .true.
          cfg%superfox_mode     = lval
       end if
    end if

    call check_type_(buf, '"even_sequence"', 'even_sequence', 'bool',          &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"even_sequence"', lval, ok)
       if (ok) then
          cfg%even_sequence_set = .true.
          cfg%even_sequence     = lval
       end if
    end if

    call check_type_(buf, '"hound_mode"', 'hound_mode', 'bool',                &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"hound_mode"', lval, ok)
       if (ok) then
          cfg%hound_mode_set = .true.
          cfg%hound_mode     = lval
       end if
    end if

    call check_type_(buf, '"multi_instance"', 'multi_instance', 'bool',        &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"multi_instance"', lval, ok)
       if (ok) then
          cfg%multi_instance_set = .true.
          cfg%multi_instance     = lval
       end if
    end if

    call check_type_(buf, '"skip_tx1"', 'skip_tx1', 'bool',                    &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"skip_tx1"', lval, ok)
       if (ok) then
          cfg%skip_tx1_set = .true.
          cfg%skip_tx1     = lval
       end if
    end if

    call check_type_(buf, '"nagain_filter"', 'nagain_filter', 'bool',          &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"nagain_filter"', lval, ok)
       if (ok) then
          cfg%nagain_filter_set = .true.
          cfg%nagain_filter     = lval
       end if
    end if

    call check_type_(buf, '"stop_hint"', 'stop_hint', 'bool',                  &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"stop_hint"', lval, ok)
       if (ok) then
          cfg%stop_hint_set = .true.
          cfg%stop_hint     = lval
       end if
    end if

    call check_type_(buf, '"hint_mode"', 'hint_mode', 'bool',                  &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"hint_mode"', lval, ok)
       if (ok) then
          cfg%hint_mode_set = .true.
          cfg%hint_mode     = lval
       end if
    end if

    ! ===== Phase 7: diagnostic / sequencing fields (RFC v1 §2) =============
    ! 2 bools + 4 ints, flat 1:1. Bools reuse the get_bool_/accept_bool path;
    ! ints use get_int_. The configure_fields Phase 7 comment lists the keys
    ! left off the wire.
    call check_type_(buf, '"last_tx_seconds_ago"', 'last_tx_seconds_ago',      &
         'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"last_tx_seconds_ago"', int_val, ok)
       if (ok) then
          cfg%last_tx_seconds_ago_set = .true.
          cfg%last_tx_seconds_ago     = int_val
       end if
    end if

    call check_type_(buf, '"currently_txing"', 'currently_txing', 'bool',      &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"currently_txing"', lval, ok)
       if (ok) then
          cfg%currently_txing_set = .true.
          cfg%currently_txing     = lval
       end if
    end if

    call check_type_(buf, '"mode_changed"', 'mode_changed', 'bool',            &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"mode_changed"', lval, ok)
       if (ok) then
          cfg%mode_changed_set = .true.
          cfg%mode_changed     = lval
       end if
    end if

    call check_type_(buf, '"qso_progress_state"', 'qso_progress_state',        &
         'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"qso_progress_state"', int_val, ok)
       if (ok) then
          cfg%qso_progress_state_set = .true.
          cfg%qso_progress_state     = int_val
       end if
    end if

    call check_type_(buf, '"sec_band_changed"', 'sec_band_changed', 'int',     &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"sec_band_changed"', int_val, ok)
       if (ok) then
          cfg%sec_band_changed_set = .true.
          cfg%sec_band_changed     = int_val
       end if
    end if

    call check_type_(buf, '"delay_units"', 'delay_units', 'int',               &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"delay_units"', int_val, ok)
       if (ok) then
          cfg%delay_units_set = .true.
          cfg%delay_units     = int_val
       end if
    end if

    ! ===== Phase 8: string & scaled / derived fields (RFC v1 §2/§6) =========
    ! These derive the LEGACY encoding at parse (see the configure_fields Phase 8
    ! comment): utc/date are ISO strings tokenized by parse_iso_time_/
    ! parse_iso_date_; n_trials is encoded to nranera by ntrials_to_nranera_;
    ! candthin_threshold/dt_center_seconds are wire reals scaled /100 at apply.
    !
    ! utc: ISO "HH:MM:SS" -> utc_nutc = hh*10000+mm*100+ss (§6.3, always full
    ! HHMMSS). A malformed time string leaves utc unset (value-domain silent skip).
    call check_type_(buf, '"utc"', 'utc', 'string', .true., .false., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"utc"', str_val, ok)
       if (ok) then
          call parse_iso_time_(trim(str_val), hh, mm, ss, vok)
          if (vok) then
             cfg%utc_set  = .true.
             cfg%utc_nutc = hh * 10000 + mm * 100 + ss
          end if
       end if
    end if

    ! date: ISO "YYYY-MM-DD" -> date_yymmdd = (yr-2000)*10000+mo*100+day (§6.4).
    ! The 2000+offset encoding caps at 2099 -> year >= 2100 is a configure_type_error
    ! (RFC §6.4 "receiver emits error if year >= 2100"). A malformed date string
    ! leaves date unset (value-domain silent skip). Only yymmdd is populated; the
    ! datetime leg is intentionally OMITted (§6.1 — C/GUI filename field).
    call check_type_(buf, '"date"', 'date', 'string', .true., .false., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"date"', str_val, ok)
       if (ok) then
          call parse_iso_date_(trim(str_val), yr, mo, dy, vok)
          if (vok) then
             if (yr .ge. 2100) then
                call set_type_error_(terr, 'date', 'year<2100', 'string')
             else
                cfg%date_set    = .true.
                cfg%date_yymmdd = (yr - 2000) * 10000 + mo * 100 + dy
             end if
          end if
       end if
    end if

    ! n_trials: plain int on the wire -> encoded nranera (§6.6). The value MUST be
    ! one of {10^N, 3*10^N}; anything else is a configure_type_error (§6.6 — a
    ! non-conforming value would integer-divide into a silently-wrong ntrials at
    ! decoder.f90:100-102). The check is a VALUE check (the JSON type is int), so it
    ! runs AFTER the int parse and reuses the same configure_type_error envelope.
    call check_type_(buf, '"n_trials"', 'n_trials', 'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"n_trials"', int_val, ok)
       if (ok) then
          call ntrials_to_nranera_(int_val, enc, vok)
          if (vok) then
             cfg%n_trials_set     = .true.
             cfg%n_trials_nranera = enc
          else
             call set_type_error_(terr, 'n_trials', 'int 10^N|3*10^N', 'number')
          end if
       end if
    end if

    ! candthin_threshold / dt_center_seconds: wire reals, scaled /100 to int at
    ! apply (§4/§6.5 — declare real(8), nint(wire*100)). Parsed like the Phase 5
    ! reals (check_type_ 'real' -> get_real_).
    call check_type_(buf, '"candthin_threshold"', 'candthin_threshold',        &
         'real', .false., .true., terr, proceed)
    if (proceed) then
       call get_real_(buf, '"candthin_threshold"', real_val, ok)
       if (ok) then
          cfg%candthin_threshold_set = .true.
          cfg%candthin_threshold     = real_val
       end if
    end if

    call check_type_(buf, '"dt_center_seconds"', 'dt_center_seconds',          &
         'real', .false., .true., terr, proceed)
    if (proceed) then
       call get_real_(buf, '"dt_center_seconds"', real_val, ok)
       if (ok) then
          cfg%dt_center_seconds_set = .true.
          cfg%dt_center_seconds     = real_val
       end if
    end if

    ! ===== Phase 9 (RFC v1 §2.4/§2.5/§6.1/§6.2): bit-packed unpacked keys ======
    ! Each unpacked key parses into its own configure_fields member; pack_phase9_
    ! (called after the version block) folds them into ndepth_packed /
    ! nexp_decode_packed and enforces the two atomic-decline errors. depth_level/
    ! q65_maxiters_level/noise_blanker_level are ints; use_averaging/deep_ap_search/
    ! q65_auto_clear_average/single_decode/vhf_features are bools (get_bool_ /
    ! accept_bool path); contest_type is an enum string resolved to the collapsed
    ! ncontest int by contest_string_to_int_ (an unknown string leaves it unset — a
    ! value-domain silent skip, exactly like an unknown mode name).
    call check_type_(buf, '"depth_level"', 'depth_level', 'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"depth_level"', int_val, ok)
       if (ok) then
          cfg%depth_level_set = .true.
          cfg%depth_level     = int_val
       end if
    end if

    call check_type_(buf, '"q65_maxiters_level"', 'q65_maxiters_level', 'int',   &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"q65_maxiters_level"', int_val, ok)
       if (ok) then
          cfg%q65_maxiters_level_set = .true.
          cfg%q65_maxiters_level     = int_val
       end if
    end if

    call check_type_(buf, '"use_averaging"', 'use_averaging', 'bool',            &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"use_averaging"', lval, ok)
       if (ok) then
          cfg%use_averaging_set = .true.
          cfg%use_averaging     = lval
       end if
    end if

    call check_type_(buf, '"deep_ap_search"', 'deep_ap_search', 'bool',          &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"deep_ap_search"', lval, ok)
       if (ok) then
          cfg%deep_ap_search_set = .true.
          cfg%deep_ap_search     = lval
       end if
    end if

    call check_type_(buf, '"q65_auto_clear_average"', 'q65_auto_clear_average',  &
         'bool', .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"q65_auto_clear_average"', lval, ok)
       if (ok) then
          cfg%q65_auto_clear_average_set = .true.
          cfg%q65_auto_clear_average     = lval
       end if
    end if

    ! contest_type: enum string -> collapsed ncontest int (0-7). Unknown -> unset.
    call check_type_(buf, '"contest_type"', 'contest_type', 'string',            &
         .true., .false., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"contest_type"', str_val, ok)
       if (ok) then
          int_val = contest_string_to_int_(trim(str_val))
          if (int_val .ge. 0) then
             cfg%contest_type_set = .true.
             cfg%contest_type     = int_val
          end if
       end if
    end if

    call check_type_(buf, '"single_decode"', 'single_decode', 'bool',            &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"single_decode"', lval, ok)
       if (ok) then
          cfg%single_decode_set = .true.
          cfg%single_decode     = lval
       end if
    end if

    call check_type_(buf, '"vhf_features"', 'vhf_features', 'bool',              &
         .false., .false., terr, proceed, accept_bool=.true.)
    if (proceed) then
       call get_bool_(buf, '"vhf_features"', lval, ok)
       if (ok) then
          cfg%vhf_features_set = .true.
          cfg%vhf_features     = lval
       end if
    end if

    call check_type_(buf, '"noise_blanker_level"', 'noise_blanker_level', 'int', &
         .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"noise_blanker_level"', int_val, ok)
       if (ok) then
          cfg%noise_blanker_level_set = .true.
          cfg%noise_blanker_level     = int_val
       end if
    end if

    ! Body schema version (RFC v1 §3). Absent => stays at the default 1.
    call check_type_(buf, '"version"', 'version', 'int', .false., .true., terr, proceed)
    if (proceed) then
       call get_int_(buf, '"version"', int_val, ok)
       if (ok) then
          cfg%version_set = .true.
          cfg%version     = int_val
       end if
    end if

    ! ===== Phase 9 post-pass: pack ndepth / nexp_decode + atomic-decline checks =
    ! Runs after ALL keys are parsed (the depth-vs-unpacked conflict is cross-key).
    call pack_phase9_(cfg, terr)

  end subroutine parse_control_frame

  ! Map a mode name string to the jt9 integer mode code. Returns -1 for
  ! an unknown name. Mirrors lib/jt9.f90's case-table accepted modes.
  function mode_string_to_int(name) result(mode_int)
    character(len=*), intent(in) :: name
    integer                       :: mode_int
    character(len=16)             :: u

    u = upper_(name)
    mode_int = -1
    select case (trim(u))
    case ('FT8');     mode_int = 8
    case ('FT4');     mode_int = 5
    case ('JT9');     mode_int = 9
    case ('JT65');    mode_int = 65
    case ('JT65+JT9'); mode_int = 74
    case ('JT4');     mode_int = 4
    case ('FST4');    mode_int = 240
    case ('FST4W');   mode_int = 241
    case ('Q65');     mode_int = 66
    case ('MSK144');  mode_int = 144
    end select
  end function mode_string_to_int

  pure function valid_mode_int_(mode_int) result(valid)
    integer, intent(in) :: mode_int
    logical             :: valid

    select case (mode_int)
    case (4, 5, 8, 9, 65, 66, 74, 144, 240, 241, 242)
       valid = .true.
    case default
       valid = .false.
    end select
  end function valid_mode_int_

  ! ===== internals ======================================================

  ! Find a string value in a flat JSON: looks for `key:"value"`, returns
  ! `value` unescaped. ok=.false. on miss or syntax error.
  subroutine get_string_(buf, key, val, ok)
    character(len=*), intent(in)  :: buf, key
    character(len=*), intent(out) :: val
    logical,          intent(out) :: ok
    integer :: vstart, blen
    logical :: found
    character(len=:), allocatable :: decoded

    val = ' '
    ok  = .false.
    call find_key_(buf, key, vstart, blen, found)
    if (.not. found) return
    if (vstart .gt. blen) return
    if (buf(vstart:vstart) .ne. '"') return
    call json_string_(buf(vstart + 1:blen), decoded, ok)
    if (ok) val = decoded
  end subroutine get_string_

  ! The JSON string whose body starts at s(1:1), up to its closing quote,
  ! decoded to UTF-8. ok=.false. on a malformed escape, a lone surrogate or
  ! no closing quote. A string without a backslash is returned as it stands.
  subroutine json_string_(s, val, ok)
    character(len=*),              intent(in)  :: s
    character(len=:), allocatable, intent(out) :: val
    logical,                       intent(out) :: ok
    character(len=:), allocatable :: out
    integer :: i, n, code, low
    ! UTF-16 surrogates: a high one (U+D800-DBFF) must be followed by a low one.
    integer, parameter :: HIGH_FIRST = int(z'D800'), HIGH_LAST = int(z'DBFF')
    integer, parameter :: LOW_FIRST  = int(z'DC00'), LOW_LAST  = int(z'DFFF')

    ok = .false.
    val = ''
    ! No escape decodes to more bytes than it spells.
    allocate(character(len=len(s)) :: out)
    n = 0
    i = 1
    do while (i .le. len(s))
       select case (s(i:i))
       case ('"')
          val = out(1:n)
          ok  = .true.
          return
       case ('\')
          if (i .eq. len(s)) return
          i = i + 1
          select case (s(i:i))
          case ('"', '\', '/')
             n = n + 1; out(n:n) = s(i:i)
          case ('b')
             n = n + 1; out(n:n) = achar(8)
          case ('f')
             n = n + 1; out(n:n) = achar(12)
          case ('n')
             n = n + 1; out(n:n) = achar(10)
          case ('r')
             n = n + 1; out(n:n) = achar(13)
          case ('t')
             n = n + 1; out(n:n) = achar(9)
          case ('u')
             if (.not. hex4_(s, i + 1, code)) return
             i = i + 4
             if (code .ge. LOW_FIRST .and. code .le. LOW_LAST) return
             if (code .ge. HIGH_FIRST .and. code .le. HIGH_LAST) then
                if (i + 2 .gt. len(s)) return
                if (s(i + 1:i + 2) .ne. '\u') return
                if (.not. hex4_(s, i + 3, low)) return
                if (low .lt. LOW_FIRST .or. low .gt. LOW_LAST) return
                code = 65536 + (code - HIGH_FIRST) * 1024 + (low - LOW_FIRST)
                i = i + 6
             end if
             call put_utf8_(code, out, n)
          case default
             return
          end select
       case default
          n = n + 1; out(n:n) = s(i:i)
       end select
       i = i + 1
    end do
  end subroutine json_string_

  ! The four hex digits s(at:at+3) as code; .false. unless all four are.
  logical function hex4_(s, at, code)
    character(len=*), intent(in)  :: s
    integer,          intent(in)  :: at
    integer,          intent(out) :: code
    integer :: k, d

    hex4_ = .false.
    code  = 0
    if (at + 3 .gt. len(s)) return
    do k = at, at + 3
       d = index('0123456789abcdef', s(k:k))
       if (d .eq. 0) d = index('0123456789ABCDEF', s(k:k))
       if (d .eq. 0) return
       code = 16 * code + d - 1
    end do
    hex4_ = .true.
  end function hex4_

  ! Append code point code to out(1:n) as UTF-8.
  subroutine put_utf8_(code, out, n)
    integer,          intent(in)    :: code
    character(len=*), intent(inout) :: out
    integer,          intent(inout) :: n

    if (code .lt. 128) then
       out(n + 1:n + 1) = char(code)
       n = n + 1
    else if (code .lt. 2048) then
       out(n + 1:n + 1) = char(192 + code / 64)
       out(n + 2:n + 2) = char(128 + iand(code, 63))
       n = n + 2
    else if (code .lt. 65536) then
       out(n + 1:n + 1) = char(224 + code / 4096)
       out(n + 2:n + 2) = char(128 + iand(code / 64, 63))
       out(n + 3:n + 3) = char(128 + iand(code, 63))
       n = n + 3
    else
       out(n + 1:n + 1) = char(240 + code / 262144)
       out(n + 2:n + 2) = char(128 + iand(code / 4096, 63))
       out(n + 3:n + 3) = char(128 + iand(code / 64, 63))
       out(n + 4:n + 4) = char(128 + iand(code, 63))
       n = n + 4
    end if
  end subroutine put_utf8_

  ! Find an integer value in a flat JSON: looks for `key:NNN`, value is
  ! decimal integer with optional sign. ok=.false. on miss or non-int.
  subroutine get_int_(buf, key, val, ok)
    character(len=*), intent(in)  :: buf, key
    integer,          intent(out) :: val
    logical,          intent(out) :: ok
    integer :: vstart, vend, blen, ios
    character :: ch
    logical :: found

    val = 0
    ok  = .false.
    call find_key_(buf, key, vstart, blen, found)
    if (.not. found) return
    if (vstart .gt. blen) return
    if (buf(vstart:vstart) .eq. '"') return  ! string, not int
    vend = vstart
    do while (vend .le. blen)
       ch = buf(vend:vend)
       if (ch .eq. ',' .or. ch .eq. '}' .or. ch .eq. ' ') exit
       vend = vend + 1
    end do
    if (vend .le. vstart) return
    read(buf(vstart : vend - 1), *, iostat=ios) val
    if (ios .eq. 0) ok = .true.
  end subroutine get_int_

  ! Find a real value (e.g., trperiod=15 or 7.5).
  subroutine get_real_(buf, key, val, ok)
    character(len=*), intent(in)  :: buf, key
    real(8),          intent(out) :: val
    logical,          intent(out) :: ok
    integer :: vstart, vend, blen, ios
    character :: ch
    logical :: found

    val = 0.d0
    ok  = .false.
    call find_key_(buf, key, vstart, blen, found)
    if (.not. found) return
    if (vstart .gt. blen) return
    if (buf(vstart:vstart) .eq. '"') return
    vend = vstart
    do while (vend .le. blen)
       ch = buf(vend:vend)
       if (ch .eq. ',' .or. ch .eq. '}' .or. ch .eq. ' ') exit
       vend = vend + 1
    end do
    if (vend .le. vstart) return
    read(buf(vstart : vend - 1), *, iostat=ios) val
    if (ios .eq. 0) ok = .true.
  end subroutine get_real_

  ! Find a boolean value in a flat JSON: looks for `key:true` or `key:false`,
  ! keying off the value's leading char ('t'/'f'). ok=.false. on miss or a
  ! non-boolean value. Value EXTRACTION uses the same unanchored index() as
  ! get_int_/get_string_/get_real_ (wire-parity — it only ever leaves a key
  ! unset, never misapplies; TYPE validation anchors via check_type_/find_key_).
  subroutine get_bool_(buf, key, val, ok)
    character(len=*), intent(in)  :: buf, key
    logical,          intent(out) :: val
    logical,          intent(out) :: ok
    integer   :: vstart, blen
    character :: ch
    logical   :: found

    val = .false.
    ok  = .false.
    call find_key_(buf, key, vstart, blen, found)
    if (.not. found) return
    if (vstart .gt. blen) return
    ch = buf(vstart:vstart)
    if (ch .eq. 't') then
       val = .true.;  ok = .true.
    else if (ch .eq. 'f') then
       val = .false.; ok = .true.
    end if
  end subroutine get_bool_

  ! The array value of key as exactly size(vals) finite numbers in JSON's
  ! grammar (set), or the value's first problem: 'text' where it is not JSON's
  ! grammar; 'type' where it, or one of its elements, is not a number, with got
  ! its category by its first character (value_category_'s, 'array' or 'object');
  ! 'range' for a well-formed array of another length or with a number too
  ! large to hold. problem is blank when the key is absent.
  subroutine get_numbers_(buf, key, vals, set, problem, got)
    use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
    character(len=*), intent(in)  :: buf, key
    real(8),          intent(out) :: vals(:)
    logical,          intent(out) :: set
    character(len=*), intent(out) :: problem, got
    real(8) :: v(size(vals)), x
    integer :: vstart, blen, p, n, width, ios
    logical :: found, finite, empty

    vals = 0.d0
    set = .false.
    problem = ' '
    call value_category_(buf, key, got)
    if (trim(got) .eq. 'absent') return
    call find_key_(buf, key, vstart, blen, found)
    problem = 'type'
    if (buf(vstart:vstart) .eq. '{') got = 'object'
    if (buf(vstart:vstart) .ne. '[') return
    n = 0
    finite = .true.
    p = after_space_(vstart + 1)
    empty = p .le. blen
    if (empty) empty = buf(p:p) .eq. ']'
    do while (.not. empty)
       problem = 'text'
       if (p .gt. blen) return
       problem = 'type'
       select case (buf(p:p))
       case ('"');      got = 'string'; return
       case ('t', 'f'); got = 'bool';   return
       case ('n');      got = 'null';   return
       case ('[');      got = 'array';  return
       case ('{');      got = 'object'; return
       end select
       problem = 'text'
       width = json_number_width_(buf(p:blen))
       if (width .eq. 0) return
       n = n + 1
       read(buf(p:p + width - 1), *, iostat=ios) x
       if (ios .ne. 0) then
          finite = .false.
       else if (.not. ieee_is_finite(x)) then
          finite = .false.
       else if (n .le. size(v)) then
          v(n) = x
       end if
       p = after_space_(p + width)
       if (p .gt. blen) return
       if (buf(p:p) .eq. ']') exit
       if (buf(p:p) .ne. ',') return
       p = after_space_(p + 1)
    end do
    problem = 'range'
    if (.not. finite .or. n .ne. size(v)) return
    vals = v
    set = .true.
    problem = ' '

  contains

    ! The first position from q on that is not JSON white space.
    integer function after_space_(q)
      integer, intent(in) :: q
      after_space_ = q
      do while (after_space_ .le. blen)
         if (index(' ' // achar(9) // achar(10) // achar(13), buf(after_space_:after_space_)) &
              .eq. 0) exit
         after_space_ = after_space_ + 1
      end do
    end function after_space_

  end subroutine get_numbers_

  ! The length of the JSON number, -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?,
  ! at the start of s; 0 when s does not start with one.
  integer function json_number_width_(s)
    character(len=*), intent(in) :: s
    integer :: i, first

    json_number_width_ = 0
    i = 1
    if (i .le. len(s)) then
       if (s(i:i) .eq. '-') i = i + 1
    end if
    first = i
    if (.not. digits_()) return
    if (s(first:first) .eq. '0' .and. i - first .gt. 1) return
    if (i .le. len(s)) then
       if (s(i:i) .eq. '.') then
          i = i + 1
          if (.not. digits_()) return
       end if
    end if
    if (i .le. len(s)) then
       if (s(i:i) .eq. 'e' .or. s(i:i) .eq. 'E') then
          i = i + 1
          if (i .le. len(s)) then
             if (s(i:i) .eq. '+' .or. s(i:i) .eq. '-') i = i + 1
          end if
          if (.not. digits_()) return
       end if
    end if
    json_number_width_ = i - 1

  contains

    ! Skip the digits from i; .false. when there are none.
    logical function digits_()
      integer :: start
      start = i
      do while (i .le. len(s))
         if (index('0123456789', s(i:i)) .eq. 0) exit
         i = i + 1
      end do
      digits_ = i .gt. start
    end function digits_

  end function json_number_width_

  ! ===== Phase 8 derivations (ISO parsers + nranera encoder) ===============

  ! Read a NON-EMPTY all-digit field into an integer. ok=.false. if empty or any
  ! char is a non-digit. A strict digit scan (not a bare list-directed read) is
  ! required so a malformed ISO field like "34" missing from "13:34" or a stray
  ! "30:99" tail is rejected rather than silently read as a partial number.
  subroutine read_int_field_(s, val, ok)
    character(len=*), intent(in)  :: s
    integer,          intent(out) :: val
    logical,          intent(out) :: ok
    integer :: i, c, ios
    val = 0; ok = .false.
    if (len(s) .lt. 1) return
    do i = 1, len(s)
       c = iachar(s(i:i))
       if (c .lt. iachar('0') .or. c .gt. iachar('9')) return
    end do
    read(s, *, iostat=ios) val
    if (ios .eq. 0) ok = .true.
  end subroutine read_int_field_

  ! Parse ISO 8601 time "HH:MM:SS" -> three ints (§6.3). ok=.false. on a malformed
  ! string (not exactly two ':' separators with a non-empty all-digit field
  ! between/around each). Ranges are NOT enforced (the caller packs HHMMSS; an
  ! out-of-range component is a value-domain matter, not a parse error).
  subroutine parse_iso_time_(s, hh, mm, ss, ok)
    character(len=*), intent(in)  :: s
    integer,          intent(out) :: hh, mm, ss
    logical,          intent(out) :: ok
    integer :: c1, c2, n
    hh = 0; mm = 0; ss = 0; ok = .false.
    n = len_trim(s)
    c1 = index(s(1:n), ':')
    if (c1 .le. 1) return                  ! no 1st ':' or empty HH
    c2 = index(s(c1+1:n), ':')
    if (c2 .le. 1) return                  ! no 2nd ':' or empty MM
    c2 = c1 + c2                           ! absolute index of the 2nd ':'
    if (c2 .ge. n) return                  ! nothing after the 2nd ':'
    call read_int_field_(s(1:c1-1),    hh, ok); if (.not. ok) return
    call read_int_field_(s(c1+1:c2-1), mm, ok); if (.not. ok) return
    call read_int_field_(s(c2+1:n),    ss, ok)
  end subroutine parse_iso_time_

  ! Parse ISO 8601 date "YYYY-MM-DD" -> three ints (§6.4). ok=.false. on a
  ! malformed string. The caller enforces the year<2100 cap.
  subroutine parse_iso_date_(s, yr, mo, dy, ok)
    character(len=*), intent(in)  :: s
    integer,          intent(out) :: yr, mo, dy
    logical,          intent(out) :: ok
    integer :: d1, d2, n
    yr = 0; mo = 0; dy = 0; ok = .false.
    n = len_trim(s)
    d1 = index(s(1:n), '-')
    if (d1 .le. 1) return                  ! no 1st '-' or empty YYYY
    d2 = index(s(d1+1:n), '-')
    if (d2 .le. 1) return                  ! no 2nd '-' or empty MM
    d2 = d1 + d2                           ! absolute index of the 2nd '-'
    if (d2 .ge. n) return                  ! nothing after the 2nd '-'
    call read_int_field_(s(1:d1-1),    yr, ok); if (.not. ok) return
    call read_int_field_(s(d1+1:d2-1), mo, ok); if (.not. ok) return
    call read_int_field_(s(d2+1:n),    dy, ok)
  end subroutine parse_iso_date_

  ! Encode a desired trial count n_trials into the legacy nranera index (§6.6):
  !   nranera even -> ntrials = 10**(nranera/2)    (decoder.f90:100)
  !   nranera odd  -> ntrials = 3*10**(nranera/2)  (decoder.f90:101)
  ! so n_trials must be 10^k or 3*10^k. ok=.false. for any other value (the caller
  ! then emits configure_type_error). Strip trailing factors of 10; the residual
  ! must be 1 (pure power of 10 -> nranera=2k) or 3 (3x -> nranera=2k+1). This is
  ! functionally equivalent to the RFC §6.6 reference algorithm (a log10-based form;
  ! same {10^N,3*10^N} acceptance set and same mapping), which likewise returns
  ! nranera=0 for n_trials=1; note
  ! decoder.f90:102 then special-cases nranera=0 -> ntrials=0 (a legacy-encoding
  ! quirk a producer hits only by sending the degenerate n_trials=1 — documented,
  ! not "fixed", per the wire-parity scope guardrail). nranera stays <= 18 for any
  ! n_trials that fits int32 (10^9), so the decode-site 10**(nranera/2) cannot
  ! overflow. A configure with nranera above 12 (10^6 trials), which the
  ! engine refuses for JT65, is declined where JT65 runs.
  subroutine ntrials_to_nranera_(n_trials, nranera, ok)
    integer, intent(in)  :: n_trials
    integer, intent(out) :: nranera
    logical, intent(out) :: ok
    integer :: v, k
    nranera = 0; ok = .false.
    if (n_trials .le. 0) return
    v = n_trials
    k = 0
    do while (mod(v, 10) .eq. 0)
       v = v / 10
       k = k + 1
    end do
    if (v .eq. 1) then
       nranera = 2 * k;     ok = .true.
    else if (v .eq. 3) then
       nranera = 2 * k + 1; ok = .true.
    end if
  end subroutine ntrials_to_nranera_

  ! Phase 9 post-pass: fold the unpacked ndepth/nexp_decode keys into the two
  ! derived packed ints (cfg%ndepth_packed / cfg%nexp_decode_packed) and emit the
  ! two RFC §6.1/§4.1 atomic-decline errors. Called once after all keys are parsed
  ! (the legacy-depth-vs-unpacked conflict is inherently cross-key). Pack rule
  ! (RFC §6.1/§6.2):
  !   ndepth      = depth_level(bits0-2) | use_avg<<4 | deep_ap<<5 | autoclr<<7
  !   nexp_decode = ncontest(bits0-2) | single<<5 | vhf<<6 | q65_pileup<<7 | (nb+3)<<8
  ! depth_level is masked to 3 bits and q65_maxiters to 2, the NB byte clamped to
  ! [0,255], so an out-of-range producer value cannot corrupt a sibling bit (none
  ! of these reaches a raw array index, so no decode crash). The
  ! legacy `depth` full-int is applied verbatim by streaming_apply when NO
  ! unpacked ndepth key is set; co-present with any unpacked key it is a conflict
  ! (declined here, reusing the configure_type_error envelope for a VALUE-domain
  ! error like Phase 8's n_trials/date checks).
  subroutine pack_phase9_(cfg, terr)
    type(configure_fields),   intent(inout) :: cfg
    type(control_type_error), intent(inout) :: terr
    logical :: any_ndepth, any_nexp
    integer :: dl, nb

    any_ndepth = cfg%depth_level_set .or. cfg%q65_maxiters_level_set .or.        &
         cfg%use_averaging_set .or. cfg%deep_ap_search_set .or.                  &
         cfg%q65_auto_clear_average_set
    any_nexp = cfg%contest_type_set .or. cfg%single_decode_set .or.              &
         cfg%vhf_features_set .or. cfg%noise_blanker_level_set

    ! Conflict: legacy depth (full int) AND any unpacked ndepth key -> decline.
    if (cfg%depth_set .and. any_ndepth) then
       call set_type_error_(terr, 'ndepth', 'depth|unpacked', 'conflict')
       return
    end if

    ! Consistency: depth_level (bits 0-2) and q65_maxiters_level (bits 0-1) overlap
    ! on bits 0-1 (RFC §6.1). If both are set they MUST agree; an inconsistent pair
    ! is a configure_type_error.
    if (cfg%depth_level_set .and. cfg%q65_maxiters_level_set) then
       if (iand(cfg%depth_level, 3) .ne. iand(cfg%q65_maxiters_level, 3)) then
          call set_type_error_(terr, 'q65_maxiters_level', 'match depth_lvl', 'conflict')
          return
       end if
    end if

    if (any_ndepth) then
       ! Effective depth_level (bits 0-2): depth_level wins; else derive from
       ! q65_maxiters_level (bits 0-1); else 0.
       if (cfg%depth_level_set) then
          dl = iand(cfg%depth_level, 7)
       else if (cfg%q65_maxiters_level_set) then
          dl = iand(cfg%q65_maxiters_level, 3)
       else
          dl = 0
       end if
       cfg%ndepth_packed = dl
       if (cfg%use_averaging_set          .and. cfg%use_averaging)              &
            cfg%ndepth_packed = ior(cfg%ndepth_packed, 16)
       if (cfg%deep_ap_search_set         .and. cfg%deep_ap_search)            &
            cfg%ndepth_packed = ior(cfg%ndepth_packed, 32)
       if (cfg%q65_auto_clear_average_set .and. cfg%q65_auto_clear_average)    &
            cfg%ndepth_packed = ior(cfg%ndepth_packed, 128)
       cfg%ndepth_packed_set = .true.
    end if

    if (any_nexp) then
       cfg%nexp_decode_packed = iand(cfg%contest_type, 7)   ! bits 0-2 (collapsed)
       if (cfg%single_decode_set .and. cfg%single_decode)                      &
            cfg%nexp_decode_packed = ior(cfg%nexp_decode_packed, 32)
       if (cfg%vhf_features_set  .and. cfg%vhf_features)                       &
            cfg%nexp_decode_packed = ior(cfg%nexp_decode_packed, 64)
       if (cfg%noise_blanker_level_set) then
          nb = cfg%noise_blanker_level + 3        ! -3 offset (RFC §6.2; 3 = off)
          if (nb .lt. 0)   nb = 0
          if (nb .gt. 255) nb = 255
          cfg%nexp_decode_packed = cfg%nexp_decode_packed + nb * 256
       end if
       cfg%nexp_decode_packed_set = .true.
    end if
  end subroutine pack_phase9_

  ! Map a contest_type wire string to the legacy ncontest int (nexp_decode bits
  ! 0-2), applying the GUI's SpecOp 5/8/9 -> 1 collapse. The
  ! SpecialOperatingActivity enum (Configuration.hpp:318, compiled only into the
  ! widgets/GUI build so the headless core does not link it -> hardcoded here; this
  ! comment guards a future upstream enum reorder) is:
  !   NONE=0 NA_VHF=1 EU_VHF=2 FIELD_DAY=3 RTTY=4 WW_DIGI=5 FOX=6 HOUND=7
  !   ARRL_DIGI=8 Q65_PILEUP=9 ; mainwindow.cpp:5469-5475 collapses 5,8,9 -> 1.
  ! Returns -1 for an unknown string (caller leaves contest_type unset — a
  ! value-domain silent skip, like an unknown mode name). Wire strings are the
  ! lowercase snake_case names of RFC §6.2.
  pure function contest_string_to_int_(s) result(n)
    character(len=*), intent(in) :: s
    integer :: n
    select case (trim(s))
    case ('none');       n = 0
    case ('na_vhf');     n = 1
    case ('eu_vhf');     n = 2
    case ('field_day');  n = 3
    case ('rtty');       n = 4
    case ('ww_digi');    n = 1   ! SpecOp 5 -> collapsed to 1
    case ('fox');        n = 6
    case ('hound');      n = 7
    case ('arrl_digi');  n = 1   ! SpecOp 8 -> collapsed to 1
    case ('q65_pileup'); n = 1   ! SpecOp 9 -> collapsed to 1
    case default;        n = -1
    end select
  end function contest_string_to_int_

  pure function upper_(s) result(u)
    character(len=*), intent(in) :: s
    character(len=len(s))        :: u
    integer :: i, c
    do i = 1, len(s)
       c = iachar(s(i:i))
       if (c .ge. iachar('a') .and. c .le. iachar('z')) then
          u(i:i) = achar(c - 32)
       else
          u(i:i) = s(i:i)
       end if
    end do
  end function upper_

  ! Locate `key` (a quoted token such as '"depth"') at a STRUCTURAL key position
  ! in a flat JSON buffer: preceded (after optional spaces) by '{' or ',' AND
  ! followed (after optional spaces) by ':'. Returns found=.true. and vstart =
  ! the index of the first value char (after the ':' and any spaces).
  !
  ! Anchoring on the structural form is what stops a quoted string VALUE that
  ! happens to equal a key token (e.g. {"mycall":"mygrid"}) from being mistaken
  ! for that key. The unanchored index() in get_int_/get_string_/get_real_ still
  ! carries that pre-existing flaw for value EXTRACTION (left as-is for
  ! wire-parity — it only ever leaves a key unset, never misapplies), but it must
  ! NOT leak into TYPE validation: an unanchored hit on a value would falsely
  ! decline a well-typed frame, or mask a genuine type error on the real key.
  ! Scans matches left-to-right and returns the first real key.
  subroutine find_key_(buf, key, vstart, blen, found)
    character(len=*), intent(in)  :: buf, key
    integer,          intent(out) :: vstart, blen
    logical,          intent(out) :: found
    integer :: start, rel, kpos, klen, p

    found  = .false.
    vstart = 0
    blen   = len_trim(buf)
    klen   = len_trim(key)
    start  = 1
    do
       if (start .gt. blen) return
       rel = index(buf(start:blen), key)
       if (rel .eq. 0) return
       kpos  = start + rel - 1       ! absolute start of this match
       start = kpos + 1              ! next search resumes after this match
       ! preceding non-space char must be '{' or ',' (a structural key position)
       p = kpos - 1
       do while (p .ge. 1)
          if (buf(p:p) .ne. ' ') exit
          p = p - 1
       end do
       if (p .lt. 1) cycle
       if (buf(p:p) .ne. '{' .and. buf(p:p) .ne. ',') cycle
       ! following non-space char must be ':'
       p = kpos + klen
       do while (p .le. blen)
          if (buf(p:p) .ne. ' ') exit
          p = p + 1
       end do
       if (p .gt. blen) cycle
       if (buf(p:p) .ne. ':') cycle
       ! skip ':' and following spaces -> first value char
       p = p + 1
       do while (p .le. blen)
          if (buf(p:p) .ne. ' ') exit
          p = p + 1
       end do
       vstart = p
       found  = .true.
       return
    end do
  end subroutine find_key_

  ! Classify the JSON value type of `key` in a flat JSON buffer, WITHOUT parsing
  ! it, using the structural key locator above. Categorizes by the value's
  ! leading char:
  !   '"'                -> 'string'
  !   't' / 'f'          -> 'bool'   (true/false)
  !   'n'                -> 'null'
  !   '}' / ','          -> 'absent' (empty value, e.g. {"mycall":}) — treated as
  !                                    absent so malformed-empty values keep the
  !                                    current silent-skip (configure_parse_error
  !                                    territory, not a type error)
  !   anything else      -> 'number' (digit, sign, '.', or an odd value the typed
  !                                    parser will soft-reject as today)
  ! 'absent' means the key is not present (or has no value), which is never a
  ! type error (RFC §4.1: absent key -> leave at current value).
  subroutine value_category_(buf, key, cat)
    character(len=*), intent(in)  :: buf, key
    character(len=*), intent(out) :: cat
    integer   :: vstart, blen
    logical   :: found
    character :: c

    cat = 'absent'
    call find_key_(buf, key, vstart, blen, found)
    if (.not. found) return
    if (vstart .gt. blen) return
    c = buf(vstart:vstart)
    select case (c)
    case ('"');           cat = 'string'
    case ('t', 'f');      cat = 'bool'
    case ('n');           cat = 'null'
    case ('}', ',');      cat = 'absent'
    case default;         cat = 'number'
    end select
  end subroutine value_category_

  ! Type-check one configure key against its schema JSON type. Sets
  ! proceed=.true. when the key is absent OR present with an accepted category
  ! (the caller then parses it). Sets proceed=.false. and records the first
  ! type error in `terr` when the key is present with a mismatched category.
  !   accept_str  : the key accepts a JSON string value
  !   accept_num  : the key accepts a JSON number value
  !   accept_bool : (optional, default .false.) the key accepts a JSON boolean.
  !                 Added in Phase 4 for my_call_standard/his_call_standard;
  !                 existing int/real/string callers omit it and reject bools.
  ! (a 'null' value is always a mismatch for the keys handled here.)
  subroutine check_type_(buf, key, keyname, expected, accept_str, accept_num,  &
                         terr, proceed, accept_bool)
    character(len=*),         intent(in)    :: buf, key, keyname, expected
    logical,                  intent(in)    :: accept_str, accept_num
    type(control_type_error), intent(inout) :: terr
    logical,                  intent(out)   :: proceed
    logical, optional,        intent(in)    :: accept_bool
    character(len=8) :: cat
    logical          :: acc_bool

    acc_bool = .false.
    if (present(accept_bool)) acc_bool = accept_bool
    call value_category_(buf, key, cat)
    proceed = .true.
    if (trim(cat) .eq. 'absent') return
    if (trim(cat) .eq. 'string' .and. accept_str) return
    if (trim(cat) .eq. 'number' .and. accept_num) return
    if (trim(cat) .eq. 'bool'   .and. acc_bool)   return
    proceed = .false.
    call set_type_error_(terr, keyname, expected, cat)
  end subroutine check_type_

  ! ===== encode requests =================================================

  ! Read a pack request. Unknown keys are ignored.
  subroutine parse_encode_request(buf, req)
    character(len=*),     intent(in)  :: buf
    type(encode_request), intent(out) :: req
    character(len=:), allocatable :: text, template, word
    character(len=8) :: cat
    logical :: has_text, has_template, present, ok

    req%text = ''
    req%his_call = ''
    req%exchange = ''
    req%problem = ''
    req%problem_key = ''

    ! The key locator would read a key of a nested object as the request's, so
    ! the id is read with every nested object and array blanked.
    call request_int_(top_level_(buf), '"id"', req%id, present, req%id_ok)
    req%id_ok = present .and. req%id_ok
    if (.not. req%id_ok) return

    if (nested_(buf)) then
       call refuse_('', 'a request is a flat JSON object')
       return
    end if

    call request_string_(buf, '"text"', text, has_text, ok)
    if (.not. ok) then
       call refuse_('text', 'text must be a JSON string')
       return
    end if
    call request_string_(buf, '"template"', template, has_template, ok)
    if (.not. ok) then
       call refuse_('template', 'template must be a JSON string')
       return
    end if
    if (has_text .and. has_template) then
       call refuse_('template', 'a request has text or template, not both')
       return
    end if
    if (.not. (has_text .or. has_template)) then
       call refuse_('text', 'a request needs text or template')
       return
    end if
    req%is_template = has_template
    if (has_template) then
       req%text = template
    else
       req%text = text
    end if

    call value_category_(buf, '"final"', cat)
    if (trim(cat) .eq. 'bool') then
       call get_bool_(buf, '"final"', req%final, ok)
    else if (trim(cat) .ne. 'absent') then
       call refuse_('final', 'final must be true or false')
       return
    end if

    call request_string_(buf, '"profile"', word, present, ok)
    if (present) then
       ok = ok .and. (word .eq. 'none' .or. word .eq. 'field_day' .or. word .eq. 'rtty')
       ok = ok .and. len(word) .eq. len_trim(word)
       if (.not. ok) then
          call refuse_('profile', 'profile must be none, field_day or rtty')
          return
       end if
       if (word .eq. 'field_day') req%profile = 1
       if (word .eq. 'rtty') req%profile = 2
    end if

    call request_string_(buf, '"his_call"', req%his_call, present, ok)
    if (.not. ok) then
       call refuse_('his_call', 'his_call must be a JSON string')
       return
    end if
    call request_string_(buf, '"exchange"', req%exchange, present, ok)
    if (.not. ok) then
       call refuse_('exchange', 'exchange must be a JSON string')
       return
    end if

    call request_int_(buf, '"serial"', req%serial, req%serial_given, ok)
    if (.not. ok) then
       call refuse_('serial', 'serial must be an integer from -2147483648 to 2147483647')
       return
    end if
    call request_int_(buf, '"report"', req%report, req%report_given, ok)
    if (.not. ok) then
       call refuse_('report', 'report must be an integer from -2147483648 to 2147483647')
       return
    end if

  contains

    subroutine refuse_(key, detail)
      character(len=*), intent(in) :: key, detail
      req%problem_key = key
      req%problem = detail
    end subroutine refuse_

  end subroutine parse_encode_request

  ! Read a render request: a pack request's keys, then freq and rate.
  subroutine parse_render_request(buf, req)
    character(len=*),     intent(in)  :: buf
    type(encode_request), intent(out) :: req
    character(len=8) :: cat
    integer :: vstart, blen
    logical :: present, ok

    call parse_encode_request(buf, req)
    if (.not. req%id_ok .or. len(req%problem) .gt. 0) return
    call request_int_(buf, '"rate"', req%rate, present, ok)
    if (.not. (present .and. ok .and. (req%rate .eq. 12000 .or. req%rate .eq. 48000))) then
       req%problem_key = 'rate'
       req%problem = 'rate must be 12000 or 48000'
       return
    end if
    call value_category_(buf, '"freq"', cat)
    ok = trim(cat) .eq. 'number'
    if (ok) then
       call find_key_(buf, '"freq"', vstart, blen, present)
       ok = json_number_(buf(vstart:blen), .true.)
    end if
    if (ok) call get_real_(buf, '"freq"', req%freq, ok)
    ! Written so that a NaN fails it.
    if (.not. (ok .and. req%freq .ge. 200.d0 .and. req%freq .le. 5000.d0)) then
       req%problem_key = 'freq'
       req%problem = 'freq must be a number of Hz from 200 to 5000'
    end if
  end subroutine parse_render_request

  ! The string value of key. present is .false. when the key is absent; ok
  ! is .false. when it is present but not a JSON string that decodes.
  subroutine request_string_(buf, key, val, present, ok)
    character(len=*),              intent(in)  :: buf, key
    character(len=:), allocatable, intent(out) :: val
    logical,                       intent(out) :: present, ok
    character(len=8) :: cat
    integer :: vstart, blen

    val = ''
    call value_category_(buf, key, cat)
    present = trim(cat) .ne. 'absent'
    ok = .not. present
    if (trim(cat) .ne. 'string') return
    call find_key_(buf, key, vstart, blen, present)
    call json_string_(buf(vstart + 1:blen), val, ok)
  end subroutine request_string_

  ! The transmitter's copy of the configure string value of key, which
  ! decodes: the whole string less ASCII white space at either end. long is
  ! .true. when it has more characters than val has bytes.
  subroutine station_value_(buf, key, val, long)
    character(len=*), intent(in)  :: buf, key
    character(len=*), intent(out) :: val
    logical,          intent(out) :: long
    character(len=*), parameter :: WHITE = ' ' // achar(9) // achar(10) //   &
         achar(11) // achar(12) // achar(13)
    character(len=:), allocatable :: s
    logical :: present, ok
    integer :: first, last, i

    call request_string_(buf, key, s, present, ok)
    val = ' '
    long = .false.
    first = verify(s, WHITE)
    if (first .eq. 0) return
    last = verify(s, WHITE, back=.true.)
    val = s(first:min(last, first + len(val) - 1))
    ! A UTF-8 continuation byte, 10xxxxxx, is part of the character before it.
    long = count([(iand(ichar(s(i:i)), 192) .ne. 128, i = first, last)]) .gt. len(val)
  end subroutine station_value_

  ! The integer value of key: a JSON integer that get_int_ reads. present is
  ! .false. when the key is absent; ok is .false. when it is present but no
  ! such integer.
  subroutine request_int_(buf, key, val, present, ok)
    character(len=*), intent(in)  :: buf, key
    integer,          intent(out) :: val
    logical,          intent(out) :: present, ok
    character(len=8) :: cat
    integer :: vstart, blen
    logical :: found

    val = 0
    call value_category_(buf, key, cat)
    present = trim(cat) .ne. 'absent'
    ok = .not. present
    if (trim(cat) .ne. 'number') return
    call find_key_(buf, key, vstart, blen, found)
    ok = json_number_(buf(vstart:blen), .false.)
    if (ok) call get_int_(buf, key, val, ok)
  end subroutine request_int_

  ! Whether s starts with a JSON number, -?(0|[1-9][0-9]*), followed when
  ! fraction is .true. by (\.[0-9]+)?([eE][+-]?[0-9]+)?, ending at ',', '}',
  ! white space or the end of s. get_int_ and get_real_ read more: their
  ! list-directed reads take '/', repeat counts and forms like '+5', '05' and
  ! '1500.'.
  logical function json_number_(s, fraction)
    character(len=*), intent(in) :: s
    logical,          intent(in) :: fraction
    integer :: i, start

    json_number_ = .false.
    i = 1
    if (i .le. len(s)) then
       if (s(i:i) .eq. '-') i = i + 1
    end if
    start = i
    if (.not. digits_(i)) return
    if (s(start:start) .eq. '0' .and. i - start .gt. 1) return
    if (fraction .and. i .le. len(s)) then
       if (s(i:i) .eq. '.') then
          i = i + 1
          if (.not. digits_(i)) return
       end if
    end if
    if (fraction .and. i .le. len(s)) then
       if (s(i:i) .eq. 'e' .or. s(i:i) .eq. 'E') then
          i = i + 1
          if (i .le. len(s)) then
             if (s(i:i) .eq. '+' .or. s(i:i) .eq. '-') i = i + 1
          end if
          if (.not. digits_(i)) return
       end if
    end if
    if (i .le. len(s)) then
       if (index(',}', s(i:i)) .eq. 0 .and. iachar(s(i:i)) .gt. 32) return
    end if
    json_number_ = .true.

  contains

    ! Skip the digits of s from i; .false. when there are none.
    logical function digits_(i)
      integer, intent(inout) :: i
      integer :: first
      first = i
      do while (i .le. len(s))
         if (index('0123456789', s(i:i)) .eq. 0) exit
         i = i + 1
      end do
      digits_ = i .gt. first
    end function digits_

  end function json_number_

  ! Whether buf holds an array, or an object besides the outermost one,
  ! outside its strings.
  logical function nested_(buf)
    character(len=*), intent(in) :: buf
    integer :: i, objects
    logical :: in_string

    nested_ = .false.
    objects = 0
    in_string = .false.
    i = 1
    do while (i .le. len(buf))
       if (in_string) then
          if (buf(i:i) .eq. '\') then
             i = i + 1
          else if (buf(i:i) .eq. '"') then
             in_string = .false.
          end if
       else if (buf(i:i) .eq. '"') then
          in_string = .true.
       else if (buf(i:i) .eq. '[') then
          nested_ = .true.
          return
       else if (buf(i:i) .eq. '{') then
          objects = objects + 1
          if (objects .gt. 1) then
             nested_ = .true.
             return
          end if
       end if
       i = i + 1
    end do
  end function nested_

  ! buf with every character of a nested object or array blanked, strings
  ! inside them included, so that only the outermost object's keys remain.
  function top_level_(buf) result(top)
    character(len=*), intent(in) :: buf
    character(len=len(buf)) :: top
    integer :: i, depth
    logical :: in_string, escaped
    character :: c

    top = buf
    depth = 0
    in_string = .false.
    escaped = .false.
    do i = 1, len(buf)
       c = buf(i:i)
       if (in_string) then
          if (escaped) then
             escaped = .false.
          else if (c .eq. '\') then
             escaped = .true.
          else if (c .eq. '"') then
             in_string = .false.
          end if
       else if (c .eq. '"') then
          in_string = .true.
       else if (c .eq. '{' .or. c .eq. '[') then
          depth = depth + 1
       else if (c .eq. '}' .or. c .eq. ']') then
          if (depth .ge. 2) top(i:i) = ' '
          depth = max(depth - 1, 0)
          cycle
       end if
       if (depth .ge. 2) top(i:i) = ' '
    end do
  end function top_level_

  ! Record the FIRST per-key type error of a frame (subsequent ones are ignored;
  ! the whole frame is declined regardless).
  subroutine set_type_error_(terr, keyname, expected, got)
    type(control_type_error), intent(inout) :: terr
    character(len=*),         intent(in)    :: keyname, expected, got
    if (terr%present) return
    terr%present  = .true.
    terr%key      = keyname
    terr%expected = expected
    terr%got      = got
  end subroutine set_type_error_

end module streaming_control
