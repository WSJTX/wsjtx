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
! per-mode-policy (D1), nfa/nfb baseline restore, and the ntol per-mode cap/restore.
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
! Compiled + run by test/run-apply-routing-unit.sh against lib/streaming_control.f90
! + lib/jt9_params_init.f90 + lib/streaming_apply.f90, in an ISOLATED module dir.
program apply_routing_test
  use streaming_control, only: configure_fields
  use jt9_params_init,   only: cli_args_t, init_default_params,             &
       init_streaming_extra_fields
  use streaming_apply,   only: apply_configure_fields, params_block
  implicit none
  integer :: nfail
  integer, parameter :: BNFA = 111, BNFB = 222   ! baseline nfa/nfb
  nfail = 0

  call frame_flat()
  call frame_rxfreq()
  call frame_decouple()
  call frame_emedelay_survive()
  call frame_emedelay_ft8()
  call frame_emedelay_q65()
  call frame_nfa_nfb_restore()
  call frame_ntol_policy()
  call frame_mode_trperiod()
  call frame_multithreaded_guard()
  call frame_qso_guard()
  call frame_utc_precedence()
  call frame_phase9_pack()

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
    ! default .true. (init_streaming_extra_fields), and mybcall/hisbcall are
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
    ! multithreaded_ft8 is NOT set here (it is apply-ignored; see frame_multithreaded_guard).
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
    cfg%robust_mode_set = .true.;      cfg%robust_mode = .true.
    cfg%nagain_flag_set = .true.;      cfg%nagain_flag = .true.
    cfg%tx_mode_set = .true.;          cfg%tx_mode = 9
    cfg%clear_average_set = .true.;    cfg%clear_average = .true.
    cfg%nutc_set = .true.;             cfg%nutc = 133430
    cfg%ntol_set = .true.;             cfg%ntol = 250
    cfg%nfa_set = .true.;              cfg%nfa = 180
    cfg%nfb_set = .true.;              cfg%nfb = 3900
    ! Phase 6 (23 keys; multithreaded_ft8 excluded — apply-ignored). Bools
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
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
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
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
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
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
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
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
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
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
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
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('emedelay = 2.5 (Q65 60s policy)', abs(p%emedelay - 2.5) .lt. 1.0e-6)
  end subroutine frame_emedelay_q65

  ! Frame NFA/NFB restore: on a mode change with NO explicit nfa/nfb, the
  ! session baseline is restored; an explicit nfa is honored instead.
  subroutine frame_nfa_nfb_restore()
    type(params_block)     :: p
    type(configure_fields) :: cfg, blank
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame NFA/NFB: mode change restores baseline unless explicit'
    ! (a) no explicit nfa/nfb -> baseline restored
    call fresh(p)
    md = 8; tr = 15.d0
    cfg = blank
    cfg%mode_set = .true.; cfg%mode = 8
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('no nfa/nfb + mode change -> nfa = baseline 111', p%nfa .eq. BNFA)
    call ok('no nfa/nfb + mode change -> nfb = baseline 222', p%nfb .eq. BNFB)
    ! (b) explicit nfa wins over baseline
    call fresh(p)
    md = 8; tr = 15.d0
    cfg = blank
    cfg%mode_set = .true.; cfg%mode = 8
    cfg%nfa_set = .true.;  cfg%nfa = 900
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('explicit nfa=900 + mode change -> nfa = 900 (not baseline)', p%nfa .eq. 900)
  end subroutine frame_nfa_nfb_restore

  ! Frame NTOL policy: per-mode cap (consumer set) vs restore (consumer unset),
  ! on a mode/trperiod change.
  subroutine frame_ntol_policy()
    type(params_block)     :: p
    type(configure_fields) :: cfg, blank
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame NTOL: per-mode cap/restore on mode change'
    ! FT8, consumer unset -> restore default 1000. Sentinel 7777 first (review
    ! HIGH: the mode-8 baseline is already 1000, so without a sentinel a deleted
    ! restore line would pass vacuously).
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    p%ntol = 7777
    cfg%mode_set = .true.; cfg%mode = 8
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('FT8 + no ntol -> ntol = 1000 (restored from 7777)', p%ntol .eq. 1000)
    ! FT8, consumer 5000 -> min(5000,1000) = 1000
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    cfg%mode_set = .true.; cfg%mode = 8
    cfg%ntol_set = .true.; cfg%ntol = 5000
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('FT8 + ntol 5000 -> ntol = 1000 (capped)', p%ntol .eq. 1000)
    ! FT8, consumer 200 -> 200 (under cap)
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    cfg%mode_set = .true.; cfg%mode = 8
    cfg%ntol_set = .true.; cfg%ntol = 200
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('FT8 + ntol 200 -> ntol = 200', p%ntol .eq. 200)
    ! Q65, consumer 999 -> hard 10
    call fresh(p); md = 8; tr = 60.d0; cfg = blank
    cfg%mode_set = .true.; cfg%mode = 66
    cfg%trperiod_set = .true.; cfg%trperiod = 60.d0
    cfg%ntol_set = .true.; cfg%ntol = 999
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('Q65 + ntol 999 -> ntol = 10 (hard)', p%ntol .eq. 10)
    ! JT65+JT9 (mode 74), consumer unset -> hard 20
    call fresh(p); md = 8; tr = 60.d0; cfg = blank
    cfg%mode_set = .true.; cfg%mode = 65 + 9
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('JT65+JT9 + no ntol -> ntol = 20 (hard)', p%ntol .eq. 20)
    ! FST4 (241), consumer 50 -> min(50,100) = 50
    call fresh(p); md = 8; tr = 60.d0; cfg = blank
    cfg%mode_set = .true.; cfg%mode = 241
    cfg%ntol_set = .true.; cfg%ntol = 50
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('FST4 + ntol 50 -> ntol = 50 (under cap)', p%ntol .eq. 50)
  end subroutine frame_ntol_policy

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
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('mode=5 -> io_mode = 5',      md .eq. 5)
    call ok('mode=5 -> nmode = 5',        p%nmode .eq. 5)
    call ok('mode=5 -> TRperiod = 7.5',   abs(tr - 7.5d0) .lt. 1.0e-9)
    call ok('mode=5 -> ntr = 7',          p%ntr .eq. 7)
    ! explicit trperiod=30 -> ntr=30
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    cfg%mode_set = .true.; cfg%mode = 144
    cfg%trperiod_set = .true.; cfg%trperiod = 30.d0
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('trperiod=30 -> ntr = 30',    p%ntr .eq. 30)
  end subroutine frame_mode_trperiod

  ! Frame MULTITHREADED-GUARD: multithreaded_ft8 is apply-IGNORED when non-false
  ! (a true would re-enable the broken multithread branch
  ! decoder.f90:192 -> 0 FT8 decodes). (a) a cfg true must NOT set
  ! lmultift8 true (a straight pass-through regression would, and this catches it);
  ! (b) a cfg false IS honored.
  subroutine frame_multithreaded_guard()
    type(params_block)     :: p
    type(configure_fields) :: cfg, blank
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame MULTITHREADED-GUARD: multithreaded_ft8 ignore-non-false'
    ! (a) true is IGNORED: plant lmultift8=.false., apply true, assert STILL false.
    ! (A pass-through regression would set it true and fail this.)
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    p%lmultift8 = .false.
    cfg%multithreaded_ft8_set = .true.; cfg%multithreaded_ft8 = .true.
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('multithreaded_ft8=true IGNORED -> lmultift8 stays false', .not. logical(p%lmultift8))
    ! (b) false is HONORED: plant lmultift8=.true. (sentinel), apply false -> false.
    call fresh(p); md = 8; tr = 15.d0; cfg = blank
    p%lmultift8 = .true.
    cfg%multithreaded_ft8_set = .true.; cfg%multithreaded_ft8 = .false.
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('multithreaded_ft8=false honored -> lmultift8 false', .not. logical(p%lmultift8))
  end subroutine frame_multithreaded_guard

  ! Frame QSO-GUARD: qso_progress_state apply guard. nQSOProgress
  ! reaches the single-pass FT8 decoder where ft8b.f90:274/299 index
  ! nappasses(0:5)/naptypes(0:5,4) -> an out-of-range value crashes the decoder
  ! (verified: qso_progress_state=99 crashes jt9 --stream). The apply IGNORES an
  ! out-of-range value (valid [0,5] pass through unchanged) -- like the
  ! multithreaded_ft8 guard. Negative-controlled: plant a sentinel (2) != asserted,
  ! so a straight pass-through regression (which would write 99 / -1) fails the
  ! ignore asserts, and a dropped apply fails the in-range asserts.
  subroutine frame_qso_guard()
    type(params_block)     :: p
    type(configure_fields) :: cfg, blank
    integer :: md
    real(8) :: tr
    write(*,'(a)') 'Frame QSO-GUARD: qso_progress_state in-range applied / out-of-range ignored'
    md = 8; tr = 15.d0
    ! out-of-range HIGH (99) IGNORED: plant 2, apply 99, assert STILL 2.
    call fresh(p); cfg = blank; p%nQSOProgress = 2
    cfg%qso_progress_state_set = .true.; cfg%qso_progress_state = 99
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('qso_progress_state=99 (>5) IGNORED -> nQSOProgress stays 2', p%nQSOProgress .eq. 2)
    ! out-of-range LOW (-1) IGNORED: plant 2, apply -1, assert STILL 2.
    call fresh(p); cfg = blank; p%nQSOProgress = 2
    cfg%qso_progress_state_set = .true.; cfg%qso_progress_state = -1
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('qso_progress_state=-1 (<0) IGNORED -> nQSOProgress stays 2', p%nQSOProgress .eq. 2)
    ! in-range boundary 5 APPLIED (overwrites sentinel 2).
    call fresh(p); cfg = blank; p%nQSOProgress = 2
    cfg%qso_progress_state_set = .true.; cfg%qso_progress_state = 5
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('qso_progress_state=5 (in range) APPLIED -> nQSOProgress = 5', p%nQSOProgress .eq. 5)
    ! in-range boundary 0 APPLIED (overwrites sentinel 2 -> proves it is applied,
    ! not merely left at a default that happens to equal 0).
    call fresh(p); cfg = blank; p%nQSOProgress = 2
    cfg%qso_progress_state_set = .true.; cfg%qso_progress_state = 0
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('qso_progress_state=0 (in range) APPLIED -> nQSOProgress = 0', p%nQSOProgress .eq. 0)
  end subroutine frame_qso_guard

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
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('utc alone -> nutc = 133430', p%nutc .eq. 133430)
    ! (b) legacy nutc alone -> nutc = nutc.
    call fresh(p); cfg = blank; p%nutc = 999999
    cfg%nutc_set = .true.; cfg%nutc = 120000
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('nutc alone -> nutc = 120000', p%nutc .eq. 120000)
    ! (c) BOTH -> utc wins (120000 overwritten by 133430).
    call fresh(p); cfg = blank; p%nutc = 999999
    cfg%nutc_set = .true.; cfg%nutc = 120000
    cfg%utc_set  = .true.; cfg%utc_nutc = 133430
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('nutc+utc -> utc WINS (nutc = 133430, not 120000)', p%nutc .eq. 133430)
  end subroutine frame_utc_precedence

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
    call apply_configure_fields(cfg, md, tr, p, BNFA, BNFB)
    call ok('ndepth_packed -> ndepth = 178 (not sentinel 99)',        p%ndepth      .eq. 178)
    call ok('nexp_decode_packed -> nexp_decode = 1382 (not sentinel 99)', p%nexp_decode .eq. 1382)
  end subroutine frame_phase9_pack

end program apply_routing_test
