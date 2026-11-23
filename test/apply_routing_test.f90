! Apply-routing unit driver. Asserts that
! apply_configure_fields (extracted from streaming_io.f90's former nested-internal
! apply_configure_) routes each configure_fields member to its CORRECT params_block
! slot — the coverage gap the fixture gates cannot close (most Phase 4/5 fields are
! inert on the FT8/JT9 fixtures, so a mis-route like his_call->hisgrid passes every
! fixture byte-identical, as proven empirically).
!
! This drives the APPLY side (configure_fields -> params_block), complementing the
! parse-unit drivers (phase4/phase5_parse_test.f90) which drive the PARSE side
! (JSON -> configure_fields). It also exercises the order-sensitive logic that was
! previously review-only: the rxfreq/tx_audio_offset decouple, emedelay-before-
! per-mode-policy (D1), and WSJT-X's F Tol and search range across modes
! (search_controls).
!
! Bool swap-blindness (known, bounded): the Phase 6 bool asserts alternate T/F in
! source order, so an ADJACENT apply-line transposition (the realistic copy-paste
! swap) flips a value and IS caught; a swap of two NON-adjacent bools that share an
! asserted value is not (bools are binary). The parse-unit's per-key anchored
! NAME match is the primary swap guard (a swap there requires editing a literal
! key string, not transposing lines). Ints use distinct values, so int-swaps are
! always caught.
!
! params_block is re-exported from streaming_apply (it owns the include); the
! baseline is a realistic params built via jt9_params_init's init routines.
! Registered with CTest as test_streaming_apply; test/run-apply-routing-unit.sh,
! given a configured build directory, builds it there and runs it through ctest.
program apply_routing_test
  use streaming_control, only: configure_fields, parse_control_frame, control_type_error
  use jt9_params_init,   only: cli_args_t, init_default_params,             &
       init_streaming_extra_fields
  use streaming_apply,   only: apply_configure_fields, params_block, engine_range_key, &
       ft8_settings_change, ft8_identity_change, apply_msk144_fields,          &
       search_controls, start_search_controls, apply_search_controls, NTOL_UNSET
  use streaming_control, only: MODE_JTTY, MODE_FREQCAL, MODE_ECHO
  use msk144_stream,     only: msk144_settings
  implicit none
  integer :: nfail
  integer, parameter :: BNFA = 111, BNFB = 222   ! baseline nfa/nfb
  nfail = 0

  call frame_flat()
  call frame_call_flags()
  call frame_npts_c0_guard()
  call frame_rxfreq()
  call frame_decouple()
  call frame_emedelay_survive()
  call frame_emedelay_ft8()
  call frame_emedelay_q65()
  call frame_nfa_nfb_restore()
  call frame_rx_tx()
  call frame_ntol_policy()
  call frame_mode_trperiod()
  call frame_multithreaded_pass_through()
  call frame_engine_limits()
  call frame_range_keys()
  call frame_utc_precedence()
  call frame_fst4_utc()
  call frame_phase9_pack()
  call frame_cancel_fields()
  call frame_msk144()

  write(*,'(a)') '------------------------------------------------------------'
  if (nfail .eq. 0) then
     write(*,'(a)') 'ALL APPLY-ROUTING CHECKS PASSED'
  else
     write(*,'(a,i0,a)') 'APPLY-ROUTING: ', nfail, ' CHECK(S) FAILED'
     call exit(1)
  end if

