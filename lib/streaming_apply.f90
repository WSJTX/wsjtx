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
! The order-sensitive logic (rxfreq decouple, apply_per_mode_policy call +
! emedelay-before-policy) keeps its original position. ntol, nfa and nfb
! follow WSJT-X's controls (search_controls).
!
! Dependencies (all acyclic — neither dependee uses this module):
!   streaming_control  -> configure_fields  (the source struct)
!   jt9_params_init    -> apply_per_mode_policy
!   include jt9com.f90 -> params_block (+ iso_c_binding / constants.f90)

module streaming_apply
  use streaming_control, only: configure_fields, NPTS_C0_ARRAY_MIN,         &
       NPTS_C0_ARRAY_MAX, MODE_JTTY, MODE_ECHO, MODE_FREQCAL, MODE_WSPR
  use jt9_params_init,   only: apply_per_mode_policy, standard_call
  use msk144_stream,     only: msk144_settings

  include 'jt9com.f90'

  private
  public :: apply_configure_fields, engine_range_key
  public :: ft8_settings_change, settings_dialog_change, ft8_identity_change
  public :: apply_msk144_fields
  public :: start_search_controls, apply_search_controls

  ! WSJT-X's frequency tolerance, search range and Rx and Tx Freq controls,
  ! which keep their values across modes: its F Tol box (ftol), the F Tol
  ! each mode puts back on entry (saved, 50 until set; saved_slot), FST4W's
  ! and JTTY's own F Tol boxes (100 and 20 until set), the waterfall's range
  ! (wide_nfa, wide_nfb), FST4's F Low and F High (600 and 1400 until set)
  ! and the Rx and Tx Freq some modes put back. JT65+JT9, which WSJT-X no
  ! longer has, keeps the codec's own value.
  type, public :: search_controls
     integer :: ftol = 50
     integer :: saved(6) = 50
     integer :: fst4w_ftol = 100
     integer :: jtty_ftol = 20
     integer :: jt65jt9_ftol = 20
     integer :: wide_nfa = 200, wide_nfb = 4000
     integer :: fst4_nfa = 600, fst4_nfb = 1400
     ! The Rx box and its range, the waterfall's Rx at which WSJT-X decodes,
     ! the Tx box as the last configure left it, the last Rx and Tx set in a
     ! mode that keeps them (RxFreq_old, TxFreq_old), FST4W's own Rx
     ! (sbFST4W_RxFreq) and Fox's own Tx (m_TxFreqFox).
     integer :: box = 1500, wf = 1500, box_lo = 200, box_hi = 5000
     integer :: tx = 1500, rx_old = 1500, tx_old = 1500
     integer :: fst4w_rx = 1500, fox_tx = 300
  end type search_controls
  ! params%ntol of a session started without -F (lib/jt9.f90).
  integer, parameter, public :: NTOL_UNSET = -999999
  ! Re-export the include'd struct so a standalone unit driver can declare a
  ! params_block to assert apply routing against (test/apply_routing_test.f90)
  ! without itself including jt9com.f90. Same bind(C) type as everywhere else
  ! (F2018 §7.5.2.4 same-type rule), so it argument-associates with the
  ! jt9_params_init init routines exactly as streaming_io's include'd one does.
  public :: params_block

