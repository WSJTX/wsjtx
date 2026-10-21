! Streaming apply — configure_fields -> params_block mapping.
!
! Extracted from streaming_io.f90's former nested-internal apply_configure_
! so the cfg->params apply ROUTING is unit-testable. The
! routing was previously unreachable (a nested internal sub of jt9_stream) and
! inert on the FT8/JT9 fixtures, so a mis-route (e.g. his_call -> hisgrid)
! passed every fixture gate byte-identical. This module makes the
! mapping a module-public procedure that a standalone driver can call against a
! bare params_block — see test/apply_routing_test.f90.
!
! This is a behavior-preserving relocation: the body below is the verbatim
! apply_configure_ body (sd%params -> params, the two host-associated baselines
! now explicit dummy args). The order-sensitive logic (rxfreq decouple,
! apply_per_mode_policy call + emedelay-before-policy, nfa/nfb baseline restore,
! ntol per-mode cap) keeps its original position, so behavior is identical.
!
! Dependencies (all acyclic — neither dependee uses this module):
!   streaming_control  -> configure_fields  (the source struct)
!   jt9_params_init    -> apply_per_mode_policy
!   include jt9com.f90 -> params_block (+ iso_c_binding / constants.f90)

module streaming_apply
  use streaming_control, only: configure_fields, NPTS_C0_ARRAY_MIN,         &
       NPTS_C0_ARRAY_MAX, MODE_JTTY
  use jt9_params_init,   only: apply_per_mode_policy
  use msk144_stream,     only: msk144_settings

  include 'jt9com.f90'

  private
  public :: apply_configure_fields, engine_range_key
  public :: ft8_settings_change, ft8_identity_change
  public :: apply_msk144_fields
  ! Re-export the include'd struct so a standalone unit driver can declare a
  ! params_block to assert apply routing against (test/apply_routing_test.f90)
  ! without itself including jt9com.f90. Same bind(C) type as everywhere else
  ! (F2018 §7.5.2.4 same-type rule), so it argument-associates with the
  ! jt9_params_init init routines exactly as streaming_io's include'd one does.
  public :: params_block

