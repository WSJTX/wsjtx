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
  use streaming_control, only: configure_fields
  use jt9_params_init,   only: apply_per_mode_policy

  include 'jt9com.f90'

  private
  public :: apply_configure_fields
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
    ! eme_delay_seconds is applied HERE, before the apply_per_mode_policy
    ! call below: a frame that ALSO sets mode/trperiod re-runs that policy, which
    ! OVERWRITES emedelay (0.0 non-Q65-60s, 2.5 Q65-60s) — so a producer
    ! eme_delay_seconds survives only on a frame with NO mode/trperiod change
    ! (matches RFC §2.4). kin_samples/nzhsym_per_period are honored at apply, but
    ! the streaming period-prep block (~:331/:339) RECOMPUTES kin/nzhsym from the
    ! accumulated sample count before each decode, so they are inert on this path
    ! (documented; verified byte-identical). robust_mode/nagain_flag/
    ! clear_average -> logical(c_bool). tx_mode (enum) -> ntxmode (read only in
    ! the 65+9 dual-decode dispatch; inert for FT8/pure-JT9 fixtures).
    if (in_cfg%dt_tolerance_seconds_set)                                       &
         params%dttol    = real(in_cfg%dt_tolerance_seconds, kind(params%dttol))
    if (in_cfg%eme_delay_seconds_set)                                          &
         params%emedelay = real(in_cfg%eme_delay_seconds, kind(params%emedelay))
    if (in_cfg%kin_samples_set)       params%kin       = in_cfg%kin_samples
    if (in_cfg%nzhsym_per_period_set) params%nzhsym    = in_cfg%nzhsym_per_period
    if (in_cfg%npts_c0_array_set)     params%npts8     = in_cfg%npts_c0_array
    if (in_cfg%min_width_set)         params%minw      = in_cfg%min_width
    if (in_cfg%min_sync_set)          params%minsync   = in_cfg%min_sync
    if (in_cfg%n_2pass_set)           params%n2pass    = in_cfg%n_2pass
    if (in_cfg%robust_mode_set)       params%nrobust   = in_cfg%robust_mode
    if (in_cfg%nagain_flag_set)       params%nagain    = in_cfg%nagain_flag
    if (in_cfg%tx_mode_set)           params%ntxmode   = in_cfg%tx_mode
    if (in_cfg%clear_average_set)     params%nclearave = in_cfg%clear_average

    ! ===== Phase 6: FT8/FT4 specifics cluster (RFC v1 §2) ===================
    ! Flat 1:1, no per-mode-policy interaction (none of these 24 are touched by
    ! apply_per_mode_policy / the nfa-nfb-restore / ntol blocks below). 21 of 24
    ! are INERT on the jt9 --stream FT8/JT9 fixtures (read only inside the
    ! multithreaded-FT8 block decoder.f90:192, which streaming forces off via
    ! lmultift8=.false.; jt65_ap_on is JT65-only; superfox_mode/even_sequence are
    ! superfox-path-only (ncontest==7 / sfrx_sub); ft8_decoder_start is non-stream
    ! (WAV/shmem) path; hint_mode unread anywhere) —
    ! parse-unit + apply-routing-unit + no-regression guard them. Only ft8_ap_on
    ! -> lft8apon, ap_cq_only -> lapcqonly, ap_width_hz -> napwid reach the
    ! single-pass FT8 decoder (decoder.f90:1141-1146), but they are EMPIRICALLY
    ! inert on the FT8 fixture in the FLIP direction too (ft8_ap_on=false / AP off,
    ! ap_cq_only=true, ap_width_hz=5000 each yield byte-identical 21 decodes vs
    ! baseline across 6 clean paired trials — the fixture has no AP-only
    ! decodes), so no decode-effect gate is possible. ap_width_hz stores the RAW
    ! wire int (the decoder halves it at lib/ft8var/ft8bvar.f90:1040 for the lapmyc
    ! AP path, §3 napwid note) — do NOT pre-scale here. nagain_filter/stop_hint/hint_mode
    ! assign logical -> logical(c_bool) nagainfil/nstophint/nhint (n-prefix, but
    ! bool-typed in the struct).
    ! multithreaded_ft8: the receiver IGNORES a non-false value.
    ! Streaming has NO multithreaded-FT8 support — lmultift8=.true. takes the
    ! decoder.f90:192 multithread branch, which yields 0 FT8 decodes on the stream
    ! path (deterministic: 0 vs a 21-decode baseline, every trial).
    ! So apply ONLY a false value (a no-op given the init-forced false), keeping the
    ! forced-false invariant; a true is dropped so a producer can never enable the
    ! broken path. Re-enable a straight pass-through if/when multithread streaming
    ! lands. (The other 23 keys are flat pass-throughs — inert on the FT8/JT9
    ! fixtures; verified byte-identical at probe values.)
    if (in_cfg%multithreaded_ft8_set .and. .not. in_cfg%multithreaded_ft8)      &
         params%lmultift8 = .false.
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
    ! No per-mode-policy interaction (none of these 6 are touched by
    ! apply_per_mode_policy / the nfa-nfb-restore / ntol blocks below). 5 of the 6
    ! are flat pass-throughs and INERT on the jt9 --stream FT8/JT9 fixtures:
    ! currently_txing/mode_changed/sec_band_changed/delay_units/
    ! last_tx_seconds_ago are read ONLY inside the multithreaded-FT8 block
    ! (decoder.f90:192..1148, which streaming forces off via lmultift8=.false.), so
    ! no decode-output gate is possible -> the parse-unit (run-phase7-parse-unit.sh)
    ! + the apply-routing unit (run-apply-routing-unit.sh, sentinel-opposite slot
    ! asserts) + no-regression are their coverage (like Phase 4).
    ! currently_txing/mode_changed assign logical -> logical(c_bool)
    ! ltxing/lmodechanged. OMITTED wire keys (utc_list*/nlist_count/datetime/
    ! from_wav_file/new_data_flag) are documented in streaming_control.f90.
    !
    ! qso_progress_state -> nQSOProgress is the EXCEPTION: it reaches the
    ! single-pass FT8 decoder (decoder.f90:1141), where ft8b.f90:274/299 use it as a
    ! RAW index into nappasses(0:5) / naptypes(0:5,4) on the default
    ! lft8apon=.true. / lapcqonly=.false. path. An out-of-range producer value
    ! (<0 or >5) faults the decoder (an -fbounds-check abort -> a one-frame DoS;
    ! silent memory corruption without the flag) -- a qso_progress_state=99
    ! crashes jt9 --stream. The valid range is [0,5] universally (every decoder
    ! dimensions naptypes(0:5,4)/nappasses(0:5); the GUI MainWindow::m_QSOProgress
    ! enum is 0..5). So, exactly like the Phase 6 multithreaded_ft8 guard
    ! (the receiver IGNORES a value that would break the decode path), the apply
    ! IGNORES an out-of-range value, keeping the safe default. Valid [0,5] pass
    ! through UNCHANGED -> wire-parity preserved for every value the CLI -Q path
    ! accepts (the CLI's identical out-of-range crash, jt9.f90:191, is a pre-existing
    ! latent bug the streaming wire contract declines to inherit). On the FT8/JT9
    ! fixtures an in-range qso_progress_state is empirically INERT (it only steers AP
    ! passes and the fixture has no AP-only decodes -> qso=5 gives a
    ! byte-identical 21-decode set), so the in-range routing is guarded by the
    ! apply-routing unit; a survival fixture guards the out-of-range case (negative-controlled).
    if (in_cfg%last_tx_seconds_ago_set) params%nlasttx         = in_cfg%last_tx_seconds_ago
    if (in_cfg%currently_txing_set)     params%ltxing          = in_cfg%currently_txing
    if (in_cfg%mode_changed_set)        params%lmodechanged    = in_cfg%mode_changed
    if (in_cfg%qso_progress_state_set .and. in_cfg%qso_progress_state >= 0        &
         .and. in_cfg%qso_progress_state <= 5)                                    &
         params%nQSOProgress = in_cfg%qso_progress_state
    if (in_cfg%sec_band_changed_set)    params%nsecbandchanged = in_cfg%sec_band_changed
    if (in_cfg%delay_units_set)         params%ndelay          = in_cfg%delay_units

    ! ===== Phase 8: string & scaled / derived fields (RFC v1 §2/§6) =========
    ! No per-mode-policy interaction. The cfg members already hold the DERIVED
    ! legacy ints (utc_nutc / date_yymmdd / n_trials_nranera — computed at parse,
    ! streaming_control.f90), so date/n_trials are flat pass-throughs here; only
    ! candthin_threshold/dt_center_seconds scale at apply (real(8) wire -> int /100,
    ! §6.5). INERT on the FT8/JT9 stream fixtures except utc (read-site map+probe):
    ! ncandthin/ndtcenter are read ONLY by the multithread FT8-variant decoder
    ! my_ft8var%decodevar (every such call — first at decoder.f90:348 — scales /100
    ! into the variant sync thresholds sync8var.f90:17-18), and every such call lives
    ! inside the if(lmultift8 .and. nmode==8) block (opens decoder.f90:192, else at
    ! :1140) that streaming forces off — the single-pass FT8 decoder
    ! (my_ft8%decode, :1141) is NOT passed them (probed inert: 5.0 / 2.0 s gave the
    ! byte-identical 21-decode set). n_trials->nranera->ntrials is JT65-deep-search-only
    ! (ntrials computed decoder.f90:143-145, consumed :1338/:1365, unused on FT8/JT9);
    ! date->yymmdd is superfox-only (sfrx_sub.f90). So those four are guarded by
    ! parse-unit + apply-routing-unit + no-regression; utc is decode-observable
    ! (round-trip fixtures). NONE is an array index, so unlike qso_progress_state no
    ! out-of-range apply guard is needed (read-site check: nranera<=18
    ! keeps 10**(nranera/2) within int32; ncandthin/ndtcenter become real thresholds).
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
    if (in_cfg%nutc_set) params%nutc = in_cfg%nutc
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
          case default;      params%ntol = 1000     ! FT8/FT4/etc
          end select
       else
          ! Consumer set explicit ntol — apply per-mode cap on the consumer
          ! value. Q65 + JT65+JT9 always hard-override (decoder invariants).
          select case (io_mode)
          case (241, 242);   params%ntol = min(params%ntol, 100)
          case (66);         params%ntol = 10
          case (65 + 9);     params%ntol = 20
          case default;      params%ntol = min(params%ntol, 1000)
          end select
       end if
    end if
  end subroutine apply_configure_fields

end module streaming_apply