contains

  ! Apply a configure frame's set fields to a params_block + the caller's
  ! mode/TRperiod state; its ntol, nfa and nfb go through
  ! apply_search_controls.
  subroutine apply_configure_fields(in_cfg, io_mode, io_TRperiod, params)
    type(configure_fields), intent(in)    :: in_cfg
    integer,                intent(inout) :: io_mode
    real(8),                intent(inout) :: io_TRperiod
    type(params_block),     intent(inout) :: params
    if (in_cfg%mode_set) then
       io_mode      = in_cfg%mode
       params%nmode = in_cfg%mode
       ! Default TRperiod for the new mode if no explicit trperiod follows.
       if (.not. in_cfg%trperiod_set) then
          select case (in_cfg%mode)
          case (8);   io_TRperiod = 15.d0
          case (5);   io_TRperiod = 7.5d0
          case (144); io_TRperiod = 30.d0
          case (MODE_ECHO); io_TRperiod = 3.d0
          ! The GUI's WSPR is WSPR-2 only: m_TRperiod=120 (on_actionWSPR_triggered).
          case (MODE_WSPR); io_TRperiod = 120.d0
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
    ! Choosing FreqCal sets the GUI's Rx Freq to 1500 (setupFreqCal): the
    ! search controls set it, with the other Rx rules.
    if (in_cfg%rxfreq_set) then
       params%nfqso = in_cfg%rxfreq
       params%nftx  = in_cfg%rxfreq
    end if
    if (in_cfg%mycall_set) params%mycall = transfer(in_cfg%mycall, params%mycall)
    if (in_cfg%mygrid_set) params%mygrid = transfer(in_cfg%mygrid, params%mygrid)

    ! ===== operator identity fields ====================
    ! char12/char6 via transfer() like mycall/mygrid (a width mismatch
    ! silently truncates/pads at the bind(C) boundary, so the configure_fields
    ! widths match the struct). Bools assign directly (logical -> logical(c_bool)).
    if (in_cfg%his_call_set)   params%hiscall  = transfer(in_cfg%his_call,  params%hiscall)
    if (in_cfg%his_grid_set)   params%hisgrid  = transfer(in_cfg%his_grid,  params%hisgrid)
    if (in_cfg%my_b_call_set)  params%mybcall  = transfer(in_cfg%my_b_call, params%mybcall)
    if (in_cfg%his_b_call_set) params%hisbcall = transfer(in_cfg%his_b_call, params%hisbcall)
    if (in_cfg%my_call_standard_set)  params%lmycallstd  = in_cfg%my_call_standard
    if (in_cfg%his_call_standard_set) params%lhiscallstd = in_cfg%his_call_standard
    ! A call sent without its flag takes the flag from the call, as at the start.
    if (in_cfg%mycall_set .and. .not. in_cfg%my_call_standard_set) &
         params%lmycallstd = standard_call(in_cfg%mycall)
    if (in_cfg%his_call_set .and. .not. in_cfg%his_call_standard_set) &
         params%lhiscallstd = standard_call(in_cfg%his_call)

    ! frequency-window ints. tx_audio_offset_hz -> nftx applies AFTER
    ! the rxfreq block above so it DECOUPLES the tx side: rxfreq set both
    ! nfqso+nftx, and a co-present tx_audio_offset_hz then overrides nftx only.
    ! jt65_jt9_split_hz -> nfsplit (honored as written; a bare
    ! mid-stream mode switch does not auto-replay init-time fsplit). max_drift_hz
    ! -> max_drift (Q65 drift search; inert for FT8/JT9, so no decode-output
    ! fixture — verified at parse-unit + no-regression, like the other fields).
    if (in_cfg%tx_audio_offset_hz_set) params%nftx      = in_cfg%tx_audio_offset_hz
    if (in_cfg%jt65_jt9_split_hz_set)  params%nfsplit   = in_cfg%jt65_jt9_split_hz
    if (in_cfg%max_drift_hz_set)       params%max_drift = in_cfg%max_drift_hz

    ! ===== decoder-tuning basic fields =================
    ! Flat 1:1. dt_tolerance_seconds/eme_delay_seconds narrow real(8) ->
    ! real(c_float) via the target's kind() (document the narrowing).
    ! eme_delay_seconds is applied before the apply_per_mode_policy call below,
    ! which a frame that also sets mode/trperiod re-runs (emedelay 2.5 for Q65
    ! at 60 s, else 0). The stream host sets emedelay as the GUI does (2.5 with
    ! decode_after_eme_delay, else 0): 0 at start, and again on a frame that
    ! sets the mode, the TR period or that key, where an eme_delay_seconds in
    ! the frame wins (streaming_io.f90). The host also sets kin and nzhsym before
    ! each decode, so kin_samples/nzhsym_per_period do not reach the decoder.
    ! robust_mode/nagain_flag/clear_average -> logical(c_bool); nagain holds
    ! only for the decode again nagain_flag asks for, and nclearave only for
    ! the next decode that runs (streaming_io.f90).
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
    if (in_cfg%aggressive_set)        params%naggressive = in_cfg%aggressive
    if (in_cfg%robust_mode_set)       params%nrobust   = in_cfg%robust_mode
    if (in_cfg%nagain_flag_set)       params%nagain    = in_cfg%nagain_flag
    if (in_cfg%tx_mode_set)           params%ntxmode   = in_cfg%tx_mode
    if (in_cfg%clear_average_set)     params%nclearave = in_cfg%clear_average

    ! ===== FT8/FT4 specifics cluster ===================
    ! Flat 1:1; the per-mode policy below touches
    ! none of these. multithreaded_ft8 selects the multithreaded FT8 decoder
    ! (ft8_engine_kernel.f90), the only reader of ft8_cycles,
    ! ft8_rxf_sensitivity, ft8_threads, ft8_low_threshold, ft8_subpass,
    ! ap_my_call, hide_ft8_duplicates, enable_dxc_search, wide_dxc_search,
    ! hound_mode, multi_instance, skip_tx1, nagain_filter and stop_hint. The
    ! engine refuses every FT8 decode, single-threaded too, while ft8_threads is
    ! outside [0,24] (decoder_engine.f90); the kernel runs 13 to 24 on one
    ! thread (ft8_engine_kernel.f90), so the stream declines a configure that
    ! leaves it outside [0,12] in FT8 (streaming_io.f90). ft8_ap_on and ap_width_hz reach both FT8
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

    ! ===== diagnostic / sequencing fields ==============
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

    ! ===== string & scaled / derived fields =========
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

    ! ===== bit-packed fields ===========
    ! ndepth_packed / nexp_decode_packed were computed at parse
    ! (pack_ndepth_nexp_decode_ in streaming_control.f90) from the 9 unpacked
    ! keys, so apply is flat. The legacy
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
    ! needed: the pack leaves a depth_level or q65_maxiters_level outside 1 to
    ! 3 unpacked, for the stream to decline, and clamps the NB byte, and none
    ! reaches a raw array index.
    if (in_cfg%ndepth_packed_set)      params%ndepth      = in_cfg%ndepth_packed
    if (in_cfg%nexp_decode_packed_set) params%nexp_decode = in_cfg%nexp_decode_packed

    ! ntol, nfa and nfb go through the search controls (apply_search_controls).

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
    ! utc (ISO "HH:MM:SS") is a SECOND route to nutc,
    ! kept alongside the legacy int nutc above. Applied AFTER nutc so utc WINS when
    ! a frame carries both (the richer ISO form). The apply order encodes the
    ! precedence — it does NOT depend on the parse order. utc_nutc was derived from
    ! the ISO string at parse (streaming_control.f90 parse_iso_time_).
    if (in_cfg%utc_set) params%nutc = in_cfg%utc_nutc

    ! Re-apply per-mode policy whenever mode or trperiod changes.
    ! Q65 60s emedelay (apply_per_mode_policy is emedelay-only).
    if (in_cfg%mode_set .or. in_cfg%trperiod_set)                              &
         call apply_per_mode_policy(params, io_mode, io_TRperiod)
  end subroutine apply_configure_fields

  ! The slot of the F Tol a mode puts back on entry (Ftol_SF in FT8,
  ! Ftol_Q65, Ftol_MSK144, Ftol_JT65, Ftol_JT4, Ftol_JT9), which WSJT-X saves
  ! when F Tol changes in that mode (on_sbFtol_valueChanged); 0 for none.
  pure integer function saved_slot(mode)
    integer, intent(in) :: mode
    select case (mode)
    case (8);     saved_slot = 1
    case (66);    saved_slot = 2
    case (144);   saved_slot = 3
    case (65);    saved_slot = 4
    case (4);     saved_slot = 5
    case (9);     saved_slot = 6
    case default; saved_slot = 0
    end select
  end function saved_slot

  ! The session's start: WSJT-X's readSettings puts back the starting mode's
  ! F Tol (50 until set) in JT9, FT8, Q65, JT65, JT4 and MSK144, F Tol keeps
  ! the form's 10 in the others (widgets/mainwindow_settings.cpp), and the
  ! mode is then set up. The command line's -L and -H are the waterfall's
  ! range, and FST4's F Low and F High in a session that starts in FST4; its
  ! -f is the Rx and Tx Freq the modes put back. params%ntol NTOL_UNSET is a
  ! session started without -F; with it, -F is set as a configure's ntol
  ! with mode is (JT65+JT9 takes it as jt9 did, at most 1000).
  subroutine start_search_controls(c, mode, params)
    type(search_controls), intent(out)   :: c
    integer,               intent(in)    :: mode
    type(params_block),    intent(inout) :: params
    type(configure_fields) :: given
    c%wide_nfa = params%nfa
    c%wide_nfb = params%nfb
    if (mode .eq. 240) then
       c%fst4_nfa = params%nfa
       c%fst4_nfb = params%nfb
    end if
    c%box = params%nfqso
    c%wf = params%nfqso
    c%tx = params%nftx
    c%rx_old = params%nfqso
    c%tx_old = params%nftx
    if (mode .eq. 241 .or. mode .eq. 242) c%fst4w_rx = params%nfqso
    if (mode .eq. 8 .and. iand(params%nexp_decode, 7) .eq. 6) c%fox_tx = params%nftx
    c%ftol = merge(50, 10, any(mode .eq. [9, 8, 66, 65, 4, 144]))
    given%ntol_set = params%ntol .ne. NTOL_UNSET
    given%ntol = params%ntol
    given%mode_set = .true.        ! capped, as jt9's cli_tolerance capped it
    given%mode = mode
    given%rxfreq_set = .true.      ! -f is the start's Rx and Tx Freq
    given%rxfreq = params%nfqso
    call apply_search_controls(c, given, -1, mode, params, .true., .false., .false.)
    if (mode .eq. 74 .and. given%ntol_set) then
       c%jt65jt9_ftol = min(given%ntol, 1000)
       params%ntol = c%jt65jt9_ftol
    end if
  end subroutine start_search_controls

  ! A configure's frequency tolerance, search range and Rx and Tx Freq, as
  ! WSJT-X's controls give them. Entering a mode (a configure with mode, or
  ! an accepted Settings change, after which WSJT-X sets its mode up again:
  ! on_actionSettings_triggered's set_mode) runs that mode's setup
  ! (widgets/mainwindow_modes.cpp); the frame's ntol, nfa, nfb, rxfreq and
  ! transmit frequency then go to the controls the mode uses, and params
  ! take the mode's values. A frame with only trperiod changes none of them,
  ! as WSJT-X's TR does not, but for FST4's range check. was_mode is the mode
  ! before the frame (-1 at the start); fast and was_fast are fast JT9 after
  ! and before it.
  subroutine apply_search_controls(c, cfg, was_mode, mode, params, entry, fast, was_fast)
    type(search_controls),  intent(inout) :: c
    type(configure_fields), intent(in)    :: cfg
    integer,                intent(in)    :: was_mode, mode
    type(params_block),     intent(inout) :: params
    logical,                intent(in)    :: entry, fast, was_fast
    logical :: vhf, single, hound, fox, superfox, superfox_hound, capped, rx_set, tx_set
    logical :: fst4w, saves_rx, fast_entry
    integer :: slot, lo, hi, top
    vhf = iand(params%nexp_decode, 64) .ne. 0
    single = iand(params%nexp_decode, 32) .ne. 0
    superfox = logical(params%b_superfox)
    hound = mode .eq. 8 .and. iand(params%nexp_decode, 7) .eq. 7
    fox = mode .eq. 8 .and. iand(params%nexp_decode, 7) .eq. 6
    superfox_hound = hound .and. superfox
    rx_set = cfg%rxfreq_set
    tx_set = cfg%rxfreq_set .or. cfg%tx_audio_offset_hz_set
    slot = saved_slot(mode)
    fst4w = mode .eq. 241 .or. mode .eq. 242
    ! The modes that keep a changed Rx for those that put it back (RxFreq_old,
    ! on_RxFreqSpinBox_valueChanged).
    saves_rx = .not. superfox_hound .and.                                     &
         .not. any(mode .eq. [144, 241, 242, MODE_WSPR, MODE_ECHO, MODE_FREQCAL])
    ! Fast JT9's setup, which a frame with mode or a Settings change runs, as
    ! does turning fast on (setupJT9 with Fast).
    fast_entry = fast .and. mode .eq. 9 .and. (entry .or. .not. was_fast)
    if (entry) then
       select case (mode)
       case (8);        c%ftol = c%saved(1)
       case (240);      if (.not. single) c%ftol = 20
       case (241, 242); c%ftol = 100      ! F Tol; the decoder takes FST4W's own
       case (4);        if (vhf) c%ftol = c%saved(5)
       ! Fast JT9 needs VHF features in WSJT-X.
       case (9);        if (vhf .or. fast) c%ftol = c%saved(6)
       case (65);       if (vhf) c%ftol = c%saved(4)
       case (66);       c%ftol = c%saved(2)
       case (144);      c%ftol = c%saved(3)
       case (MODE_FREQCAL); if (vhf) c%ftol = c%saved(6)   ! as JT9's setup
       case (MODE_ECHO);    if (vhf) c%ftol = c%saved(5)   ! as JT4's setup
       end select
       ! Choosing a mode sets F Tol's range, 20 to 500 in MSK144 and 1 to 1000
       ! otherwise, FST4W's 1 to 100 and JTTY's 2 to 500, which hold the values
       ! they carry (switch_mode, setupMSK144, setupJTTY); a held F Tol is
       ! saved as any change.
       lo = merge(20, 1, mode .eq. 144)
       hi = merge(500, 1000, mode .eq. 144)
       if (c%ftol .lt. lo .or. c%ftol .gt. hi) then
          c%ftol = max(lo, min(hi, c%ftol))
          if (slot .gt. 0) c%saved(slot) = c%ftol
       end if
       c%fst4w_ftol = max(1, min(100, c%fst4w_ftol))
       if (mode .eq. MODE_JTTY) c%jtty_ftol = max(2, min(500, c%jtty_ftol))
       ! Rx: WSJT-X decodes at the waterfall's Rx (c%wf), which a change of
       ! the Rx box moves, as do the setups of FST4, JT65, Q65, JTTY, SuperFox
       ! Hound, FreqCal and FST4W (its own box); the other setups only set
       ! the box, which moves it only if the value changes. Every setup but
       ! Hound's sets the box's range, 1400 to 1600 in MSK144 and 200 to 5000
       ! otherwise, which holds its value (switch_mode). FST4, FT4, JT4,
       ! JT65, Q65, JTTY (from another mode) and Hound without SuperFox put
       ! back RxFreq_old; MSK144 sets 1500; SuperFox Hound 750 unless already
       ! 700 to 800; choosing FreqCal sets 1500, keeping the box on a Settings
       ! change (setupFST4 to setupFreqCal).
       if (.not. hound) then
          c%box_lo = merge(1400, 200, mode .eq. 144)
          c%box_hi = merge(1600, 5000, mode .eq. 144)
          call set_box(c%box)
       end if
       select case (mode)
       case (240, 65, 66)
          if (.not. rx_set) call set_box(c%rx_old)
          c%wf = c%box
       case (5, 4)
          if (.not. rx_set) call set_box(c%rx_old)
       case (MODE_JTTY)
          if (was_mode .ne. MODE_JTTY .and. .not. rx_set) call set_box(c%rx_old)
          c%wf = c%box
       case (8)
          if (superfox_hound) then
             c%wf = c%box
             if (.not. rx_set .and. (c%box .lt. 700 .or. c%box .gt. 800)) call set_box(750)
          else if (hound) then
             if (.not. rx_set) call set_box(c%rx_old)
          end if
       case (144)
          if (.not. rx_set) call set_box(1500)
       case (MODE_FREQCAL)
          if (cfg%mode_set .and. .not. rx_set) c%box = 1500   ! signals blocked
          c%wf = c%box
       case (241, 242)
          c%wf = c%fst4w_rx
       end select
       ! Tx: FST4, FT4, JT4, JT65, Q65 and JTTY (from another mode) put back
       ! TxFreq_old; MSK144 sets 1500; FT8 puts back TxFreq_old but in Fox,
       ! which takes 750 with SuperFox and its own otherwise (300 until set).
       if (.not. tx_set) then
          if (any(mode .eq. [240, 5, 4, 65, 66]) .or.                          &
               (mode .eq. MODE_JTTY .and. was_mode .ne. MODE_JTTY)) then
             params%nftx = c%tx_old
          else if (mode .eq. 144) then
             params%nftx = 1500
          else if (mode .eq. 8) then
             if (fox .and. superfox) then
                params%nftx = 750
             else if (fox) then
                params%nftx = c%fox_tx
             else
                params%nftx = c%tx_old
             end if
          end if
       end if
    end if
    ! Fast JT9's setup sets Rx and Tx to 700.
    if (fast_entry) then
       if (.not. rx_set) call set_box(700)
       if (.not. tx_set) params%nftx = 700
    end if
    ! A frame's rxfreq: FST4W's own box there, the Rx box elsewhere (unheld,
    ! as before; the range checks refuse what the decoder cannot take); a
    ! box that changes moves the waterfall's Rx.
    if (rx_set) then
       if (fst4w) then
          c%fst4w_rx = cfg%rxfreq
          c%wf = cfg%rxfreq
       else if (cfg%rxfreq .ne. c%box) then
          c%box = cfg%rxfreq
          c%wf = cfg%rxfreq
          if (saves_rx) c%rx_old = cfg%rxfreq
       end if
    end if
    capped = cfg%mode_set .or. cfg%trperiod_set
    if (cfg%ntol_set) then
       ! A frame with mode or trperiod caps it as WSJT-X's boxes do (FST4W's
       ! at 100, F Tol at 1000); one without them leaves it to the range
       ! checks, which refuse a value the decoder cannot take.
       select case (mode)
       case (241, 242); c%fst4w_ftol = merge(min(cfg%ntol, 100), cfg%ntol, capped)
       case (MODE_JTTY); c%jtty_ftol = cfg%ntol  ! the stream's 0..6000 Hz check
       ! JT65+JT9, which WSJT-X no longer has, keeps the codec's rule: 20 in a
       ! frame with mode or trperiod, what it says otherwise.
       case (74);       c%jt65jt9_ftol = merge(20, cfg%ntol, capped)
       case default
          c%ftol = merge(min(cfg%ntol, 1000), cfg%ntol, capped)
          if (slot .gt. 0) c%saved(slot) = c%ftol
       end select
    else if (mode .eq. 74 .and. capped) then
       c%jt65jt9_ftol = 20
    end if
    ! SuperFox Hound holds F Tol to 100 and the Rx box to 200..1400 Hz
    ! (on_sbFtol_valueChanged, on_RxFreqSpinBox_valueChanged); the held F Tol
    ! is saved as any other. SuperFox Fox holds Tx to 750
    ! (on_TxFreqSpinBox_valueChanged).
    if (superfox_hound) then
       if (c%ftol .gt. 100) then
          c%ftol = 100
          c%saved(1) = 100
       end if
       if (c%box .lt. 200 .or. c%box .gt. 1400) then
          c%box = max(200, min(1400, c%box))
          c%wf = c%box
       end if
    end if
    if (fox .and. superfox) params%nftx = 750
    ! WSJT-X's decoder takes the waterfall's Rx, but MSK144's, fast JT9's,
    ! FreqCal's and JTTY's, which take the Rx box (fastSink, MainWindow::decode's
    ! narg, freqcal_, the JTTY span's RxFreqSpinBox_2).
    params%nfqso = c%wf
    if (mode .eq. 144 .or. mode .eq. MODE_FREQCAL .or. mode .eq. MODE_JTTY .or.     &
         (fast .and. mode .eq. 9)) params%nfqso = c%box
    if (cfg%nfa_set) then
       if (mode .eq. 240) then
          c%fst4_nfa = cfg%nfa
       else
          c%wide_nfa = cfg%nfa
       end if
    end if
    if (cfg%nfb_set) then
       if (mode .eq. 240) then
          c%fst4_nfb = cfg%nfb
       else
          c%wide_nfb = cfg%nfb
       end if
    end if
    ! FST4's F Low and F High stay inside the waterfall's range, F High in
    ! whole hundreds (chk_FST4_freq_range: on entry, a TR change and their
    ! own changes).
    if (mode .eq. 240 .and. (entry .or. cfg%trperiod_set .or. cfg%nfa_set .or. cfg%nfb_set)) then
       c%fst4_nfa = max(c%fst4_nfa, c%wide_nfa)
       top = min(5000, c%wide_nfb)
       if (c%fst4_nfb .gt. top) c%fst4_nfb = 100 * (top / 100)
    end if
    select case (mode)
    case (241, 242);  params%ntol = c%fst4w_ftol
    case (MODE_JTTY); params%ntol = c%jtty_ftol
    case (74);        params%ntol = c%jt65jt9_ftol
    case default;     params%ntol = c%ftol
    end select
    ! SuperFox Hound's decode with multithreaded FT8 on takes 50
    ! (MainWindow::decode).
    if (superfox_hound .and. logical(params%lmultift8)) params%ntol = 50
    ! FST4 searches F Low to F High, or Rx Freq +/- F Tol with single decode
    ! (MainWindow::decode); the other modes the waterfall's range.
    if (mode .eq. 240 .and. single) then
       params%nfa = params%nfqso - c%ftol
       params%nfb = params%nfqso + c%ftol
    else if (mode .eq. 240) then
       params%nfa = c%fst4_nfa
       params%nfb = c%fst4_nfb
    else
       params%nfa = c%wide_nfa
       params%nfb = c%wide_nfb
    end if
    ! A change of Tx Freq is kept for the modes that put it back, but in the
    ! modes that never keep it, and in Fox without SuperFox as Fox's own
    ! (on_TxFreqSpinBox_valueChanged).
    if (params%nftx .ne. c%tx .and. .not. fox .and.                           &
         .not. any(mode .eq. [144, 241, 242, MODE_WSPR, MODE_ECHO, MODE_FREQCAL])) &
         c%tx_old = params%nftx
    if (params%nftx .ne. c%tx .and. fox .and. .not. superfox) c%fox_tx = params%nftx
    c%tx = params%nftx

  contains

    ! The Rx box set to n, held to its range; a change moves the waterfall's
    ! Rx and is kept where the mode keeps it.
    subroutine set_box(n)
      integer, intent(in) :: n
      integer :: held
      held = max(c%box_lo, min(c%box_hi, n))
      if (held .ne. c%box) then
         c%box = held
         c%wf = held
         if (saves_rx) c%rx_old = held
      end if
    end subroutine set_box
  end subroutine apply_search_controls

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
    case ('wspr_threads')
       key = 'wspr_threads'; set = in_cfg%wspr_threads_set
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
    case ('aggressiveness')
       key = 'aggressive'; set = in_cfg%aggressive_set
    case default
       ! flags and passes: no configure key sets a value the engine refuses
       ! (the conversion makes them 0/1 or clamps them).
       key = field; set = .true.
    end select
    if (present(carried)) carried = .not. set
  end function engine_range_key

  ! A change the GUI cancels its multithreaded FT8 backpressure on
  ! (cancelPendingFt8Decode, widgets/mainwindow.cpp): its FT8 decoder menus,
  ! the depth among them (the rest of ndepth, its averaging, deep search and
  ! automatic clear toggles, is checked when the final is taken up,
  ! ft8_identity_change), and its Settings (settings_dialog_change). The
  ! codec also cancels on hound_mode and eme_delay_seconds, which WSJT-X does
  ! not have.
  pure logical function ft8_settings_change(was, now)
    type(params_block), intent(in) :: was, now
    ft8_settings_change = iand(was%ndepth, 7) .ne. iand(now%ndepth, 7) .or. &
         (was%lmultift8 .neqv. now%lmultift8) .or. was%nmt .ne. now%nmt .or. &
         was%ndecoderstart .ne. now%ndecoderstart .or.                     &
         was%nft8cycles .ne. now%nft8cycles .or.                           &
         was%nft8rxfsens .ne. now%nft8rxfsens .or.                         &
         (was%lft8lowth .neqv. now%lft8lowth) .or.                         &
         (was%lft8subpass .neqv. now%lft8subpass) .or.                     &
         (was%lwidedxcsearch .neqv. now%lwidedxcsearch) .or.               &
         (was%lhideft8dupes .neqv. now%lhideft8dupes) .or.                 &
         (was%lft8apon .neqv. now%lft8apon) .or.                           &
         (was%lhound .neqv. now%lhound) .or. was%emedelay .ne. now%emedelay .or. &
         settings_dialog_change(was, now)
  end function ft8_settings_change

  ! A change of the Settings WSJT-X's decode reads that the params hold: my
  ! call and grid, SuperFox, the special operating activity (collapsed),
  ! single decode, VHF features, trials, aggressive and two-pass
  ! (MainWindow::decode). Accepting one cancels a waiting FT8 final and sets
  ! the mode up again (on_actionSettings_triggered). The stream checks the
  ! Settings it keeps outside the params (Low sidelobes, the activity before
  ! packing, Decode after EME delay) where a configure applies
  ! (streaming_io.f90).
  pure logical function settings_dialog_change(was, now)
    type(params_block), intent(in) :: was, now
    settings_dialog_change =                                               &
         any(was%mycall .ne. now%mycall) .or. any(was%mygrid .ne. now%mygrid) .or. &
         (was%b_superfox .neqv. now%b_superfox) .or.                       &
         iand(was%nexp_decode, 255) .ne. iand(now%nexp_decode, 255) .or.   &
         was%nranera .ne. now%nranera .or.                                 &
         was%naggressive .ne. now%naggressive .or. was%n2pass .ne. now%n2pass
  end function settings_dialog_change

  ! The rest of the GUI's pending-final identity, which it checks
  ! only when it takes the final up (DecodeOperatingContext.hpp
  ! hasSameFt8PendingIdentity, widgets/mainwindow.cpp publishPendingFt8Decode):
  ! the decode range, the submode and the whole depth (bit 0x100 of ndepth is
  ! the JT9 window request, which make_op sets from low_sidelobes).
  pure logical function ft8_identity_change(was, now)
    type(params_block), intent(in) :: was, now
    ft8_identity_change = was%nfa .ne. now%nfa .or. was%nfb .ne. now%nfb .or. &
         was%nsubmode .ne. now%nsubmode .or.                               &
         iand(was%ndepth, not(256)) .ne. iand(now%ndepth, not(256))
  end function ft8_identity_change

end module streaming_apply