contains

  ! Apply a configure frame's set fields to a params_block + the caller's
  ! mode/TRperiod state. baseline_nfa/baseline_nfb are the session-baseline
  ! nfa/nfb (stashed by the caller at stream start) restored on mode change
  ! when the consumer didn't explicit-set them this frame.
  subroutine apply_configure_fields(in_cfg, io_mode, io_TRperiod, params,    &
       baseline_nfa, baseline_nfb)
    type(configure_fields), intent(in)    :: in_cfg
    integer,                intent(inout) :: io_mode
    real(8),                intent(inout) :: io_TRperiod
    type(params_block),     intent(inout) :: params
    integer,                intent(in)    :: baseline_nfa, baseline_nfb
    if (in_cfg%mode_set) then
       io_mode      = in_cfg%mode
       params%nmode = in_cfg%mode
       ! Default TRperiod for the new mode if no explicit trperiod follows.
       if (.not. in_cfg%trperiod_set) then
          select case (in_cfg%mode)
          case (8);   io_TRperiod = 15.d0
          case (5);   io_TRperiod = 7.5d0
          case (144); io_TRperiod = 30.d0
          case default; io_TRperiod = 60.d0
          end select
          params%ntr = int(io_TRperiod)
       end if
    end if
    if (in_cfg%trperiod_set) then
       io_TRperiod    = in_cfg%trperiod
       params%ntr  = int(io_TRperiod)
    end if
    if (in_cfg%depth_set)  params%ndepth   = in_cfg%depth
    if (in_cfg%submode_set) params%nsubmode = in_cfg%submode
    if ((in_cfg%mode_set.or.in_cfg%submode_set).and..not.in_cfg%min_width_set) then
       if(params%nmode==4.and.params%nsubmode==0) params%minw=0
    endif
    if (in_cfg%rxfreq_set) then
       params%nfqso = in_cfg%rxfreq
       params%nftx  = in_cfg%rxfreq
    end if
    if (in_cfg%mycall_set) params%mycall = transfer(in_cfg%mycall, params%mycall)
    if (in_cfg%mygrid_set) params%mygrid = transfer(in_cfg%mygrid, params%mygrid)

    ! ===== Phase 4: operator identity fields (RFC v1 §2) ====================
    ! char12/char6 via transfer() like mycall/mygrid (§4 — a width mismatch
    ! silently truncates/pads at the bind(C) boundary, so the configure_fields
    ! widths match the struct). Bools assign directly (logical -> logical(c_bool)).
    if (in_cfg%his_call_set)   params%hiscall  = transfer(in_cfg%his_call,  params%hiscall)
    if (in_cfg%his_grid_set)   params%hisgrid  = transfer(in_cfg%his_grid,  params%hisgrid)
    if (in_cfg%my_b_call_set)  params%mybcall  = transfer(in_cfg%my_b_call, params%mybcall)
    if (in_cfg%his_b_call_set) params%hisbcall = transfer(in_cfg%his_b_call, params%hisbcall)
    if (in_cfg%my_call_standard_set)  params%lmycallstd  = in_cfg%my_call_standard
    if (in_cfg%his_call_standard_set) params%lhiscallstd = in_cfg%his_call_standard

    ! Phase 4: frequency-window ints. tx_audio_offset_hz -> nftx applies AFTER
    ! the rxfreq block above so it DECOUPLES the tx side: rxfreq set both
    ! nfqso+nftx, and a co-present tx_audio_offset_hz then overrides nftx only.
    ! jt65_jt9_split_hz -> nfsplit (honored as written; a bare
    ! mid-stream mode switch does not auto-replay init-time fsplit). max_drift_hz
    ! -> max_drift (Q65 drift search; inert for FT8/JT9, so no decode-output
    ! fixture — verified at parse-unit + no-regression, like the other §4 fields).
    if (in_cfg%tx_audio_offset_hz_set) params%nftx      = in_cfg%tx_audio_offset_hz
    if (in_cfg%jt65_jt9_split_hz_set)  params%nfsplit   = in_cfg%jt65_jt9_split_hz
    if (in_cfg%max_drift_hz_set)       params%max_drift = in_cfg%max_drift_hz

    ! ===== Phase 5: decoder-tuning basic fields (RFC v1 §2) =================
    ! Flat 1:1. dt_tolerance_seconds/eme_delay_seconds narrow real(8) ->
    ! real(c_float) via the target's kind() (§4 — document the narrowing).
    ! eme_delay_seconds is applied before the apply_per_mode_policy call below,
    ! which a frame that also sets mode/trperiod re-runs (emedelay 2.5 for Q65
    ! at 60 s, else 0). The stream host sets emedelay as the GUI does (2.5 with
    ! decode_after_eme_delay, else 0): 0 at start, and again on a frame that
    ! sets the mode, the TR period or that key, where an eme_delay_seconds in
    ! the frame wins (streaming_io.f90). The host also sets kin and nzhsym before
    ! each decode, so kin_samples/nzhsym_per_period do not reach the decoder.
    ! robust_mode/nagain_flag/clear_average -> logical(c_bool); nagain holds
    ! only for the decode again nagain_flag asks for (streaming_io.f90).
    ! tx_mode (enum) -> ntxmode, read only by JT65+JT9.
    if (in_cfg%dt_tolerance_seconds_set)                                       &
         params%dttol    = real(in_cfg%dt_tolerance_seconds, kind(params%dttol))
    if (in_cfg%eme_delay_seconds_set)                                          &
         params%emedelay = real(in_cfg%eme_delay_seconds, kind(params%emedelay))
    if (in_cfg%kin_samples_set)       params%kin       = in_cfg%kin_samples
    if (in_cfg%nzhsym_per_period_set) params%nzhsym    = in_cfg%nzhsym_per_period
    if (in_cfg%npts_c0_array_set .and.                                      &
         in_cfg%npts_c0_array .ge. NPTS_C0_ARRAY_MIN .and.                  &
         in_cfg%npts_c0_array .le. NPTS_C0_ARRAY_MAX)                       &
         params%npts8 = in_cfg%npts_c0_array
    if (in_cfg%min_width_set)         params%minw      = in_cfg%min_width
    if (in_cfg%min_sync_set)          params%minsync   = in_cfg%min_sync
    if (in_cfg%n_2pass_set)           params%n2pass    = in_cfg%n_2pass
    if (in_cfg%robust_mode_set)       params%nrobust   = in_cfg%robust_mode
    if (in_cfg%nagain_flag_set)       params%nagain    = in_cfg%nagain_flag
    if (in_cfg%tx_mode_set)           params%ntxmode   = in_cfg%tx_mode
    if (in_cfg%clear_average_set)     params%nclearave = in_cfg%clear_average

    ! ===== Phase 6: FT8/FT4 specifics cluster (RFC v1 §2) ===================
    ! Flat 1:1; the per-mode policy and the ntol/nfa/nfb blocks below touch
    ! none of these. multithreaded_ft8 selects the multithreaded FT8 decoder
    ! (ft8_engine_kernel.f90), the only reader of ft8_cycles,
    ! ft8_rxf_sensitivity, ft8_threads, ft8_low_threshold, ft8_subpass,
    ! ap_my_call, hide_ft8_duplicates, enable_dxc_search, wide_dxc_search,
    ! hound_mode, multi_instance, skip_tx1, nagain_filter and stop_hint. The
    ! engine refuses every FT8 decode, single-threaded too, while ft8_threads is
    ! outside [0,24] (decoder_engine.f90); the kernel runs 13 to 24 on one
    ! thread (ft8_engine_kernel.f90). ft8_ap_on and ap_width_hz reach both FT8
    ! decoders; in the multithreaded one ft8_ap_on gates only the a8 pass.
    ! ap_cq_only reaches the single-threaded FT8 decoder, FT4, FST4 and Q65.
    ! ft8_decoder_start sets the multithreaded pass schedule (streaming_io.f90);
    ! superfox_mode and even_sequence drive SuperFox decoding
    ! (ft8_engine_kernel.f90, streaming_io.f90); jt65_ap_on is JT65's; no
    ! decoder reads common_ft8b or hint_mode. ap_width_hz is in Hz
    ! and is not pre-scaled: the decoders compare frequency offsets with it, or
    ! with half of it in one multithreaded AP pass (ft8var/ft8bvar.f90).
    ! nagain_filter/stop_hint/hint_mode assign logical -> logical(c_bool)
    ! nagainfil/nstophint/nhint (n-prefix, but bool-typed in the struct).
    if (in_cfg%multithreaded_ft8_set) params%lmultift8 = in_cfg%multithreaded_ft8
    if (in_cfg%ft8_cycles_set)          params%nft8cycles      = in_cfg%ft8_cycles
    if (in_cfg%ft8_rxf_sensitivity_set) params%nft8rxfsens     = in_cfg%ft8_rxf_sensitivity
    if (in_cfg%ft8_threads_set)         params%nmt             = in_cfg%ft8_threads
    if (in_cfg%ft8_decoder_start_set)   params%ndecoderstart   = in_cfg%ft8_decoder_start
    if (in_cfg%ft8_low_threshold_set)   params%lft8lowth       = in_cfg%ft8_low_threshold
    if (in_cfg%ft8_subpass_set)         params%lft8subpass     = in_cfg%ft8_subpass
    if (in_cfg%ft8_ap_on_set)           params%lft8apon        = in_cfg%ft8_ap_on
    if (in_cfg%ap_cq_only_set)          params%lapcqonly       = in_cfg%ap_cq_only
    if (in_cfg%ap_my_call_set)          params%lapmyc          = in_cfg%ap_my_call
    if (in_cfg%jt65_ap_on_set)          params%ljt65apon       = in_cfg%jt65_ap_on
    if (in_cfg%ap_width_hz_set)         params%napwid          = in_cfg%ap_width_hz
    if (in_cfg%hide_ft8_duplicates_set) params%lhideft8dupes   = in_cfg%hide_ft8_duplicates
    if (in_cfg%common_ft8b_set)         params%lcommonft8b     = in_cfg%common_ft8b
    if (in_cfg%enable_dxc_search_set)   params%lenabledxcsearch = in_cfg%enable_dxc_search
    if (in_cfg%wide_dxc_search_set)     params%lwidedxcsearch  = in_cfg%wide_dxc_search
    if (in_cfg%superfox_mode_set)       params%b_superfox      = in_cfg%superfox_mode
    if (in_cfg%even_sequence_set)       params%b_even_seq      = in_cfg%even_sequence
    if (in_cfg%hound_mode_set)          params%lhound          = in_cfg%hound_mode
    if (in_cfg%multi_instance_set)      params%lmultinst       = in_cfg%multi_instance
    if (in_cfg%skip_tx1_set)            params%lskiptx1        = in_cfg%skip_tx1
    if (in_cfg%nagain_filter_set)       params%nagainfil       = in_cfg%nagain_filter
    if (in_cfg%stop_hint_set)           params%nstophint       = in_cfg%stop_hint
    if (in_cfg%hint_mode_set)           params%nhint           = in_cfg%hint_mode

    ! ===== Phase 7: diagnostic / sequencing fields (RFC v1 §2) ==============
    ! Flat 1:1, outside the per-mode policy. last_tx_seconds_ago,
    ! currently_txing, mode_changed, sec_band_changed and delay_units are read
    ! only by the multithreaded FT8 decoder (ft8_engine_kernel.f90).
    ! currently_txing/mode_changed assign logical -> logical(c_bool)
    ! ltxing/lmodechanged. The keys left off the wire (utc_list*/nlist_count/
    ! datetime/from_wav_file/new_data_flag) are listed in streaming_control.f90.
    !
    ! qso_progress_state reaches both FT8 decoders, FT4, JT65, JT65+JT9, FST4
    ! and Q65, which index AP tables dimensioned (0:5,...) with it (ft8/ft8b.f90,
    ! ft8var/ft8bvar.f90, ft4_decode.f90, extract.f90, fst4_decode.f90,
    ! qra/q65/q65_ap.f90). The engine refuses a request with it outside [0,5]
    ! (decoder_engine.f90), the stream declines a configure that would leave
    ! one where a decoder reads it, and the CLI's -Q refuses one (jt9.f90).
    if (in_cfg%last_tx_seconds_ago_set) params%nlasttx         = in_cfg%last_tx_seconds_ago
    if (in_cfg%currently_txing_set)     params%ltxing          = in_cfg%currently_txing
    if (in_cfg%mode_changed_set)        params%lmodechanged    = in_cfg%mode_changed
    if (in_cfg%qso_progress_state_set)  params%nQSOProgress    = in_cfg%qso_progress_state
    if (in_cfg%sec_band_changed_set)    params%nsecbandchanged = in_cfg%sec_band_changed
    if (in_cfg%delay_units_set)         params%ndelay          = in_cfg%delay_units

    ! ===== Phase 8: string & scaled / derived fields (RFC v1 §2/§6) =========
    ! No per-mode-policy interaction. utc, date and n_trials arrive as the
    ! legacy ints computed at parse (streaming_control.f90); candthin_threshold
    ! and dt_center_seconds scale here to hundredths, which their only reader,
    ! the multithreaded FT8 decoder, divides back (ft8var/sync8var.f90). utc
    ! sets the decode time, and date reaches only the SuperFox decoder. n_trials
    ! is the trial count of JT65's soft-decision Reed-Solomon decoder
    ! (extract.f90 ftrsdap), used with VHF features on and for averaged decodes.
    ! The engine refuses a JT65 decode above 1,000,000 trials, nranera 12, the
    ! most the GUI offers in any mode. None of these indexes an array.
    if (in_cfg%date_set)     params%yymmdd  = in_cfg%date_yymmdd
    if (in_cfg%n_trials_set) params%nranera = in_cfg%n_trials_nranera
    if (in_cfg%candthin_threshold_set)                                          &
         params%ncandthin = nint(in_cfg%candthin_threshold * 100.d0)
    if (in_cfg%dt_center_seconds_set)                                           &
         params%ndtcenter = nint(in_cfg%dt_center_seconds * 100.d0)

    ! ===== Phase 9: bit-packed fields (RFC v1 §2.4/§2.5/§6.1/§6.2) ===========
    ! ndepth_packed / nexp_decode_packed were computed at parse (pack_phase9_ in
    ! streaming_control.f90) from the 9 unpacked keys, so apply is flat. The legacy
    ! `depth` full-int routes to ndepth verbatim above (the depth_set line near the
    ! top); a frame carrying BOTH depth and any unpacked ndepth key was declined at
    ! parse (conflict configure_type_error), so the depth and ndepth_packed
    ! assignments never both fire on an accepted frame. Only depth_level is
    ! decode-observable on FT8/JT9 (the fixtures run at depth=3 -> ndepth=3;
    ! depth_level=1 -> ndepth=1 changes the FT8 pass structure ft8_decode.f90:178/182
    ! and the JT9 iand(ndepth,7) tests jt9_decode.f90:88,92). The other 8
    ! unpacked keys reach Q65/JT65/FST4/JT4 or the contest branch only
    ! (single_decode/vhf_features/noise_blanker never reach the single-pass FT8
    ! my_ft8%decode), so they are guarded by the parse-unit (the bit-pack) + this
    ! routing (apply-routing-unit) + no-regression. No out-of-range apply guard is
    ! needed: the pack already masks depth_level/q65_maxiters to their
    ! bit widths and clamps the NB byte, and none reaches a raw array index.
    if (in_cfg%ndepth_packed_set)      params%ndepth      = in_cfg%ndepth_packed
    if (in_cfg%nexp_decode_packed_set) params%nexp_decode = in_cfg%nexp_decode_packed

    ! Decode-bandwidth fields. Apply BEFORE the per-mode
    ! policy block so consumer values flow through the cap: FT8/FST4 use
    ! min(ntol, cap) so a smaller consumer value wins; Q65 + JT65+JT9 hard-
    ! override the consumer value (those modes have decoder-side invariants
    ! tied to the per-mode ntol). nfa/nfb pass through unchanged.
    if (in_cfg%ntol_set) params%ntol = in_cfg%ntol
    if (in_cfg%nfa_set)  params%nfa  = in_cfg%nfa
    if (in_cfg%nfb_set)  params%nfb  = in_cfg%nfb

    ! Per-period UTC. Producer sends a fresh nutc in each
    ! period's configure frame so streaming_emit_decode can format the time
    ! field correctly (instead of "000000").
    if (in_cfg%nutc_set) then
       params%nutc = in_cfg%nutc
       ! Long-period FST4 numeric UTC also accepts legacy HHMM; ISO UTC is always HHMMSS.
       if ((io_mode.eq.240.or.io_mode.eq.241.or.io_mode.eq.242).and.io_TRperiod.ge.60.and. &
            params%nutc.le.2359) &
            params%nutc=100*params%nutc
       ! Legacy numeric JT4 and Q65 times may be HHMM.
       if ((io_mode.eq.4.or.io_mode.eq.66).and.io_TRperiod.ge.60.and.params%nutc.le.2359) &
            params%nutc=100*params%nutc
    endif
    ! Phase 8 (RFC v1 §6.3/§4.1): utc (ISO "HH:MM:SS") is a SECOND route to nutc,
    ! kept alongside the legacy int nutc above. Applied AFTER nutc so utc WINS when
    ! a frame carries both (the richer ISO form). The apply order encodes the
    ! precedence — it does NOT depend on the parse order. utc_nutc was derived from
    ! the ISO string at parse (streaming_control.f90 parse_iso_time_).
    if (in_cfg%utc_set) params%nutc = in_cfg%utc_nutc

    ! Re-apply per-mode policy whenever mode or trperiod changes.
    if (in_cfg%mode_set .or. in_cfg%trperiod_set) then
       ! Q65 60s emedelay (apply_per_mode_policy is emedelay-only).
       call apply_per_mode_policy(params, io_mode, io_TRperiod)

       ! Restore session-baseline nfa/nfb on mode change
       ! when consumer didn't explicit-set this frame. Mirrors WAV semantics
       ! (each invocation starts from CLI defaults).
       if (.not. in_cfg%nfa_set) params%nfa = baseline_nfa
       if (.not. in_cfg%nfb_set) params%nfb = baseline_nfb

       ! ntol mode-change policy. The cap-vs-restore logic
       ! differs from init-time (which always starts from CLI args), so the
       ! ntol block lives here at config-time rather than in
       ! apply_per_mode_policy.
       if (.not. in_cfg%ntol_set) then
          ! Restore per-mode default — NOT min()-clamping the carry-over.
          select case (io_mode)
          case (241, 242);   params%ntol = 100      ! FST4/FST4W
          case (65 + 9);     params%ntol = 20       ! JT65+JT9 hard
          case (66);         params%ntol = 10       ! Q65 hard
          case (MODE_JTTY);  params%ntol = 20       ! the GUI's JTTY F Tol
          case default;      params%ntol = 1000     ! FT8/FT4/etc
          end select
       else
          ! Consumer set explicit ntol — apply per-mode cap on the consumer
          ! value. Q65 + JT65+JT9 always hard-override (decoder invariants).
          select case (io_mode)
          case (241, 242);   params%ntol = min(params%ntol, 100)
          case (66);         params%ntol = 10
          case (65 + 9);     params%ntol = 20
          case (MODE_JTTY)   ! the stream's 0..6000 Hz range check limits it
          case default;      params%ntol = min(params%ntol, 1000)
          end select
       end if
    end if
  end subroutine apply_configure_fields

  ! An accepted configure's MSK144 options and calls. Only a configure's
  ! mycall and his_call become the decoder's calls, never the session's
  ! starting ones, which are not a station's.
  subroutine apply_msk144_fields(in_cfg, settings)
    type(configure_fields), intent(in)    :: in_cfg
    type(msk144_settings),  intent(inout) :: settings
    if (in_cfg%short_messages_set) settings%short_messages = in_cfg%short_messages
    if (in_cfg%swl_set)            settings%swl            = in_cfg%swl
    if (in_cfg%train_set)          settings%train          = in_cfg%train
    if (in_cfg%phase_eq_set)       settings%phase_eq       = in_cfg%phase_eq
    if (in_cfg%mycall_set)         settings%mycall         = in_cfg%mycall
    if (in_cfg%his_call_set)       settings%hiscall        = in_cfg%his_call
  end subroutine apply_msk144_fields

  ! The configure key behind an option field engine_params_problem names: the
  ! key this frame set, else the one whose earlier value the field carries
  ! (carried). The unpacked depth keys replace the whole depth, so a depth they
  ! leave at 0 names depth_level.
  function engine_range_key(field, in_cfg, carried) result(key)
    character(len=*),       intent(in)            :: field
    type(configure_fields), intent(in)            :: in_cfg
    logical,                intent(out), optional :: carried
    character(len=32)                             :: key
    logical                                       :: set
    select case (trim(field))
    case ('search_low_hz')
       key = 'nfa'; set = in_cfg%nfa_set
    case ('search_high_hz')
       key = 'nfb'; set = in_cfg%nfb_set
    case ('search_range')
       key = 'nfa'
       if (in_cfg%nfb_set .and. .not. in_cfg%nfa_set) key = 'nfb'
       set = in_cfg%nfa_set .or. in_cfg%nfb_set
    case ('receive_frequency_hz')
       key = 'rxfreq'; set = in_cfg%rxfreq_set
    case ('tolerance_hz')
       key = 'ntol'; set = in_cfg%ntol_set
    case ('depth')
       key = 'depth'
       if (in_cfg%ndepth_packed_set) key = 'depth_level'
       set = in_cfg%depth_set .or. in_cfg%ndepth_packed_set
    case ('submode')
       key = 'submode'; set = in_cfg%submode_set
    case ('minimum_width')
       key = 'min_width'; set = in_cfg%min_width_set
    case ('trials')
       key = 'n_trials'; set = in_cfg%n_trials_set
    case ('qso_progress')
       key = 'qso_progress_state'; set = in_cfg%qso_progress_state_set
    case ('threads')
       key = 'ft8_threads'; set = in_cfg%ft8_threads_set
    case ('period_seconds')
       key = 'trperiod'; set = in_cfg%trperiod_set .or. in_cfg%mode_set
    case ('eme_delay_seconds')
       key = 'eme_delay_seconds'
       if (in_cfg%decode_after_eme_delay_set .and. .not. in_cfg%eme_delay_seconds_set) &
            key = 'decode_after_eme_delay'
       set = in_cfg%eme_delay_seconds_set .or. in_cfg%decode_after_eme_delay_set .or. &
            in_cfg%mode_set .or. in_cfg%trperiod_set
    case ('blanker_mode', 'blanker_percent')
       key = 'noise_blanker_level'; set = in_cfg%noise_blanker_level_set
    case ('max_drift')
       key = 'max_drift_hz'; set = in_cfg%max_drift_hz_set
    case ('utc')
       key = 'nutc'
       if (in_cfg%utc_set) key = 'utc'
       set = in_cfg%utc_set .or. in_cfg%nutc_set
    case ('contest')
       key = 'contest_type'; set = in_cfg%contest_type_set
    case default
       ! flags, passes and aggressiveness: no configure key sets a value the
       ! engine refuses (the conversion makes them 0/1, clamps or fixes them).
       key = field; set = .true.
    end select
    if (present(carried)) carried = .not. set
  end function engine_range_key

  ! A change the GUI cancels its multithreaded FT8 backpressure on
  ! (cancelPendingFt8Decode, widgets/mainwindow.cpp): its FT8 decoder menus
  ! and Settings (my call and grid, SuperFox, special operating activity and
  ! Hound, single decode, VHF features, trials, decode after EME delay).
  pure logical function ft8_settings_change(was, now)
    type(params_block), intent(in) :: was, now
    ft8_settings_change = was%ndepth .ne. now%ndepth .or.                  &
         (was%lmultift8 .neqv. now%lmultift8) .or. was%nmt .ne. now%nmt .or. &
         was%ndecoderstart .ne. now%ndecoderstart .or.                     &
         was%nft8cycles .ne. now%nft8cycles .or.                           &
         was%nft8rxfsens .ne. now%nft8rxfsens .or.                         &
         (was%lft8lowth .neqv. now%lft8lowth) .or.                         &
         (was%lft8subpass .neqv. now%lft8subpass) .or.                     &
         (was%lwidedxcsearch .neqv. now%lwidedxcsearch) .or.               &
         (was%lhideft8dupes .neqv. now%lhideft8dupes) .or.                 &
         (was%lft8apon .neqv. now%lft8apon) .or.                           &
         any(was%mycall .ne. now%mycall) .or. any(was%mygrid .ne. now%mygrid) .or. &
         (was%b_superfox .neqv. now%b_superfox) .or.                       &
         (was%lhound .neqv. now%lhound) .or.                               &
         iand(was%nexp_decode, 255) .ne. iand(now%nexp_decode, 255) .or.   &
         was%nranera .ne. now%nranera .or. was%emedelay .ne. now%emedelay
  end function ft8_settings_change

  ! The rest of the GUI's pending-final identity, which it checks
  ! only when it takes the final up (DecodeOperatingContext.hpp
  ! hasSameFt8PendingIdentity, widgets/mainwindow.cpp publishPendingFt8Decode).
  pure logical function ft8_identity_change(was, now)
    type(params_block), intent(in) :: was, now
    ft8_identity_change = was%nfa .ne. now%nfa .or. was%nfb .ne. now%nfb .or. &
         was%nsubmode .ne. now%nsubmode
  end function ft8_identity_change

end module streaming_apply
