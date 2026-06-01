! Streaming control-frame parser.
!
! Parses control JSON frames received on stdin via the streaming
! framing. Two frame types are recognized today:
!
!   {"t":"configure","mode":"FT8","depth":3,"rxfreq":1500,
!                    "mycall":"KJ5HST","mygrid":"EM18","trperiod":15}
!   {"t":"halt"}
!
! Hand-rolled flat-JSON parser. Callers should treat unknown "t" values
! as forward-compat extensions (mirrors the consumer-ignores-unknown
! discipline from the ready handshake).
!
! Restrictions (deliberate scope):
!   - Flat objects only; no nested objects, no arrays.
!   - Numeric values must be plain decimal (no scientific notation).
!   - String values must NOT contain escape sequences. Callsigns, grids,
!     and mode names are alphanumeric ASCII per WSJT-X conventions, so
!     this is a safe simplification today; it can be extended if needed.

module streaming_control
  use, intrinsic :: iso_fortran_env, only: error_unit
  implicit none
  private

  ! Action returned by parse_control_frame.
  integer, parameter, public :: CTRL_UNKNOWN   = 0
  integer, parameter, public :: CTRL_CONFIGURE = 1
  integer, parameter, public :: CTRL_HALT      = 2
  integer, parameter, public :: CTRL_PARSE_ERR = 3

  type, public :: configure_fields
     ! Each "*_set" flag indicates whether the JSON contained that key.
     ! Callers apply only the fields that were set so partial updates work.
     logical                :: mode_set     = .false.
     integer                :: mode         = 0
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
     ! Per-period UTC as HHMMSS integer. Threaded into
     ! params%nutc so streaming_emit_decode can format the time field
     ! correctly. Producer should send a fresh nutc in each period's
     ! configure frame (mirrors WAV-path filename-derived per-invocation
     ! UTC).
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
     ! the "mode" parse. OBSERVABILITY (read-site map): on the
     ! jt9 --stream FT8/JT9 fixtures kin_samples(kin)+nzhsym_per_period(nzhsym)
     ! are OVERWRITTEN every period by the streaming period-prep block
     ! (streaming_io.f90 ~:331/:339); dttol/minw/minsync/n2pass/nrobust/nclearave
     ! are JT4/JT65/Q65-only; emedelay is forced to 0 by apply_per_mode_policy on
     ! a mode-setting frame; ntxmode is read only in mode 65+9 — so 10 of 12 are
     ! inert here (parse-unit + no-regression guard them). Only npts_c0_array(npts8)
     ! and nagain_flag(nagain) reach the FT8/JT9 decoders.
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
     ! Phase 6 (RFC v1 §2): FT8/FT4 specifics cluster — 19 bools + 5 ints, flat
     ! 1:1 onto params_block. OBSERVABILITY (read-site map + binary
     ! probe): 21 of 24 are INERT on the jt9 --stream FT8/JT9 fixtures — most are
     ! read ONLY inside the multithreaded-FT8 block (guard
     ! if(params%lmultift8 .and. nmode==8), decoder.f90:192), which streaming
     ! forces off (lmultift8=.false. constructed at jt9.f90:316); jt65_ap_on is
     ! JT65-only; superfox_mode/even_sequence are superfox-path-only;
     ! ft8_decoder_start is non-stream (WAV/shmem) path; hint_mode is never read. Only
     ! ft8_ap_on/ap_cq_only/ap_width_hz reach the single-pass FT8 decoder
     ! (decoder.f90:1141-1146, the lmultift8=.false. branch). ap_width_hz stores
     ! the RAW int (decode halves it at lib/ft8var/ft8bvar.f90:1040; §3 napwid
     ! note) — do NOT pre-scale. nagain_filter/stop_hint/hint_mode map to logical(c_bool)
     ! nagainfil/nstophint/nhint despite the n-prefix. Bools reuse the Phase 4/5
     ! get_bool_/accept_bool path; the 5 ints use get_int_. multithreaded_ft8 is
     ! PARSED faithfully but APPLY ignores a non-false value (a true re-enables the
     ! broken multithread branch -> 0 decodes; see streaming_apply.f90).
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
     ! flat 1:1 onto params_block. OBSERVABILITY (read-site map):
     ! ALL 6 are INERT on the jt9 --stream FT8/JT9 fixtures. Five are read ONLY
     ! inside the multithreaded-FT8 block (guard
     ! if(params%lmultift8 .and. nmode==8), decoder.f90:192..1148, which streaming
     ! forces off via lmultift8=.false.): currently_txing (ltxing, decoder.f90:228),
     ! mode_changed (lmodechanged, :193), sec_band_changed (nsecbandchanged,
     ! :208), delay_units (ndelay, :201/:1077), last_tx_seconds_ago (nlasttx,
     ! :337, consumed only by the multithread FT8-variant decoder ft8bvar/ft8svar).
     ! qso_progress_state (nQSOProgress) DOES reach the single-pass FT8 decoder
     ! (decoder.f90:1141) but only steers AP decoding passes; the FT8 fixture has
     ! no AP-only decodes, so an IN-RANGE value is empirically inert (probed:
     ! qso=5 gives a byte-identical 21-decode set). It is the one Phase 7 key the
     ! apply GUARDS (streaming_apply.f90): an OUT-OF-range value (<0 or >5) is a raw
     ! index into nappasses(0:5)/naptypes(0:5,4) at ft8b.f90:274/299 -> a decoder
     ! crash, so the apply ignores it (valid [0,5] pass through; see the apply
     ! comment). Parse stays a plain get_int_ (no parse-time range check -- v1 does
     ! type validation only, not range validation). No decode-output gate is
     ! possible for the inert in-range values -> parse-unit + apply-routing-unit +
     ! no-regression are the coverage (like Phase 4); the out-of-range guard is
     ! covered by a survival fixture + an apply-routing assert. The 4 ints use
     ! get_int_; the 2 bools (currently_txing/mode_changed, logical(c_bool) in
     ! jt9com.f90) reuse the get_bool_/accept_bool path.
     !
     ! OMITTED Phase 7 wire keys (intentionally NOT exposed in v1):
     !   utc_list / utc_list_0..9 / nlist_count — map to listutc, which is read
     !     ONLY by the JT4 averaging path (decoder.f90:1310, the nmode==4 block) and
     !     has no writer in the decode core — dead for the FT8/JT9/FT4/Q65 stream
     !     paths. Exposing it would be a no-op there.
     !   datetime — read into a dead local at decoder.f90:99 (assigned, never used);
     !     no decode effect. Only date->yymmdd carries time (Phase 8, §6.1).
     !   from_wav_file (ndiskdat) / new_data_flag (newdat) — receiver-managed
     !     invariants (§6.1/§9): the receiver hard-sets both, and a producer
     !     override would break the one-shot decode contract. Not producer-settable.
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
     ! bad nranera silently corrupts ntrials at decoder.f90:143-145) and date with
     ! year >= 2100 (§6.4 — the 2000+offset YYMMDD encoding caps at 2099).
     !
     ! PRECEDENCE (§4.1): utc is a SECOND route to nutc alongside the legacy int
     ! key nutc (kept for the legacy subset). utc_nutc is stored SEPARATELY from
     ! nutc; apply_configure_fields applies nutc THEN utc_nutc, so utc WINS when a
     ! frame carries both (richer ISO form). Order-independent at apply (the apply
     ! sequence encodes precedence, not the parse order).
     !
     ! OBSERVABILITY (read-site map + binary probe): utc->nutc
     ! round-trips through the decode "time" field (DECODE-OBSERVABLE via the
     ! nutc round-trip fixtures). The other four are INERT on the FT8/JT9 stream
     ! fixtures:
     !   - candthin_threshold->ncandthin / dt_center_seconds->ndtcenter are read
     !     ONLY by the multithread FT8-variant decoder my_ft8var%decodevar (all such
     !     calls — first at decoder.f90:348 — scale /100 into the variant's sync
     !     thresholds at lib/ft8var/sync8var.f90:17-18), and EVERY my_ft8var%decodevar
     !     call lives inside the if(lmultift8 .and. nmode==8) block (opens
     !     decoder.f90:192, else-arm at :1140) which streaming forces off — same class
     !     as the Phase 6/7 multithread keys. The single-pass FT8 decoder
     !     (my_ft8%decode, :1141) is NOT passed them. Probed inert: ncandthin
     !     scaled to 5.0 and ndtcenter to a 2.0 s shift each gave the byte-identical
     !     21-decode FT8 set.
     !   - n_trials->nranera->ntrials reaches only the JT65 deep-search decoder
     !     (ntrials computed at decoder.f90:143-145, consumed at :1338/:1365), unused
     !     on FT8/JT9.
     !   - date->yymmdd's only param read site is decoder.f90:182 (guarded by
     !     if(ncontest==7 .and. b_superfox .and. b_even_seq) -> superfox-only),
     !     decoded into yr/mo/day at lib/superfox/sfrx_sub.f90:21,25-27.
     ! None is an array index (array-bound check: nranera<=18 keeps the
     ! decode-site 10**(nranera/2) within int32 for any parseable valid n_trials;
     ! ncandthin/ndtcenter become real thresholds), so no out-of-range apply guard is
     ! needed (unlike qso_progress_state). The four inert keys are guarded by
     ! parse-unit + apply-routing-unit + no-regression (like Phase 4/6/7).
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

  public :: parse_control_frame
  public :: mode_string_to_int

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
    integer            :: int_val
    real(8)            :: real_val
    logical            :: lval
    logical            :: ok, proceed
    integer            :: hh, mm, ss, yr, mo, dy, enc   ! Phase 8 ISO/encode temps
    logical            :: vok                           ! Phase 8 value-parse ok

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
          if (int_val .ge. 0) then
             cfg%mode_set = .true.
             cfg%mode     = int_val
          end if
       else
          call get_int_(buf, '"mode"', int_val, ok)
          if (ok) then
             cfg%mode_set = .true.
             cfg%mode     = int_val
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
       end if
    end if

    call check_type_(buf, '"mygrid"', 'mygrid', 'string', .true., .false., terr, proceed)
    if (proceed) then
       call get_string_(buf, '"mygrid"', str_val, ok)
       if (ok) then
          cfg%mygrid_set = .true.
          cfg%mygrid     = str_val(:min(len(str_val), 6))
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
       if (ok) then
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
    ! ints use get_int_. All inert on the stream path (multithread-block-only /
    ! AP-only) — see the configure_fields Phase 7 comment for read sites + OMITs.
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
    ! decoder.f90:143-145). The check is a VALUE check (the JSON type is int), so it
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

  ! ===== internals ======================================================

  ! Find a string value in a flat JSON: looks for `key:"value"`, returns
  ! `value` unescaped. ok=.false. on miss or syntax error.
  subroutine get_string_(buf, key, val, ok)
    character(len=*), intent(in)  :: buf, key
    character(len=*), intent(out) :: val
    logical,          intent(out) :: ok
    integer :: kpos, vstart, vend, blen

    val = ' '
    ok  = .false.
    blen = len_trim(buf)
    kpos = index(buf(1:blen), key)
    if (kpos .eq. 0) return
    vstart = kpos + len_trim(key)
    ! skip ':' and whitespace
    do while (vstart .le. blen .and. (buf(vstart:vstart) .eq. ':' .or.    &
                                       buf(vstart:vstart) .eq. ' '))
       vstart = vstart + 1
    end do
    if (vstart .gt. blen) return
    if (buf(vstart:vstart) .ne. '"') return
    vstart = vstart + 1
    vend = index(buf(vstart:blen), '"')
    if (vend .le. 0) return
    val = buf(vstart : vstart + vend - 2)
    ok  = .true.
  end subroutine get_string_

  ! Find an integer value in a flat JSON: looks for `key:NNN`, value is
  ! decimal integer with optional sign. ok=.false. on miss or non-int.
  subroutine get_int_(buf, key, val, ok)
    character(len=*), intent(in)  :: buf, key
    integer,          intent(out) :: val
    logical,          intent(out) :: ok
    integer :: kpos, vstart, vend, blen, ios
    character :: ch

    val = 0
    ok  = .false.
    blen = len_trim(buf)
    kpos = index(buf(1:blen), key)
    if (kpos .eq. 0) return
    vstart = kpos + len_trim(key)
    do while (vstart .le. blen .and. (buf(vstart:vstart) .eq. ':' .or.    &
                                       buf(vstart:vstart) .eq. ' '))
       vstart = vstart + 1
    end do
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
    integer :: kpos, vstart, vend, blen, ios
    character :: ch

    val = 0.d0
    ok  = .false.
    blen = len_trim(buf)
    kpos = index(buf(1:blen), key)
    if (kpos .eq. 0) return
    vstart = kpos + len_trim(key)
    do while (vstart .le. blen .and. (buf(vstart:vstart) .eq. ':' .or.    &
                                       buf(vstart:vstart) .eq. ' '))
       vstart = vstart + 1
    end do
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
    integer   :: kpos, vstart, blen
    character :: ch

    val = .false.
    ok  = .false.
    blen = len_trim(buf)
    kpos = index(buf(1:blen), key)
    if (kpos .eq. 0) return
    vstart = kpos + len_trim(key)
    do while (vstart .le. blen .and. (buf(vstart:vstart) .eq. ':' .or.    &
                                       buf(vstart:vstart) .eq. ' '))
       vstart = vstart + 1
    end do
    if (vstart .gt. blen) return
    ch = buf(vstart:vstart)
    if (ch .eq. 't') then
       val = .true.;  ok = .true.
    else if (ch .eq. 'f') then
       val = .false.; ok = .true.
    end if
  end subroutine get_bool_

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
  !   nranera even -> ntrials = 10**(nranera/2)    (decoder.f90:143)
  !   nranera odd  -> ntrials = 3*10**(nranera/2)  (decoder.f90:144)
  ! so n_trials must be 10^k or 3*10^k. ok=.false. for any other value (the caller
  ! then emits configure_type_error). Strip trailing factors of 10; the residual
  ! must be 1 (pure power of 10 -> nranera=2k) or 3 (3x -> nranera=2k+1). This is
  ! functionally equivalent to the RFC §6.6 reference algorithm (a log10-based form;
  ! same {10^N,3*10^N} acceptance set and same mapping), which likewise returns
  ! nranera=0 for n_trials=1; note
  ! decoder.f90:145 then special-cases nranera=0 -> ntrials=0 (a legacy-encoding
  ! quirk a producer hits only by sending the degenerate n_trials=1 — documented,
  ! not "fixed", per the wire-parity scope guardrail). nranera stays <= 18 for any
  ! n_trials that fits int32 (10^9), so the decode-site 10**(nranera/2) cannot
  ! overflow (read-site bound check — no out-of-range guard needed).
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
  !   nexp_decode = ncontest(bits0-2) | single<<5 | vhf<<6 | (nb+3)<<8
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
       do while (p .ge. 1 .and. buf(p:p) .eq. ' ')
          p = p - 1
       end do
       if (p .lt. 1) cycle
       if (buf(p:p) .ne. '{' .and. buf(p:p) .ne. ',') cycle
       ! following non-space char must be ':'
       p = kpos + klen
       do while (p .le. blen .and. buf(p:p) .eq. ' ')
          p = p + 1
       end do
       if (p .gt. blen) cycle
       if (buf(p:p) .ne. ':') cycle
       ! skip ':' and following spaces -> first value char
       p = p + 1
       do while (p .le. blen .and. buf(p:p) .eq. ' ')
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