contains

  subroutine ok(label, cond)
    character(len=*), intent(in) :: label
    logical,          intent(in) :: cond
    if (cond) then
       write(*,'(a,a)') '  PASS  ', label
    else
       write(*,'(a,a)') '  FAIL  ', label
       nfail = nfail + 1
    end if
  end subroutine ok

  ! Build a realistic, fully-defined baseline params (mode 8 / 15 s).
  subroutine fresh(p)
    type(params_block), intent(out) :: p
    type(cli_args_t) :: args
    call init_default_params(p, 8, 15.d0, args)
    call init_streaming_extra_fields(p, args)
  end subroutine fresh

  subroutine frame_npts_c0_guard()
    type(configure_fields) :: cfg
    type(params_block) :: p
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame npts_c0_array domain guard'
    call fresh(p)
    md = 9; tr = 60.d0
    p%npts8 = 74736
    cfg%npts_c0_array_set = .true.
    cfg%npts_c0_array = -1
    call apply(cfg, md, tr, p)
    call ok('negative npts_c0_array not applied', p%npts8 .eq. 74736)
    cfg%npts_c0_array = 0
    call apply(cfg, md, tr, p)
    call ok('zero npts_c0_array not applied', p%npts8 .eq. 74736)
    cfg%npts_c0_array = 81649
    call apply(cfg, md, tr, p)
    call ok('oversize npts_c0_array not applied', p%npts8 .eq. 74736)
    cfg%npts_c0_array = 81648
    call apply(cfg, md, tr, p)
    call ok('max npts_c0_array applied', p%npts8 .eq. 81648)
  end subroutine frame_npts_c0_guard

  ! Read back a c_char(N) params field as a fixed-length string (inverse of the
  ! transfer() the apply does).
  function s12(arr) result(s)
    character(len=12) :: s
    character, intent(in) :: arr(12)
    s = transfer(arr, s)
  end function s12

  function s6(arr) result(s)
    character(len=6) :: s
    character, intent(in) :: arr(6)
    s = transfer(arr, s)
  end function s6

  ! ===================================================================
  ! Frame FLAT: every flat field, distinct value, NO mode/trperiod (so the
  ! per-mode-policy block does NOT fire — emedelay survives, ntol/nfa/nfb keep
  ! the explicit value). This is the core routing assertion: each value must
  ! land in its OWN slot. The mis-route killers are the same-shaped pairs
  ! (his_call/hisgrid, tx_audio_offset/nfsplit, min_width/min_sync,
  ! dt_tolerance/emedelay).
  subroutine frame_flat()
    type(params_block)     :: p
    type(configure_fields) :: cfg
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame FLAT: every flat field -> its own slot (no mode/trperiod)'
    call fresh(p)
    ! Defeat baseline-collision vacuity: lmycallstd/lhiscallstd
    ! start from the start calls (blank here, so false), and mybcall/hisbcall are
    ! undefined after init. Plant sentinels OPPOSITE to each asserted value, so a
    ! dropped/mis-routed apply leaves the sentinel and the assert fails.
    p%lmycallstd  = .false.   ! asserted .true.
    p%lhiscallstd = .true.    ! asserted .false.
    p%mybcall  = transfer('zzzzzzzzzzzz', p%mybcall)
    p%hisbcall = transfer('zzzzzzzzzzzz', p%hisbcall)
    ! Phase 6 sentinels: plant each param OPPOSITE its asserted value (an
    ! assert whose expected value == the init default is vacuous). Bools
    ! alternate T/F asserted so a swap between adjacent keys flips a value and is
    ! caught; ints get a distinct 99 sentinel so a mis-route leaves 99 != asserted.
    ! multithreaded_ft8 is NOT set here (see frame_multithreaded_pass_through).
    p%lft8lowth       = .false.   ! asserted .true.
    p%lft8subpass     = .true.    ! asserted .false.
    p%lft8apon        = .true.    ! asserted .false.
    p%lapcqonly       = .false.   ! asserted .true.
    p%lapmyc          = .true.    ! asserted .false.
    p%ljt65apon       = .false.   ! asserted .true.
    p%lhideft8dupes   = .true.    ! asserted .false.
    p%lcommonft8b     = .false.   ! asserted .true.
    p%lenabledxcsearch = .true.   ! asserted .false.
    p%lwidedxcsearch  = .false.   ! asserted .true.
    p%b_superfox      = .true.    ! asserted .false.
    p%b_even_seq      = .false.   ! asserted .true.
    p%lhound          = .true.    ! asserted .false.
    p%lmultinst       = .false.   ! asserted .true.
    p%lskiptx1        = .true.    ! asserted .false.
    p%nagainfil       = .false.   ! asserted .true.
    p%nstophint       = .true.    ! asserted .false.
    p%nhint           = .false.   ! asserted .true.
    p%nft8cycles    = 99
    p%nft8rxfsens   = 99
    p%nmt           = 99
    p%ndecoderstart = 99
    p%napwid        = 99
    ! Phase 7 sentinels (plant OPPOSITE the asserted value). bools:
    ! ltxing asserted .true. -> plant .false.; lmodechanged asserted .false. ->
    ! plant .true. (the SENTINEL is load-bearing here since the init default is
    ! also .false., matching the asserted value). ints get a distinct 99.
    p%ltxing        = .false.   ! asserted .true.
    p%lmodechanged  = .true.    ! asserted .false.
    p%nlasttx        = 99
    p%nQSOProgress   = 99
    p%nsecbandchanged = 99
    p%ndelay         = 99
    ! Phase 8 sentinels (plant OPPOSITE the asserted value; the
    ! asserted values are ALSO non-default so neither the sentinel nor the default
    ! can mask a dropped apply). nranera default is 6 / ncandthin default 100, so
    ! the asserted 8 / 150 differ from both the planted 99 and the init default.
    ! utc is NOT exercised here (it shares the nutc slot — see frame_utc_precedence).
    p%yymmdd    = 99
    p%nranera   = 99
    p%ncandthin = 99
    p%ndtcenter = 99
    md = 8; tr = 15.d0
    cfg%depth_set = .true.;   cfg%depth = 7
    cfg%submode_set = .true.; cfg%submode = 2
    cfg%mycall_set = .true.;  cfg%mycall = 'K5TST'
    cfg%mygrid_set = .true.;  cfg%mygrid = 'EM10'
    cfg%his_call_set = .true.;  cfg%his_call = 'DX1ABC'
    cfg%his_grid_set = .true.;  cfg%his_grid = 'FN31'
    cfg%my_b_call_set = .true.; cfg%my_b_call = 'K5TST/B'
    cfg%his_b_call_set = .true.; cfg%his_b_call = 'DX1/B'
    cfg%my_call_standard_set = .true.;  cfg%my_call_standard = .true.
    cfg%his_call_standard_set = .true.; cfg%his_call_standard = .false.
    cfg%tx_audio_offset_hz_set = .true.; cfg%tx_audio_offset_hz = 1234
    cfg%jt65_jt9_split_hz_set = .true.;  cfg%jt65_jt9_split_hz = 2345
    cfg%max_drift_hz_set = .true.;       cfg%max_drift_hz = 55
    cfg%dt_tolerance_seconds_set = .true.; cfg%dt_tolerance_seconds = 0.7d0
    cfg%eme_delay_seconds_set = .true.;    cfg%eme_delay_seconds = 1.3d0
    cfg%kin_samples_set = .true.;      cfg%kin_samples = 99000
    cfg%nzhsym_per_period_set = .true.; cfg%nzhsym_per_period = 42
    cfg%npts_c0_array_set = .true.;    cfg%npts_c0_array = 60000
    cfg%min_width_set = .true.;        cfg%min_width = 3
    cfg%min_sync_set = .true.;         cfg%min_sync = 4
    cfg%n_2pass_set = .true.;          cfg%n_2pass = 1
    cfg%aggressive_set = .true.;       cfg%aggressive = 7
    cfg%robust_mode_set = .true.;      cfg%robust_mode = .true.
    cfg%nagain_flag_set = .true.;      cfg%nagain_flag = .true.
    cfg%tx_mode_set = .true.;          cfg%tx_mode = 9
    cfg%clear_average_set = .true.;    cfg%clear_average = .true.
    cfg%nutc_set = .true.;             cfg%nutc = 133430
    cfg%ntol_set = .true.;             cfg%ntol = 250
    cfg%nfa_set = .true.;              cfg%nfa = 180
    cfg%nfb_set = .true.;              cfg%nfb = 3900
    ! Phase 6 (23 keys; multithreaded_ft8 has its own frame). Bools
    ! alternate T/F (asserted); ints distinct (2/4/3/1/150).
    cfg%ft8_low_threshold_set = .true.;   cfg%ft8_low_threshold = .true.
    cfg%ft8_subpass_set = .true.;         cfg%ft8_subpass = .false.
    cfg%ft8_ap_on_set = .true.;           cfg%ft8_ap_on = .false.
    cfg%ap_cq_only_set = .true.;          cfg%ap_cq_only = .true.
    cfg%ap_my_call_set = .true.;          cfg%ap_my_call = .false.
    cfg%jt65_ap_on_set = .true.;          cfg%jt65_ap_on = .true.
    cfg%hide_ft8_duplicates_set = .true.; cfg%hide_ft8_duplicates = .false.
    cfg%common_ft8b_set = .true.;         cfg%common_ft8b = .true.
    cfg%enable_dxc_search_set = .true.;   cfg%enable_dxc_search = .false.
    cfg%wide_dxc_search_set = .true.;     cfg%wide_dxc_search = .true.
    cfg%superfox_mode_set = .true.;       cfg%superfox_mode = .false.
    cfg%even_sequence_set = .true.;       cfg%even_sequence = .true.
    cfg%hound_mode_set = .true.;          cfg%hound_mode = .false.
    cfg%multi_instance_set = .true.;      cfg%multi_instance = .true.
    cfg%skip_tx1_set = .true.;            cfg%skip_tx1 = .false.
    cfg%nagain_filter_set = .true.;       cfg%nagain_filter = .true.
    cfg%stop_hint_set = .true.;           cfg%stop_hint = .false.
    cfg%hint_mode_set = .true.;           cfg%hint_mode = .true.
    cfg%ft8_cycles_set = .true.;          cfg%ft8_cycles = 2
    cfg%ft8_rxf_sensitivity_set = .true.; cfg%ft8_rxf_sensitivity = 4
    cfg%ft8_threads_set = .true.;         cfg%ft8_threads = 3
    cfg%ft8_decoder_start_set = .true.;   cfg%ft8_decoder_start = 1
    cfg%ap_width_hz_set = .true.;         cfg%ap_width_hz = 150
    ! Phase 7 (6 keys; all flat pass-throughs). bools alternate T/F (asserted);
    ! ints distinct (4/5/7/6) and != the planted 99 sentinel.
    cfg%last_tx_seconds_ago_set = .true.; cfg%last_tx_seconds_ago = 4
    cfg%currently_txing_set = .true.;     cfg%currently_txing = .true.
    cfg%mode_changed_set = .true.;        cfg%mode_changed = .false.
    cfg%qso_progress_state_set = .true.;  cfg%qso_progress_state = 5
    cfg%sec_band_changed_set = .true.;    cfg%sec_band_changed = 7
    cfg%delay_units_set = .true.;         cfg%delay_units = 6
    ! Phase 8 (4 keys; utc tested in frame_utc_precedence). The cfg members hold
    ! the DERIVED legacy ints (utc_nutc/date_yymmdd/n_trials_nranera computed at
    ! parse) — here we set them directly. candthin_threshold/dt_center_seconds are
    ! reals scaled nint(x*100) at apply: 1.5 -> 150, 0.2 -> 20.
    cfg%date_set = .true.;              cfg%date_yymmdd = 210703
    cfg%n_trials_set = .true.;          cfg%n_trials_nranera = 8
    cfg%candthin_threshold_set = .true.; cfg%candthin_threshold = 1.5d0
    cfg%dt_center_seconds_set = .true.;  cfg%dt_center_seconds = 0.2d0
    call apply(cfg, md, tr, p)
    call ok('depth -> ndepth = 7',            p%ndepth   .eq. 7)
    call ok('submode -> nsubmode = 2',        p%nsubmode .eq. 2)
    call ok('mycall -> mycall = K5TST',       trim(s12(p%mycall))  .eq. 'K5TST')
    call ok('mygrid -> mygrid = EM10',        trim(s6(p%mygrid))   .eq. 'EM10')
    call ok('his_call -> hiscall = DX1ABC',   trim(s12(p%hiscall)) .eq. 'DX1ABC')
    call ok('his_grid -> hisgrid = FN31',     trim(s6(p%hisgrid))  .eq. 'FN31')
    call ok('my_b_call -> mybcall = K5TST/B',  trim(s12(p%mybcall))  .eq. 'K5TST/B')
    call ok('his_b_call -> hisbcall = DX1/B',  trim(s12(p%hisbcall)) .eq. 'DX1/B')
    call ok('my_call_standard -> lmycallstd true',       logical(p%lmycallstd))
    call ok('his_call_standard -> lhiscallstd false', .not. logical(p%lhiscallstd))
    call ok('tx_audio_offset_hz -> nftx = 1234 (not nfsplit)', p%nftx     .eq. 1234)
    call ok('jt65_jt9_split_hz -> nfsplit = 2345 (not nftx)',  p%nfsplit  .eq. 2345)
    call ok('max_drift_hz -> max_drift = 55',  p%max_drift .eq. 55)
    call ok('dt_tolerance_seconds -> dttol = 0.7', abs(p%dttol - 0.7) .lt. 1.0e-6)
    call ok('eme_delay_seconds -> emedelay = 1.3 (survives)', abs(p%emedelay - 1.3) .lt. 1.0e-6)
    call ok('kin_samples -> kin = 99000',      p%kin     .eq. 99000)
    call ok('nzhsym_per_period -> nzhsym = 42', p%nzhsym .eq. 42)
    call ok('npts_c0_array -> npts8 = 60000',  p%npts8   .eq. 60000)
    call ok('min_width -> minw = 3 (not minsync)',  p%minw    .eq. 3)
    call ok('min_sync -> minsync = 4 (not minw)',   p%minsync .eq. 4)
    call ok('n_2pass -> n2pass = 1',           p%n2pass  .eq. 1)
    call ok('aggressive -> naggressive = 7',   p%naggressive .eq. 7)
    call ok('robust_mode -> nrobust true',     logical(p%nrobust))
    call ok('nagain_flag -> nagain true',      logical(p%nagain))
    call ok('tx_mode -> ntxmode = 9',          p%ntxmode .eq. 9)
    call ok('clear_average -> nclearave true', logical(p%nclearave))
    call ok('nutc -> nutc = 133430',           p%nutc    .eq. 133430)
    call ok('ntol -> ntol = 250 (no policy)',  p%ntol    .eq. 250)
    call ok('nfa -> nfa = 180',                p%nfa     .eq. 180)
    call ok('nfb -> nfb = 3900',               p%nfb     .eq. 3900)
    ! Phase 6 routing (23 keys). Each asserted value is opposite its planted
    ! sentinel, so a dropped/mis-routed apply leaves the sentinel and fails.
    call ok('ft8_low_threshold -> lft8lowth true',        logical(p%lft8lowth))
    call ok('ft8_subpass -> lft8subpass false',     .not. logical(p%lft8subpass))
    call ok('ft8_ap_on -> lft8apon false',          .not. logical(p%lft8apon))
    call ok('ap_cq_only -> lapcqonly true',               logical(p%lapcqonly))
    call ok('ap_my_call -> lapmyc false',           .not. logical(p%lapmyc))
    call ok('jt65_ap_on -> ljt65apon true',               logical(p%ljt65apon))
    call ok('hide_ft8_duplicates -> lhideft8dupes false', .not. logical(p%lhideft8dupes))
    call ok('common_ft8b -> lcommonft8b true',            logical(p%lcommonft8b))
    call ok('enable_dxc_search -> lenabledxcsearch false', .not. logical(p%lenabledxcsearch))
    call ok('wide_dxc_search -> lwidedxcsearch true',     logical(p%lwidedxcsearch))
    call ok('superfox_mode -> b_superfox false',    .not. logical(p%b_superfox))
    call ok('even_sequence -> b_even_seq true',           logical(p%b_even_seq))
    call ok('hound_mode -> lhound false',           .not. logical(p%lhound))
    call ok('multi_instance -> lmultinst true',           logical(p%lmultinst))
    call ok('skip_tx1 -> lskiptx1 false',           .not. logical(p%lskiptx1))
    call ok('nagain_filter -> nagainfil true',            logical(p%nagainfil))
    call ok('stop_hint -> nstophint false',         .not. logical(p%nstophint))
    call ok('hint_mode -> nhint true',                    logical(p%nhint))
    call ok('ft8_cycles -> nft8cycles = 2',          p%nft8cycles    .eq. 2)
    call ok('ft8_rxf_sensitivity -> nft8rxfsens = 4', p%nft8rxfsens  .eq. 4)
    call ok('ft8_threads -> nmt = 3',                p%nmt           .eq. 3)
    call ok('ft8_decoder_start -> ndecoderstart = 1', p%ndecoderstart .eq. 1)
    call ok('ap_width_hz -> napwid = 150 (RAW, not halved)', p%napwid .eq. 150)
    ! Phase 7 routing (6 keys). Each asserted value is opposite its planted
    ! sentinel, so a dropped/mis-routed apply leaves the sentinel and fails.
    call ok('last_tx_seconds_ago -> nlasttx = 4',         p%nlasttx .eq. 4)
    call ok('currently_txing -> ltxing true',             logical(p%ltxing))
    call ok('mode_changed -> lmodechanged false',   .not. logical(p%lmodechanged))
    call ok('qso_progress_state -> nQSOProgress = 5',     p%nQSOProgress .eq. 5)
    call ok('sec_band_changed -> nsecbandchanged = 7',    p%nsecbandchanged .eq. 7)
    call ok('delay_units -> ndelay = 6',                  p%ndelay .eq. 6)
    ! Phase 8 routing (4 keys; utc in frame_utc_precedence). Each asserted value is
    ! opposite its planted sentinel (99) AND non-default, so a dropped/mis-routed
    ! apply leaves the sentinel and fails. candthin/dtcenter prove the /100 scaling.
    call ok('date -> yymmdd = 210703',                    p%yymmdd    .eq. 210703)
    call ok('n_trials -> nranera = 8',                    p%nranera   .eq. 8)
    call ok('candthin_threshold 1.5 -> ncandthin = 150',  p%ncandthin .eq. 150)
    call ok('dt_center_seconds 0.2 -> ndtcenter = 20',    p%ndtcenter .eq. 20)
  end subroutine frame_flat

  ! Frame CALL FLAGS: a call sent without its standard-call key sets that flag
  ! from the call, as the start values do; a key sent with it wins.
  subroutine frame_call_flags()
    type(params_block)     :: p
    type(configure_fields) :: cfg, cfg_my, cfg_his
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame CALL FLAGS: a call without its flag sets the flag from the call'
    call fresh(p)
    call ok('blank start calls -> both flags false', &
         .not. logical(p%lmycallstd) .and. .not. logical(p%lhiscallstd))
    md = 8; tr = 15.d0
    p%lmycallstd  = .true.    ! asserted .false.
    p%lhiscallstd = .false.   ! asserted .true.
    cfg%mycall_set = .true.;   cfg%mycall = 'PJ4/K1ABC'
    cfg%his_call_set = .true.; cfg%his_call = 'W9XYZ'
    call apply(cfg, md, tr, p)
    call ok('mycall PJ4/K1ABC alone -> lmycallstd false', .not. logical(p%lmycallstd))
    call ok('his_call W9XYZ alone -> lhiscallstd true', logical(p%lhiscallstd))
    cfg%mycall = 'K1ABC'
    cfg%his_call = ' '
    call apply(cfg, md, tr, p)
    call ok('mycall K1ABC alone -> lmycallstd true', logical(p%lmycallstd))
    call ok('his_call empty alone -> lhiscallstd false', .not. logical(p%lhiscallstd))
    ! A standard call with its key false: the key wins. One side per frame, so a
    ! guard that reads the other side's key is caught.
    p%lmycallstd  = .true.    ! asserted .false.
    cfg_my%mycall_set = .true.; cfg_my%mycall = 'K1ABC'
    cfg_my%my_call_standard_set = .true.; cfg_my%my_call_standard = .false.
    call apply(cfg_my, md, tr, p)
    call ok('mycall K1ABC with my_call_standard false -> false', .not. logical(p%lmycallstd))
    p%lhiscallstd = .true.    ! asserted .false.
    cfg_his%his_call_set = .true.; cfg_his%his_call = 'W9XYZ'
    cfg_his%his_call_standard_set = .true.; cfg_his%his_call_standard = .false.
    call apply(cfg_his, md, tr, p)
    call ok('his_call W9XYZ with his_call_standard false -> false', .not. logical(p%lhiscallstd))
  end subroutine frame_call_flags

  ! Frame RXFREQ: rxfreq sets BOTH nfqso and nftx.
  subroutine frame_rxfreq()
    type(params_block)     :: p
    type(configure_fields) :: cfg
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame RXFREQ: rxfreq -> nfqso AND nftx'
    call fresh(p)
    md = 8; tr = 15.d0
    cfg%rxfreq_set = .true.; cfg%rxfreq = 1600
    call apply(cfg, md, tr, p)
    call ok('rxfreq -> nfqso = 1600', p%nfqso .eq. 1600)
    call ok('rxfreq -> nftx  = 1600', p%nftx  .eq. 1600)
  end subroutine frame_rxfreq

  ! Frame DECOUPLE: rxfreq sets both, then tx_audio_offset_hz overrides nftx only.
  subroutine frame_decouple()
    type(params_block)     :: p
    type(configure_fields) :: cfg
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame DECOUPLE: rxfreq + tx_audio_offset_hz -> nfqso=rx, nftx=offset'
    call fresh(p)
    md = 8; tr = 15.d0
    cfg%rxfreq_set = .true.;             cfg%rxfreq = 1600
    cfg%tx_audio_offset_hz_set = .true.; cfg%tx_audio_offset_hz = 1700
    call apply(cfg, md, tr, p)
    call ok('nfqso = 1600 (rxfreq)',         p%nfqso .eq. 1600)
    call ok('nftx  = 1700 (offset overrides)', p%nftx .eq. 1700)
  end subroutine frame_decouple

  ! Frame EMEDELAY (D1a): eme_delay survives when NO mode/trperiod is set.
  subroutine frame_emedelay_survive()
    type(params_block)     :: p
    type(configure_fields) :: cfg
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame EMEDELAY-survive: eme_delay, no mode/trperiod -> survives'
    call fresh(p)
    md = 8; tr = 15.d0
    cfg%eme_delay_seconds_set = .true.; cfg%eme_delay_seconds = 1.9d0
    call apply(cfg, md, tr, p)
    call ok('emedelay = 1.9 (no policy)', abs(p%emedelay - 1.9) .lt. 1.0e-6)
  end subroutine frame_emedelay_survive

  ! Frame EMEDELAY (D1b): eme_delay + mode=8 -> per-mode policy clobbers to 0.0
  ! (emedelay is applied BEFORE the policy call, which overwrites it).
  subroutine frame_emedelay_ft8()
    type(params_block)     :: p
    type(configure_fields) :: cfg
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame EMEDELAY-ft8: eme_delay + mode=8 -> policy clobbers to 0.0'
    call fresh(p)
    md = 8; tr = 15.d0
    cfg%eme_delay_seconds_set = .true.; cfg%eme_delay_seconds = 1.9d0
    cfg%mode_set = .true.;              cfg%mode = 8
    call apply(cfg, md, tr, p)
    call ok('emedelay = 0.0 (FT8 policy)', abs(p%emedelay - 0.0) .lt. 1.0e-6)
  end subroutine frame_emedelay_ft8

  ! Frame EMEDELAY (D1c): eme_delay + mode=66 (Q65) + trperiod=60 -> policy = 2.5.
  subroutine frame_emedelay_q65()
    type(params_block)     :: p
    type(configure_fields) :: cfg
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame EMEDELAY-q65: eme_delay + Q65 60s -> policy = 2.5'
    call fresh(p)
    md = 8; tr = 15.d0
    cfg%eme_delay_seconds_set = .true.; cfg%eme_delay_seconds = 1.9d0
    cfg%mode_set = .true.;              cfg%mode = 66
    cfg%trperiod_set = .true.;          cfg%trperiod = 60.d0
    call apply(cfg, md, tr, p)
    call ok('emedelay = 2.5 (Q65 60s policy)', abs(p%emedelay - 2.5) .lt. 1.0e-6)
  end subroutine frame_emedelay_q65

  ! A configure as the stream applies it: the fields, then the search
  ! controls of a session that started with p's values (its nfa and nfb the
  ! command line's -L and -H, its ntol -F), set up again when the frame
  ! carries mode.
  subroutine apply(cfg, md, tr, p)
    type(configure_fields), intent(in)    :: cfg
    integer,                intent(inout) :: md
    real(8),                intent(inout) :: tr
    type(params_block),     intent(inout) :: p
    type(search_controls) :: c
    call start_search_controls(c, md, p)
    call apply_configure_fields(cfg, md, tr, p)
    call apply_search_controls(c, cfg, md, md, p, cfg%mode_set, .false., .false.)
  end subroutine apply

  ! One configure against the controls c: mode md entered when entry is true
  ! (a mode key, or an accepted Settings change), with ntol when nt >= 0.
  subroutine step(c, p, md, entry, nt)
    type(search_controls), intent(inout) :: c
    type(params_block),    intent(inout) :: p
    integer,               intent(in)    :: md, nt
    logical,               intent(in)    :: entry
    type(configure_fields) :: cfg
    cfg%mode_set = entry
    cfg%mode = md
    cfg%ntol_set = nt .ge. 0
    cfg%ntol = nt
    call apply_search_controls(c, cfg, -2, md, p, entry, .false., .false.)
  end subroutine step

  ! WSJT-X's F Tol across modes (widgets/mainwindow_modes.cpp, each setup;
  ! widgets/mainwindow_settings.cpp, its start): one box, which FT8, Q65 and
  ! MSK144 set to their saved F Tol on entry, JT4, JT9 and JT65 too with VHF
  ! features, FST4 to 20 without single decode and FST4W to 100; the other
  ! entries keep it. FST4W and JTTY have their own boxes.
  subroutine frame_ntol_policy()
    type(params_block)     :: p
    type(search_controls)  :: c
    type(configure_fields) :: cfg
    write(*,'(a)') 'Frame NTOL: WSJT-X''s F Tol across modes'
    ! The session's start without -F: FT8, Q65 and JT9 50, FT4 the form's 10,
    ! FST4 20, FST4W its own 100.
    call fresh(p); p%ntol = NTOL_UNSET
    call start_search_controls(c, 8, p)
    call ok('start FT8 -> 50', p%ntol .eq. 50)
    call fresh(p); p%ntol = NTOL_UNSET
    call start_search_controls(c, 66, p)
    call ok('start Q65 -> 50 (was 10)', p%ntol .eq. 50)
    call fresh(p); p%ntol = NTOL_UNSET
    call start_search_controls(c, 5, p)
    call ok('start FT4 -> 10', p%ntol .eq. 10)
    call fresh(p); p%ntol = NTOL_UNSET
    call start_search_controls(c, 240, p)
    call ok('start FST4 -> 20', p%ntol .eq. 20)
    call fresh(p); p%ntol = NTOL_UNSET
    call start_search_controls(c, 241, p)
    call ok('start FST4W -> 100', p%ntol .eq. 100)
    ! -F is set as a configure's ntol: at most 1000, FST4W at most 100.
    call fresh(p); p%ntol = 5000
    call start_search_controls(c, 8, p)
    call ok('start FT8 -F 5000 -> 1000', p%ntol .eq. 1000)
    call fresh(p); p%ntol = 300
    call start_search_controls(c, 241, p)
    call ok('start FST4W -F 300 -> 100', p%ntol .eq. 100)

    ! From FT8: JT9, JT4, JT65 and FreqCal without VHF features keep F Tol.
    call fresh(p); p%ntol = NTOL_UNSET
    call start_search_controls(c, 8, p)
    call step(c, p, 8, .false., 30)
    call ok('FT8 ntol 30 -> 30', p%ntol .eq. 30)
    call step(c, p, 9, .true., -1)
    call ok('-> JT9, VHF off -> 30 kept (was 1000)', p%ntol .eq. 30)
    call step(c, p, 4, .true., -1)
    call ok('-> JT4, VHF off -> 30 kept', p%ntol .eq. 30)
    call step(c, p, 65, .true., -1)
    call ok('-> JT65, VHF off -> 30 kept', p%ntol .eq. 30)
    call step(c, p, MODE_FREQCAL, .true., -1)
    call ok('-> FreqCal, VHF off -> 30 kept', p%ntol .eq. 30)
    ! With VHF features they put back their own (50 until set).
    p%nexp_decode = ior(p%nexp_decode, 64)
    call step(c, p, 9, .true., -1)
    call ok('-> JT9, VHF on -> 50', p%ntol .eq. 50)
    call step(c, p, 9, .false., 100)
    call ok('JT9 ntol 100 -> 100', p%ntol .eq. 100)
    call step(c, p, 8, .true., -1)
    call ok('-> FT8 -> its saved 30', p%ntol .eq. 30)
    call step(c, p, 9, .true., -1)
    call ok('-> JT9, VHF on -> its saved 100', p%ntol .eq. 100)
    call step(c, p, 4, .true., -1)
    call ok('-> JT4, VHF on -> 50', p%ntol .eq. 50)
    call step(c, p, 65, .true., -1)
    call ok('-> JT65, VHF on -> 50', p%ntol .eq. 50)
    call step(c, p, MODE_FREQCAL, .true., -1)
    call ok('-> FreqCal, VHF on -> JT9''s 100', p%ntol .eq. 100)
    ! An accepted Settings change sets the mode up again: VHF features turned
    ! on in JT9 put back JT9's F Tol.
    p%nexp_decode = iand(p%nexp_decode, not(64))
    call step(c, p, 8, .true., -1)
    call step(c, p, 9, .true., -1)
    call ok('FT8 -> JT9, VHF off -> FT8''s 30', p%ntol .eq. 30)
    p%nexp_decode = ior(p%nexp_decode, 64)
    call step(c, p, 9, .false., -1)
    call ok('VHF turned on, no Settings accept -> 30', p%ntol .eq. 30)
    cfg = configure_fields()
    call apply_search_controls(c, cfg, 9, 9, p, .true., .false., .false.)
    call ok('VHF turned on, Settings accepted -> JT9''s 100', p%ntol .eq. 100)
    ! Fast JT9 (VHF features in WSJT-X) puts back JT9's too.
    p%nexp_decode = iand(p%nexp_decode, not(64))
    call step(c, p, 8, .true., -1)
    cfg = configure_fields()
    cfg%mode_set = .true.; cfg%mode = 9
    call apply_search_controls(c, cfg, 9, 9, p, .true., .true., .false.)
    call ok('FT8 -> fast JT9 -> JT9''s 100', p%ntol .eq. 100)

    ! Q65 and MSK144 put back theirs; Q65 takes the value sent (was 10).
    call step(c, p, 66, .true., -1)
    call ok('-> Q65 -> 50 (was 10)', p%ntol .eq. 50)
    call step(c, p, 66, .true., 999)
    call ok('Q65 with ntol 999 -> 999 (was 10)', p%ntol .eq. 999)
    cfg = configure_fields()
    cfg%trperiod_set = .true.; cfg%trperiod = 30.d0
    call apply_search_controls(c, cfg, 66, 66, p, .false., .false., .false.)
    call ok('Q65 trperiod alone -> 999 kept', p%ntol .eq. 999)
    call step(c, p, 144, .true., -1)
    call ok('-> MSK144 -> 50 (was 1000)', p%ntol .eq. 50)
    call step(c, p, 66, .true., -1)
    call ok('-> Q65 -> its saved 999', p%ntol .eq. 999)
    call step(c, p, 8, .true., 5000)
    call ok('FT8 with mode and ntol 5000 -> 1000', p%ntol .eq. 1000)
    call step(c, p, 8, .false., 2000)
    call ok('FT8 ntol 2000 alone -> 2000, left to the range checks', p%ntol .eq. 2000)

    ! FST4: 20 without single decode; with it F Tol is kept. FST4W's own box
    ! (100, at most 100), and F Tol 100 after it.
    call step(c, p, 8, .true., 40)
    call step(c, p, 240, .true., -1)
    call ok('-> FST4 -> 20 (was 1000)', p%ntol .eq. 20)
    call step(c, p, 8, .true., -1)
    p%nexp_decode = ior(p%nexp_decode, 32)
    call step(c, p, 240, .true., -1)
    call ok('-> FST4, single decode -> FT8''s 40 kept', p%ntol .eq. 40)
    p%nexp_decode = iand(p%nexp_decode, not(32))
    call step(c, p, 241, .true., -1)
    call ok('-> FST4W -> 100', p%ntol .eq. 100)
    call step(c, p, 241, .true., 500)
    call ok('FST4W with mode and ntol 500 -> 100', p%ntol .eq. 100)
    call step(c, p, 241, .false., 30)
    call ok('FST4W ntol 30 -> 30', p%ntol .eq. 30)
    call step(c, p, 5, .true., -1)
    call ok('-> FT4 -> the 100 FST4W''s setup left in F Tol', p%ntol .eq. 100)
    call step(c, p, 241, .true., -1)
    call ok('-> FST4W -> its own 30', p%ntol .eq. 30)
    ! JTTY's own box: 20 until set, kept across modes.
    call step(c, p, MODE_JTTY, .true., -1)
    call ok('-> JTTY -> 20', p%ntol .eq. 20)
    call step(c, p, MODE_JTTY, .false., 60)
    call step(c, p, 8, .true., -1)
    call ok('-> FT8 -> its saved 40', p%ntol .eq. 40)
    call step(c, p, MODE_JTTY, .true., -1)
    call ok('-> JTTY -> its own 60', p%ntol .eq. 60)
    ! JT65+JT9 keeps the codec's rule: 20 with mode, what a frame without it says.
    call step(c, p, 74, .true., 999)
    call ok('JT65+JT9 with mode and ntol 999 -> 20', p%ntol .eq. 20)
    call step(c, p, 74, .false., 999)
    call ok('JT65+JT9 ntol 999 alone -> 999', p%ntol .eq. 999)

    ! SuperFox Hound: Rx Freq 750 on entry unless 700 to 800, held to
    ! 200..1400; F Tol held to 100 and saved so.
    call fresh(p); p%ntol = NTOL_UNSET
    call start_search_controls(c, 8, p)
    p%nexp_decode = ior(iand(p%nexp_decode, not(7)), 7)
    p%b_superfox = .true.
    p%nfqso = 1500
    call step(c, p, 8, .true., -1)
    call ok('SuperFox Hound entry, Rx 1500 -> 750', p%nfqso .eq. 750)
    call ok('SuperFox Hound entry -> F Tol 50', p%ntol .eq. 50)
    call rx(c, p, 8, 8, .false., 760)
    call step(c, p, 8, .true., -1)
    call ok('SuperFox Hound entry, Rx 760 -> 760 kept', p%nfqso .eq. 760)
    call step(c, p, 8, .false., 500)
    call ok('SuperFox Hound ntol 500 -> 100', p%ntol .eq. 100)
    cfg = configure_fields()
    cfg%rxfreq_set = .true.; cfg%rxfreq = 1500
    p%nfqso = 1500
    call apply_search_controls(c, cfg, 8, 8, p, .false., .false., .false.)
    call ok('SuperFox Hound rxfreq 1500 -> 1400', p%nfqso .eq. 1400)
    ! With multithreaded FT8 WSJT-X's SuperFox Hound decode takes 50.
    p%lmultift8 = .true.
    call step(c, p, 8, .false., 80)
    call ok('SuperFox Hound, multithreaded, F Tol 80 -> 50', p%ntol .eq. 50)
    p%lmultift8 = .false.
    call step(c, p, 8, .false., -1)
    call ok('SuperFox Hound, single-threaded -> F Tol 80', p%ntol .eq. 80)
    p%b_superfox = .false.
    call step(c, p, 8, .true., -1)
    call ok('Hound without SuperFox -> FT8''s saved 80', p%ntol .eq. 80)

    ! Each mode's own saved F Tol, distinct values (JT4 70, JT65 60, MSK144
    ! 300, JT9 90, Q65 40, FT8 30); Echo with VHF features takes JT4's.
    call fresh(p); p%ntol = NTOL_UNSET
    call start_search_controls(c, 8, p)
    p%nexp_decode = ior(p%nexp_decode, 64)
    call step(c, p, 4, .true., 70)
    call step(c, p, 65, .true., 60)
    call step(c, p, 144, .true., 300)
    call step(c, p, 9, .true., 90)
    call step(c, p, 66, .true., 40)
    call step(c, p, 8, .true., 30)
    call step(c, p, 4, .true., -1)
    call ok('-> JT4, VHF -> 70', p%ntol .eq. 70)
    call step(c, p, 65, .true., -1)
    call ok('-> JT65, VHF -> 60', p%ntol .eq. 60)
    call step(c, p, 144, .true., -1)
    call ok('-> MSK144 -> 300', p%ntol .eq. 300)
    call step(c, p, 9, .true., -1)
    call ok('-> JT9, VHF -> 90', p%ntol .eq. 90)
    call step(c, p, 66, .true., -1)
    call ok('-> Q65 -> 40', p%ntol .eq. 40)
    call step(c, p, MODE_ECHO, .true., -1)
    call step(c, p, 5, .true., -1)
    call ok('-> Echo, VHF -> JT4''s 70, then FT4 keeps it', p%ntol .eq. 70)
    ! Entering a mode holds a carried F Tol to its range: 1 to 1000, MSK144
    ! 20 to 500, saved so.
    call step(c, p, 9, .false., 3000)
    call ok('JT9 ntol 3000 alone -> 3000', p%ntol .eq. 3000)
    call step(c, p, 9, .true., -1)
    call ok('-> JT9 again -> held to 1000', p%ntol .eq. 1000)
    call step(c, p, 144, .false., 10)
    call step(c, p, 8, .true., -1)
    call step(c, p, 144, .true., -1)
    call ok('MSK144 saved 10 -> entry holds to 20', p%ntol .eq. 20)
    call step(c, p, 8, .false., 0)
    call step(c, p, 5, .true., -1)
    call ok('FT8 ntol 0 alone -> FT4 entry holds to 1', p%ntol .eq. 1)

    ! JT65+JT9 takes -F at the start, at most 1000.
    call fresh(p); p%ntol = 50
    call start_search_controls(c, 74, p)
    call ok('start JT65+JT9 -F 50 -> 50', p%ntol .eq. 50)
    call fresh(p); p%ntol = NTOL_UNSET
    call start_search_controls(c, 74, p)
    call ok('start JT65+JT9 -> 20', p%ntol .eq. 20)
    ! A negative -F is -F.
    call fresh(p); p%ntol = -5
    call start_search_controls(c, 8, p)
    call ok('start FT8 -F -5 -> -5, for the range check', p%ntol .eq. -5)
  end subroutine frame_ntol_policy

  ! The search range: the waterfall's (the command line's -L and -H until a
  ! frame sets it) in every mode but FST4, which searches F Low to F High
  ! (600 and 1400 until set, held inside the waterfall's range, F High in
  ! whole hundreds: chk_FST4_freq_range), or Rx Freq +/- F Tol with single
  ! decode (MainWindow::decode). A mode change keeps them, as WSJT-X's do.
  subroutine frame_nfa_nfb_restore()
    type(params_block)     :: p
    type(search_controls)  :: c
    type(configure_fields) :: cfg
    write(*,'(a)') 'Frame NFA/NFB: the waterfall''s range, and FST4''s'
    call fresh(p); p%ntol = NTOL_UNSET
    p%nfa = 200; p%nfb = 4000
    call start_search_controls(c, 8, p)
    call ok('start -> nfa = -L 200', p%nfa .eq. 200)
    cfg = configure_fields()
    cfg%nfa_set = .true.; cfg%nfa = 300
    call apply_search_controls(c, cfg, 8, 8, p, .false., .false., .false.)
    call ok('nfa 300 -> 300', p%nfa .eq. 300)
    call step(c, p, 66, .true., -1)
    call ok('-> Q65 -> nfa 300 kept (was the baseline)', p%nfa .eq. 300)
    call ok('-> Q65 -> nfb 4000 kept', p%nfb .eq. 4000)
    call step(c, p, 240, .true., -1)
    call ok('-> FST4 -> F Low 600', p%nfa .eq. 600)
    call ok('-> FST4 -> F High 1400', p%nfb .eq. 1400)
    cfg = configure_fields()
    cfg%nfa_set = .true.; cfg%nfa = 700
    call apply_search_controls(c, cfg, 240, 240, p, .false., .false., .false.)
    call ok('FST4 nfa 700 -> F Low 700', p%nfa .eq. 700)
    p%nexp_decode = ior(p%nexp_decode, 32)
    p%nfqso = 1500
    cfg = configure_fields()
    cfg%ntol_set = .true.; cfg%ntol = 50
    call apply_search_controls(c, cfg, 240, 240, p, .false., .false., .false.)
    call ok('FST4 single decode -> nfa = Rx - F Tol', p%nfa .eq. 1450)
    call ok('FST4 single decode -> nfb = Rx + F Tol', p%nfb .eq. 1550)
    p%nexp_decode = iand(p%nexp_decode, not(32))
    call step(c, p, 240, .false., -1)
    call ok('single decode off -> F Low 700 again', p%nfa .eq. 700)
    call step(c, p, 8, .true., -1)
    call ok('-> FT8 -> the waterfall''s 300', p%nfa .eq. 300)
    cfg = configure_fields()
    cfg%nfb_set = .true.; cfg%nfb = 1050
    call apply_search_controls(c, cfg, 8, 8, p, .false., .false., .false.)
    call step(c, p, 240, .true., -1)
    call ok('waterfall to 1050 -> FST4 F High 1000', p%nfb .eq. 1000)
    cfg = configure_fields()
    cfg%nfa_set = .true.; cfg%nfa = 100
    call apply_search_controls(c, cfg, 240, 240, p, .false., .false., .false.)
    call ok('FST4 nfa 100 -> held to the waterfall''s 300', p%nfa .eq. 300)
    ! A session that starts in FST4 takes -L and -H as F Low and F High.
    call fresh(p); p%ntol = NTOL_UNSET
    p%nfa = 250; p%nfb = 3000
    call start_search_controls(c, 240, p)
    call ok('start FST4 -> nfa = -L', p%nfa .eq. 250)
  end subroutine frame_nfa_nfb_restore

  ! Rx and Tx Freq on a mode's entry (widgets/mainwindow_modes.cpp). WSJT-X
  ! decodes at the waterfall's Rx (MSK144, fast JT9 and FreqCal at the Rx
  ! box), which a change of the Rx box moves, as do
  ! the setups of FST4, JT65, Q65, JTTY, SuperFox Hound, FreqCal and FST4W
  ! (its own box); FST4, FT4, JT4, JT65, Q65, JTTY (from another mode) and
  ! Hound without SuperFox put back the last Rx set in a mode that keeps it
  ! (not MSK144, FST4W, WSPR, Echo, FreqCal, SuperFox Hound), and the Tx
  ! likewise (FT8 too, but in Fox); MSK144 sets 1500; every setup but Hound's
  ! holds the box to its range. A frame's rxfreq wins.
  subroutine frame_rx_tx()
    type(params_block)     :: p
    type(search_controls)  :: c
    write(*,'(a)') 'Frame RX/TX: WSJT-X''s Rx and Tx Freq on entry'
    call fresh(p); p%ntol = NTOL_UNSET
    p%nfqso = 1500; p%nftx = 1500
    call start_search_controls(c, 8, p)
    call rx(c, p, 8, 8, .false., 1200)
    call go(c, p, 8, 144)
    call ok('FT8 Rx 1200 -> MSK144 -> Rx 1500', p%nfqso .eq. 1500)
    call ok('FT8 Tx 1200 -> MSK144 -> Tx 1500', p%nftx .eq. 1500)
    call go(c, p, 144, 8)
    call ok('MSK144 -> FT8 keeps Rx 1500 (FT8 sets no Rx)', p%nfqso .eq. 1500)
    call ok('MSK144 -> FT8 puts back Tx 1200', p%nftx .eq. 1200)
    call go(c, p, 8, 5)
    call ok('-> FT4 -> Rx 1200, the last kept', p%nfqso .eq. 1200)
    call ok('-> FT4 -> Tx 1200, the last kept', p%nftx .eq. 1200)
    call go(c, p, 5, MODE_FREQCAL)
    call ok('-> FreqCal -> Rx 1500', p%nfqso .eq. 1500)
    call go(c, p, MODE_FREQCAL, 66)
    call ok('FreqCal -> Q65 -> Rx 1200 (FreqCal keeps none)', p%nfqso .eq. 1200)
    call rx(c, p, 66, 65, .true., 900)
    call ok('-> JT65 with rxfreq 900 -> 900 (the frame wins)', p%nfqso .eq. 900)
    call go(c, p, 65, 9)
    call go(c, p, 9, MODE_JTTY)
    call ok('-> JTTY from JT9 -> Rx 900, the last kept', p%nfqso .eq. 900)
    call rx(c, p, MODE_JTTY, MODE_JTTY, .false., 2000)
    call go(c, p, MODE_JTTY, MODE_JTTY)
    call ok('JTTY again keeps Rx 2000', p%nfqso .eq. 2000)
    call rx(c, p, MODE_JTTY, 8, .true., 100)
    call go(c, p, 8, 9)
    call ok('a carried Rx 100 -> JT9 -> held to 200', p%nfqso .eq. 200)
    ! Fast JT9's 700 is kept for the modes that put Rx back; FT4 puts back
    ! Tx too, straight after MSK144's 1500.
    call fresh(p); p%ntol = NTOL_UNSET
    p%nfqso = 1200; p%nftx = 1200
    call start_search_controls(c, 8, p)
    call apply_search_controls(c, entering(9), 8, 9, p, .true., .true., .false.)
    call go(c, p, 9, 144)
    call ok('FT8 1200 -> MSK144 -> Tx 1500', p%nftx .eq. 1500)
    call go(c, p, 144, 5)
    call ok('fast JT9 -> MSK144 -> FT4 -> Rx 700, fast JT9''s kept', p%nfqso .eq. 700)
    call ok('fast JT9 -> MSK144 -> FT4 -> Tx 700, fast JT9''s kept', p%nftx .eq. 700)
    ! FST4W's own Rx; leaving it the waterfall keeps it where the next setup
    ! sets no Rx (FT8, and FT4 when the box is already the last kept), and
    ! takes the box's where it does (JT65), FreqCal's 1500 and fast JT9's 700.
    call fresh(p); p%ntol = NTOL_UNSET
    p%nfqso = 1200; p%nftx = 1200
    call start_search_controls(c, 8, p)
    call go(c, p, 8, 241)
    call ok('FT8 Rx 1200 -> FST4W -> its Rx 1500', p%nfqso .eq. 1500)
    call rx(c, p, 241, 241, .false., 1400)
    call ok('FST4W rxfreq 1400 -> 1400', p%nfqso .eq. 1400)
    call go(c, p, 241, 8)
    call ok('-> FT8 -> still FST4W''s 1400, as WSJT-X''s waterfall', p%nfqso .eq. 1400)
    call go(c, p, 8, 5)
    call ok('-> FT4, box already 1200 -> still 1400', p%nfqso .eq. 1400)
    call go(c, p, 5, 65)
    call ok('-> JT65 -> the box''s 1200', p%nfqso .eq. 1200)
    call go(c, p, 65, 241)
    call ok('-> FST4W -> its own 1400', p%nfqso .eq. 1400)
    call go(c, p, 241, MODE_FREQCAL)
    call ok('FST4W -> FreqCal -> Rx 1500', p%nfqso .eq. 1500)
    call go(c, p, MODE_FREQCAL, 241)
    call apply_search_controls(c, entering(9), 241, 9, p, .true., .true., .false.)
    call ok('FST4W -> fast JT9 -> Rx 700', p%nfqso .eq. 700)
    call ok('FST4W -> fast JT9 -> Tx 700', p%nftx .eq. 700)
    ! MSK144 and fast JT9 decode at the box: back from FST4W with the box
    ! already 1500 or 700 they keep it, where the waterfall kept FST4W's.
    call go(c, p, 9, 144)
    call go(c, p, 144, 241)
    call rx(c, p, 241, 241, .false., 1400)
    call go(c, p, 241, 144)
    call ok('MSK144 -> FST4W 1400 -> MSK144 -> 1500, the box', p%nfqso .eq. 1500)
    call apply_search_controls(c, entering(9), 144, 9, p, .true., .true., .false.)
    call go(c, p, 9, 241)
    call apply_search_controls(c, entering(9), 241, 9, p, .true., .true., .false.)
    call ok('fast JT9 -> FST4W -> fast JT9 -> 700, the box', p%nfqso .eq. 700)
    ! Hound without SuperFox puts back Rx without setting the box's range:
    ! after MSK144 it stays 1400..1600, so the last kept 1300 is held to 1400,
    ! as in WSJT-X (setupFT8 skips switch_mode in Hound).
    call fresh(p); p%ntol = NTOL_UNSET
    p%nfqso = 1300; p%nftx = 1300
    call start_search_controls(c, 8, p)
    call go(c, p, 8, MODE_FREQCAL)
    p%nexp_decode = ior(iand(p%nexp_decode, not(7)), 7)
    call go(c, p, MODE_FREQCAL, 8)
    call ok('FreqCal -> Hound -> Rx 1300', p%nfqso .eq. 1300)
    p%nexp_decode = iand(p%nexp_decode, not(7))
    call go(c, p, 8, 144)
    p%nexp_decode = ior(iand(p%nexp_decode, not(7)), 7)
    call go(c, p, 144, 8)
    call ok('MSK144 -> Hound -> Rx 1400, held to MSK144''s range', p%nfqso .eq. 1400)
    ! Fox takes its own Tx, 300 until set, saved there; SuperFox Fox holds 750.
    p%nexp_decode = ior(iand(p%nexp_decode, not(7)), 6)
    call go(c, p, 8, 8)
    call ok('-> Fox -> Tx 300', p%nftx .eq. 300)
    call tx(c, p, 450)
    p%nexp_decode = iand(p%nexp_decode, not(7))
    call go(c, p, 8, 8)
    call ok('Fox Tx 450 -> plain FT8 -> Tx 1300, not Fox''s', p%nftx .eq. 1300)
    p%nexp_decode = ior(iand(p%nexp_decode, not(7)), 6)
    call go(c, p, 8, 8)
    call ok('-> Fox again -> its own 450', p%nftx .eq. 450)
    p%b_superfox = .true.
    call go(c, p, 8, 8)
    call ok('-> SuperFox Fox -> Tx 750', p%nftx .eq. 750)
    call tx(c, p, 900)
    call ok('SuperFox Fox Tx 900 -> held to 750', p%nftx .eq. 750)
    p%b_superfox = .false.
    p%nexp_decode = iand(p%nexp_decode, not(7))
    ! A session that starts in Fox takes -f as Fox's Tx.
    call fresh(p); p%ntol = NTOL_UNSET
    p%nfqso = 1500; p%nftx = 600
    p%nexp_decode = ior(iand(p%nexp_decode, not(7)), 6)
    call start_search_controls(c, 8, p)
    p%nexp_decode = iand(p%nexp_decode, not(7))
    call go(c, p, 8, 8)
    p%nexp_decode = ior(iand(p%nexp_decode, not(7)), 6)
    call go(c, p, 8, 8)
    call ok('start in Fox with Tx 600 -> back in Fox -> 600', p%nftx .eq. 600)
  end subroutine frame_rx_tx

  ! A configure that chooses mode md, from was.
  subroutine go(c, p, was, md)
    type(search_controls), intent(inout) :: c
    type(params_block),    intent(inout) :: p
    integer,               intent(in)    :: was, md
    call apply_search_controls(c, entering(md), was, md, p, .true., .false., .false.)
  end subroutine go

  function entering(md) result(cfg)
    integer, intent(in) :: md
    type(configure_fields) :: cfg
    cfg%mode_set = .true.
    cfg%mode = md
  end function entering

  ! A configure with rxfreq n (and so Tx n), choosing md when entry.
  subroutine rx(c, p, was, md, entry, n)
    type(search_controls), intent(inout) :: c
    type(params_block),    intent(inout) :: p
    integer,               intent(in)    :: was, md, n
    logical,               intent(in)    :: entry
    type(configure_fields) :: cfg
    cfg%mode_set = entry
    cfg%mode = md
    cfg%rxfreq_set = .true.
    cfg%rxfreq = n
    p%nfqso = n
    p%nftx = n
    call apply_search_controls(c, cfg, was, md, p, entry, .false., .false.)
  end subroutine rx

  ! A configure in FT8 with tx_audio_offset_hz n.
  subroutine tx(c, p, n)
    type(search_controls), intent(inout) :: c
    type(params_block),    intent(inout) :: p
    integer,               intent(in)    :: n
    type(configure_fields) :: cfg
    cfg%tx_audio_offset_hz_set = .true.
    cfg%tx_audio_offset_hz = n
    p%nftx = n
    call apply_search_controls(c, cfg, 8, 8, p, .false., .false., .false.)
  end subroutine tx

  ! Frame MODE/TRPERIOD: mode routes to io_mode + nmode; default TRperiod is
  ! derived when no explicit trperiod follows; explicit trperiod -> ntr.
  subroutine frame_mode_trperiod()
    type(params_block)     :: p
    type(configure_fields) :: cfg, blank
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame MODE/TRPERIOD: mode -> io_mode/nmode + TRperiod default'
    ! mode=5 (FT4), no explicit trperiod -> io_mode=5, nmode=5, TRperiod=7.5, ntr=7
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    cfg%mode_set = .true.; cfg%mode = 5
    call apply(cfg, md, tr, p)
    call ok('mode=5 -> io_mode = 5',      md .eq. 5)
    call ok('mode=5 -> nmode = 5',        p%nmode .eq. 5)
    call ok('mode=5 -> TRperiod = 7.5',   abs(tr - 7.5d0) .lt. 1.0e-9)
    call ok('mode=5 -> ntr = 7',          p%ntr .eq. 7)
    ! explicit trperiod=30 -> ntr=30
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    cfg%mode_set = .true.; cfg%mode = 144
    cfg%trperiod_set = .true.; cfg%trperiod = 30.d0
    call apply(cfg, md, tr, p)
    call ok('trperiod=30 -> ntr = 30',    p%ntr .eq. 30)
  end subroutine frame_mode_trperiod

  ! Frame MULTITHREADED: multithreaded_ft8 passes straight through, each value
  ! applied over the opposite planted value.
  subroutine frame_multithreaded_pass_through()
    type(params_block)     :: p
    type(configure_fields) :: cfg, blank
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame MULTITHREADED: multithreaded_ft8 passes through'
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    p%lmultift8 = .false.
    cfg%multithreaded_ft8_set = .true.; cfg%multithreaded_ft8 = .true.
    call apply(cfg, md, tr, p)
    call ok('multithreaded_ft8=true -> lmultift8 true', logical(p%lmultift8))
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    p%lmultift8 = .true.
    cfg%multithreaded_ft8_set = .true.; cfg%multithreaded_ft8 = .false.
    call apply(cfg, md, tr, p)
    call ok('multithreaded_ft8=false -> lmultift8 false', .not. logical(p%lmultift8))
  end subroutine frame_multithreaded_pass_through

  ! Frame ENGINE-LIMITS: the apply passes qso_progress_state, ft8_threads and
  ! n_trials through as sent; the stream declines a configure whose values the
  ! decoder would refuse (streaming_io.f90). Sentinels (nQSOProgress 2, nmt 2,
  ! nranera 4) differ from every asserted value, so a guard that drops a value
  ! fails here, and a dropped apply fails the in-range checks.
  subroutine frame_engine_limits()
    type(params_block)     :: p
    type(configure_fields) :: cfg, blank
    integer :: md, i
    real(8) :: tr
    integer, parameter :: qso(4) = [99, -1, 5, 0], threads(4) = [25, -1, 24, 0]
    integer, parameter :: nranera(3) = [13, 12, 0]
    character(len=8) :: v
    write(*,'(a)') 'Frame ENGINE-LIMITS: qso_progress_state/ft8_threads/n_trials pass through'
    md = 8; tr = 15.d0
    do i = 1, size(qso)
       call fresh(p); cfg = blank; p%nQSOProgress = 2
       cfg%qso_progress_state_set = .true.; cfg%qso_progress_state = qso(i)
       call apply(cfg, md, tr, p)
       write(v, '(i0)') qso(i)
       call ok('qso_progress_state=' // trim(v) // ' applied as sent', p%nQSOProgress .eq. qso(i))
    end do
    do i = 1, size(threads)
       call fresh(p); cfg = blank; p%nmt = 2
       cfg%ft8_threads_set = .true.; cfg%ft8_threads = threads(i)
       call apply(cfg, md, tr, p)
       write(v, '(i0)') threads(i)
       call ok('ft8_threads=' // trim(v) // ' applied as sent', p%nmt .eq. threads(i))
    end do
    do i = 1, size(nranera)
       call fresh(p); cfg = blank; p%nranera = 4
       cfg%n_trials_set = .true.; cfg%n_trials_nranera = nranera(i)
       call apply(cfg, md, tr, p)
       write(v, '(i0)') nranera(i)
       call ok('n_trials nranera=' // trim(v) // ' applied as sent', p%nranera .eq. nranera(i))
    end do
  end subroutine frame_engine_limits

  ! Frame RANGE-KEYS: engine_range_key names the configure key behind a field
  ! the engine refuses: the key the frame set, else the carried value's key.
  subroutine frame_range_keys()
    type(configure_fields) :: cfg, blank
    write(*,'(a)') 'Frame RANGE-KEYS: refused engine fields name their configure keys'
    cfg = blank
    call key_is('search_low_hz', cfg, 'nfa')
    call key_is('search_high_hz', cfg, 'nfb')
    call key_is('search_range', cfg, 'nfa')
    call key_is('receive_frequency_hz', cfg, 'rxfreq')
    call key_is('tolerance_hz', cfg, 'ntol')
    call key_is('depth', cfg, 'depth')
    call key_is('minimum_width', cfg, 'min_width')
    call key_is('submode', cfg, 'submode')
    call key_is('trials', cfg, 'n_trials')
    call key_is('qso_progress', cfg, 'qso_progress_state')
    call key_is('threads', cfg, 'ft8_threads')
    call key_is('period_seconds', cfg, 'trperiod')
    call key_is('eme_delay_seconds', cfg, 'eme_delay_seconds')
    call key_is('blanker_mode', cfg, 'noise_blanker_level')
    call key_is('blanker_percent', cfg, 'noise_blanker_level')
    call key_is('max_drift', cfg, 'max_drift_hz')
    call key_is('utc', cfg, 'nutc')
    call key_is('contest', cfg, 'contest_type')
    call key_is('aggressiveness', cfg, 'aggressive')
    cfg = blank; cfg%nfb_set = .true.
    call key_is('search_range', cfg, 'nfb')
    cfg%nfa_set = .true.
    call key_is('search_range', cfg, 'nfa')
    ! The parse packs any unpacked depth key into ndepth_packed.
    cfg = blank; cfg%ndepth_packed_set = .true.
    call key_is('depth', cfg, 'depth_level')
    cfg = blank; cfg%submode_set = .true.
    call key_is('minimum_width', cfg, 'min_width')
    cfg = blank; cfg%decode_after_eme_delay_set = .true.
    call key_is('eme_delay_seconds', cfg, 'decode_after_eme_delay')
    cfg%eme_delay_seconds_set = .true.
    call key_is('eme_delay_seconds', cfg, 'eme_delay_seconds')
    cfg = blank; cfg%utc_set = .true.
    call key_is('utc', cfg, 'utc')
    ! carried: the named value came from an earlier configure.
    cfg = blank
    call carried_is('receive_frequency_hz', cfg, .true.)
    cfg%rxfreq_set = .true.
    call carried_is('receive_frequency_hz', cfg, .false.)
    cfg = blank; cfg%submode_set = .true.
    call carried_is('minimum_width', cfg, .true.)
    cfg = blank; cfg%mode_set = .true.
    call carried_is('period_seconds', cfg, .false.)
    call carried_is('eme_delay_seconds', cfg, .false.)
    call carried_is('trials', cfg, .true.)
  end subroutine frame_range_keys

  subroutine carried_is(field, cfg, expected)
    character(len=*),       intent(in) :: field
    type(configure_fields), intent(in) :: cfg
    logical,                intent(in) :: expected
    character(len=32) :: key
    logical :: carried
    key = engine_range_key(field, cfg, carried)
    call ok(field // ' carried ' // merge('T', 'F', expected), carried .eqv. expected)
  end subroutine carried_is

  subroutine key_is(field, cfg, expected)
    character(len=*),       intent(in) :: field, expected
    type(configure_fields), intent(in) :: cfg
    call ok(field // ' -> ' // expected, trim(engine_range_key(field, cfg)) .eq. expected)
  end subroutine key_is

  ! Frame UTC-PRECEDENCE (Phase 8): utc (ISO) is a SECOND route to
  ! nutc alongside the legacy int nutc. apply_configure_fields applies nutc THEN
  ! utc, so utc WINS when a frame carries both. Three cases, each sentinel-planted
  ! (nutc=999999, != every asserted value) so a dropped apply fails:
  !   (a) utc alone        -> nutc = utc_nutc (133430)
  !   (b) legacy nutc alone -> nutc = nutc (120000)
  !   (c) BOTH             -> utc wins (133430, NOT 120000)
  ! Case (c) is the precedence killer: a swap of the two apply lines (utc before
  ! nutc) would leave 120000 and fail it.
  subroutine frame_utc_precedence()
    type(params_block)     :: p
    type(configure_fields) :: cfg, blank
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame UTC-PRECEDENCE: utc routes to nutc and wins over a co-present nutc'
    md = 8; tr = 15.d0
    ! (a) utc alone -> nutc = utc_nutc.
    call fresh(p); cfg = blank; p%nutc = 999999
    cfg%utc_set = .true.; cfg%utc_nutc = 133430
    call apply(cfg, md, tr, p)
    call ok('utc alone -> nutc = 133430', p%nutc .eq. 133430)
    ! (b) legacy nutc alone -> nutc = nutc.
    call fresh(p); cfg = blank; p%nutc = 999999
    cfg%nutc_set = .true.; cfg%nutc = 120000
    call apply(cfg, md, tr, p)
    call ok('nutc alone -> nutc = 120000', p%nutc .eq. 120000)
    ! (c) BOTH -> utc wins (120000 overwritten by 133430).
    call fresh(p); cfg = blank; p%nutc = 999999
    cfg%nutc_set = .true.; cfg%nutc = 120000
    cfg%utc_set  = .true.; cfg%utc_nutc = 133430
    call apply(cfg, md, tr, p)
    call ok('nutc+utc -> utc WINS (nutc = 133430, not 120000)', p%nutc .eq. 133430)
  end subroutine frame_utc_precedence

  subroutine frame_fst4_utc()
    type(configure_fields) :: cfg, blank
    type(params_block) :: p
    integer :: md, mode
    real(8) :: tr
    character(len=16) :: label

    do mode = 240, 242
       write(label,'(a,i0)') 'mode ', mode
       call fresh(p)
       cfg = blank
       cfg%mode_set = .true.; cfg%mode = mode
       cfg%trperiod_set = .true.; cfg%trperiod = 120.d0
       cfg%nutc_set = .true.; cfg%nutc = 120000
       md = 8; tr = 15.d0
       call apply(cfg, md, tr, p)
       call ok(trim(label)//': full numeric noon stays HHMMSS', p%nutc .eq. 120000)

       cfg%mode_set = .false.; cfg%trperiod_set = .false.
       cfg%nutc = 1234
       call apply(cfg, md, tr, p)
       call ok(trim(label)//': legacy numeric HHMM becomes HHMMSS', p%nutc .eq. 123400)

       cfg%nutc = 15
       call apply(cfg, md, tr, p)
       call ok(trim(label)//': legacy numeric 0015 means 00:15:00', p%nutc .eq. 1500)

       cfg%nutc = 2359
       cfg%utc_set = .true.; cfg%utc_nutc = 1500
       call apply(cfg, md, tr, p)
       call ok(trim(label)//': ISO 00:15:00 overrides numeric HHMM', p%nutc .eq. 1500)

       cfg%nutc_set = .false.; cfg%utc_nutc = 30
       call apply(cfg, md, tr, p)
       call ok(trim(label)//': ISO 00:00:30 retains seconds', p%nutc .eq. 30)
    end do
  end subroutine frame_fst4_utc

  ! Frame PHASE9-PACK (Phase 9): the two derived packed ints route
  ! FLAT to params%ndepth / params%nexp_decode. The bit-PACKING itself (each wire
  ! key -> its bit, the contest collapse, the masking/clamp) is asserted by the
  ! parse-unit (run-phase9-parse-unit.sh); this guards the cfg -> params ROUTING.
  ! ndepth_packed and the legacy depth are mutually exclusive on an accepted frame
  ! (co-present is declined at parse), so they are tested separately: frame_flat
  ! covers depth -> ndepth, this covers ndepth_packed -> ndepth. Sentinel-opposite:
  ! plant ndepth/nexp_decode = 99 (!= the asserted values) so a
  ! dropped or mis-routed apply leaves the sentinel and fails. The asserted values
  ! are realistic packs: ndepth 178 = 2|0x10|0x20|0x80; nexp_decode 1382 =
  ! 6|0x20|0x40|(5<<8).
  subroutine frame_phase9_pack()
    type(params_block)     :: p
    type(configure_fields) :: cfg, blank
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame PHASE9-PACK: ndepth_packed/nexp_decode_packed -> their slots'
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    p%ndepth      = 99        ! sentinel != 178
    p%nexp_decode = 99        ! sentinel != 1382
    cfg%ndepth_packed_set = .true.;      cfg%ndepth_packed = 178
    cfg%nexp_decode_packed_set = .true.; cfg%nexp_decode_packed = 1382
    call apply(cfg, md, tr, p)
    call ok('ndepth_packed -> ndepth = 178 (not sentinel 99)',        p%ndepth      .eq. 178)
    call ok('nexp_decode_packed -> nexp_decode = 1382 (not sentinel 99)', p%nexp_decode .eq. 1382)
  end subroutine frame_phase9_pack

  ! What cancels the GUI's multithreaded FT8 backpressure, field by field: a
  ! setting at once, an identity-only field when the waiting final is taken up.
  subroutine frame_cancel_fields()
    type(params_block) :: was, now
    call fresh(was)
    now = was
    call ok('cancel: an unchanged copy changes no setting', .not. ft8_settings_change(was, now))
    call ok('cancel: an unchanged copy changes no identity', .not. ft8_identity_change(was, now))
    now = was; now%ndepth = was%ndepth + 1
    call setting('depth', was, now)
    ! The rest of ndepth (averaging 16, deep search 32, automatic clear 128,
    ! 64) is checked at take-up, as WSJT-X's pending identity.
    now = was; now%ndepth = ieor(was%ndepth, 16)
    call identity('use_averaging', was, now)
    now = was; now%ndepth = ieor(was%ndepth, 32)
    call identity('deep_ap_search', was, now)
    now = was; now%ndepth = ieor(was%ndepth, 128)
    call identity('q65_auto_clear_average', was, now)
    ! Bit 0x100 of ndepth is the JT9 window request, not a depth.
    now = was; now%ndepth = ior(was%ndepth, 256)
    call ok('cancel: setting the JT9 window bit alone is not an FT8 setting', &
         .not. ft8_settings_change(was, now))
    call ok('cancel: clearing the JT9 window bit alone is not an FT8 setting', &
         .not. ft8_settings_change(now, was))
    now = was; now%ndepth = ior(was%ndepth + 1, 256)
    call setting('a depth change that also sets the window bit', was, now)
    was%ndepth = ior(was%ndepth, 256)
    now = was; now%ndepth = was%ndepth + 1
    call setting('a depth change with the window bit already set', was, now)
    call fresh(was)
    now = was; now%lmultift8 = .not. was%lmultift8
    call setting('multithreaded_ft8', was, now)
    now = was; now%nmt = was%nmt + 1
    call setting('ft8_threads', was, now)
    now = was; now%ndecoderstart = was%ndecoderstart + 1
    call setting('ft8_decoder_start', was, now)
    now = was; now%nft8cycles = was%nft8cycles + 1
    call setting('ft8_cycles', was, now)
    now = was; now%nft8rxfsens = was%nft8rxfsens + 1
    call setting('ft8_rxf_sensitivity', was, now)
    now = was; now%lft8lowth = .not. was%lft8lowth
    call setting('ft8_low_threshold', was, now)
    now = was; now%lft8subpass = .not. was%lft8subpass
    call setting('ft8_subpass', was, now)
    now = was; now%lwidedxcsearch = .not. was%lwidedxcsearch
    call setting('wide_dxc_search', was, now)
    now = was; now%lhideft8dupes = .not. was%lhideft8dupes
    call setting('hide_ft8_duplicates', was, now)
    now = was; now%lft8apon = .not. was%lft8apon
    call setting('ft8_ap_on', was, now)
    now = was; now%mycall(12) = merge('Y', 'X', was%mycall(12) .eq. 'X')
    call setting('mycall', was, now)
    now = was; now%mygrid(6) = merge('Y', 'X', was%mygrid(6) .eq. 'X')
    call setting('mygrid', was, now)
    now = was; now%b_superfox = .not. was%b_superfox
    call setting('superfox_mode', was, now)
    now = was; now%lhound = .not. was%lhound
    call setting('hound_mode', was, now)
    now = was; now%nexp_decode = ieor(was%nexp_decode, 7)
    call setting('contest_type', was, now)
    now = was; now%nexp_decode = ieor(was%nexp_decode, 32)
    call setting('single_decode', was, now)
    now = was; now%nexp_decode = ieor(was%nexp_decode, 64)
    call setting('vhf_features', was, now)
    now = was; now%nranera = was%nranera + 1
    call setting('n_trials', was, now)
    now = was; now%naggressive = was%naggressive + 1
    call setting('aggressive', was, now)
    now = was; now%n2pass = 3 - was%n2pass
    call setting('n_2pass', was, now)
    now = was; now%nexp_decode = ieor(was%nexp_decode, 128)
    call setting('q65_pileup', was, now)
    now = was; now%emedelay = was%emedelay + 1
    call setting('decode_after_eme_delay', was, now)
    now = was; now%nexp_decode = was%nexp_decode + 256
    call ok('cancel: noise_blanker_level is no FT8 setting', .not. ft8_settings_change(was, now))
    now = was; now%nfa = was%nfa + 1
    call identity('nfa', was, now)
    now = was; now%nfb = was%nfb + 1
    call identity('nfb', was, now)
    now = was; now%nsubmode = was%nsubmode + 1
    call identity('submode', was, now)
  end subroutine frame_cancel_fields

  ! MSK144's options and calls: each key to its own setting, kept until a
  ! frame sets it.
  subroutine frame_msk144()
    type(configure_fields)   :: cfg, blank
    type(control_type_error) :: terr
    type(msk144_settings)    :: s
    integer :: action
    write(*,'(a)') 'Frame MSK144: options and calls -> the stream''s MSK144 settings'
    call ok('msk144: off, no equalization and no calls by default',          &
         .not. (s%short_messages .or. s%swl .or. s%train) .and.               &
         all(s%phase_eq .eq. 0.d0) .and. s%mycall .eq. ' ' .and. s%hiscall .eq. ' ')
    cfg = blank
    cfg%short_messages_set = .true.; cfg%short_messages = .true.
    call apply_msk144_fields(cfg, s)
    call ok('msk144: short_messages -> short_messages only',                 &
         s%short_messages .and. .not. s%swl .and. .not. s%train)
    cfg = blank
    cfg%swl_set = .true.; cfg%swl = .true.
    call apply_msk144_fields(cfg, s)
    call ok('msk144: swl -> swl, short_messages kept', s%swl .and. s%short_messages .and. .not. s%train)
    cfg = blank
    cfg%train_set = .true.; cfg%train = .true.
    cfg%short_messages_set = .true.; cfg%short_messages = .false.
    call apply_msk144_fields(cfg, s)
    call ok('msk144: train -> train, short_messages false', s%train .and. s%swl .and. .not. s%short_messages)
    cfg = blank
    cfg%phase_eq_set = .true.; cfg%phase_eq = [1.d0, 2.d0, 3.d0, 4.d0, 5.d0]
    call apply_msk144_fields(cfg, s)
    call ok('msk144: phase_eq -> phase_eq', all(s%phase_eq .eq. [1.d0, 2.d0, 3.d0, 4.d0, 5.d0]))
    cfg = blank
    cfg%mycall_set = .true.; cfg%mycall = 'KJ5HST'
    cfg%his_call_set = .true.; cfg%his_call = 'W1AW/7'
    call apply_msk144_fields(cfg, s)
    call ok('msk144: mycall and his_call -> the decoder''s calls',            &
         s%mycall .eq. 'KJ5HST' .and. s%hiscall .eq. 'W1AW/7')
    call apply_msk144_fields(blank, s)
    call ok('msk144: a frame without them keeps every one',                   &
         s%train .and. s%swl .and. .not. s%short_messages .and.               &
         all(s%phase_eq .eq. [1.d0, 2.d0, 3.d0, 4.d0, 5.d0]) .and.             &
         s%mycall .eq. 'KJ5HST' .and. s%hiscall .eq. 'W1AW/7')
    call parse_control_frame('{"t":"configure","phase_eq":[0,0,5,0]}', action, cfg, terr)
    call apply_msk144_fields(cfg, s)
    call ok('msk144: a refused phase_eq carries none',                        &
         cfg%phase_eq_out_of_range .and. all(s%phase_eq .eq. [1.d0, 2.d0, 3.d0, 4.d0, 5.d0]))
  end subroutine frame_msk144

  subroutine setting(key, was, now)
    character(len=*),   intent(in) :: key
    type(params_block), intent(in) :: was, now
    call ok('cancel: ' // key // ' is a setting', ft8_settings_change(was, now))
  end subroutine setting

  subroutine identity(key, was, now)
    character(len=*),   intent(in) :: key
    type(params_block), intent(in) :: was, now
    call ok('cancel: ' // key // ' is identity only',                         &
         ft8_identity_change(was, now) .and. .not. ft8_settings_change(was, now))
  end subroutine identity

end program apply_routing_test
