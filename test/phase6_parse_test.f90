! Phase 6 parse-unit regression driver. Exercises parse_control_frame for the 24
! Phase 6 FT8/FT4-specifics keys (19 bools + 5 ints) + the tricky cases: bool
! true/false (distinguishing "set false" from "absent"), int parse, wrong-type
! detection (bool-as-number, int-as-string), the "mode" substring non-collisions
! introduced by hint_mode / hound_mode / superfox_mode (all contain "mode"), and
! value-vs-key aliasing. Compiled + run by
! test/run-phase6-parse-unit.sh against lib/streaming_control.f90.
!
! SCOPE: this guards the JSON -> configure_fields PARSE (the module-public entry
! point). The configure_fields -> params APPLY routing lives in the module-public
! streaming_apply::apply_configure_fields and is asserted by
! test/run-apply-routing-unit.sh (which includes the 24 Phase 6 slot asserts +
! the multithreaded_ft8 ignore-non-false guard). All 24 Phase 6 keys are inert on
! the FT8/JT9 fixtures (multithreaded_ft8 is destructive and
! apply-guarded, the rest read only on multithread/JT65/superfox/WAV paths or have
! no AP-only decodes on the fixture), so no decode-output gate can assert their
! values; this parse-unit + the apply-routing unit are their coverage.
program p6_parse_test
  use streaming_control, only: parse_control_frame, configure_fields,        &
       control_type_error, CTRL_CONFIGURE
  implicit none
  integer :: nfail
  nfail = 0

  call frame_A()
  call frame_B()
  call frame_C()
  call frame_D()
  call frame_E()
  call frame_F()
  call frame_G()

  write(*,'(a)') '------------------------------------------------------------'
  if (nfail .eq. 0) then
     write(*,'(a)') 'ALL PHASE 6 PARSE-UNIT CHECKS PASSED'
  else
     write(*,'(a,i0,a)') 'PHASE 6 PARSE-UNIT: ', nfail, ' CHECK(S) FAILED'
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

  ! Frame A: all 24 keys present — 19 bools true, 5 ints at distinct values.
  subroutine frame_A()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","multithreaded_ft8":true,"ft8_cycles":2,' //              &
      '"ft8_rxf_sensitivity":4,"ft8_threads":3,"ft8_decoder_start":1,' //         &
      '"ft8_low_threshold":true,"ft8_subpass":true,"ft8_ap_on":true,' //          &
      '"ap_cq_only":true,"ap_my_call":true,"jt65_ap_on":true,' //                 &
      '"ap_width_hz":150,"hide_ft8_duplicates":true,"common_ft8b":true,' //       &
      '"enable_dxc_search":true,"wide_dxc_search":true,"superfox_mode":true,' //  &
      '"even_sequence":true,"hound_mode":true,"multi_instance":true,' //          &
      '"skip_tx1":true,"nagain_filter":true,"stop_hint":true,"hint_mode":true}'
    write(*,'(a)') 'Frame A: all 24 keys present (19 bools true, 5 ints distinct)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('action == CTRL_CONFIGURE', action .eq. CTRL_CONFIGURE)
    call ok('terr not present',          .not. terr%present)
    ! ints
    call ok('ft8_cycles == 2',           cfg%ft8_cycles_set          .and. cfg%ft8_cycles          .eq. 2)
    call ok('ft8_rxf_sensitivity == 4',  cfg%ft8_rxf_sensitivity_set .and. cfg%ft8_rxf_sensitivity .eq. 4)
    call ok('ft8_threads == 3',          cfg%ft8_threads_set         .and. cfg%ft8_threads         .eq. 3)
    call ok('ft8_decoder_start == 1',    cfg%ft8_decoder_start_set   .and. cfg%ft8_decoder_start   .eq. 1)
    call ok('ap_width_hz == 150 (RAW)',  cfg%ap_width_hz_set         .and. cfg%ap_width_hz         .eq. 150)
    ! bools (true)
    call ok('multithreaded_ft8 true',    cfg%multithreaded_ft8_set   .and. cfg%multithreaded_ft8)
    call ok('ft8_low_threshold true',    cfg%ft8_low_threshold_set   .and. cfg%ft8_low_threshold)
    call ok('ft8_subpass true',          cfg%ft8_subpass_set         .and. cfg%ft8_subpass)
    call ok('ft8_ap_on true',            cfg%ft8_ap_on_set           .and. cfg%ft8_ap_on)
    call ok('ap_cq_only true',           cfg%ap_cq_only_set          .and. cfg%ap_cq_only)
    call ok('ap_my_call true',           cfg%ap_my_call_set          .and. cfg%ap_my_call)
    call ok('jt65_ap_on true',           cfg%jt65_ap_on_set          .and. cfg%jt65_ap_on)
    call ok('hide_ft8_duplicates true',  cfg%hide_ft8_duplicates_set .and. cfg%hide_ft8_duplicates)
    call ok('common_ft8b true',          cfg%common_ft8b_set         .and. cfg%common_ft8b)
    call ok('enable_dxc_search true',    cfg%enable_dxc_search_set   .and. cfg%enable_dxc_search)
    call ok('wide_dxc_search true',      cfg%wide_dxc_search_set     .and. cfg%wide_dxc_search)
    call ok('superfox_mode true',        cfg%superfox_mode_set       .and. cfg%superfox_mode)
    call ok('even_sequence true',        cfg%even_sequence_set       .and. cfg%even_sequence)
    call ok('hound_mode true',           cfg%hound_mode_set          .and. cfg%hound_mode)
    call ok('multi_instance true',       cfg%multi_instance_set      .and. cfg%multi_instance)
    call ok('skip_tx1 true',             cfg%skip_tx1_set            .and. cfg%skip_tx1)
    call ok('nagain_filter true',        cfg%nagain_filter_set       .and. cfg%nagain_filter)
    call ok('stop_hint true',            cfg%stop_hint_set           .and. cfg%stop_hint)
    call ok('hint_mode true',            cfg%hint_mode_set           .and. cfg%hint_mode)
  end subroutine frame_A

  ! Frame B: bools false (distinguish "set false" from "absent"); ap_width_hz=0.
  subroutine frame_B()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","ft8_ap_on":false,"ap_cq_only":false,' //                 &
      '"superfox_mode":false,"hint_mode":false,"stop_hint":false,' //             &
      '"multithreaded_ft8":false,"ap_width_hz":0}'
    write(*,'(a)') 'Frame B: bools false (set, not absent); ap_width_hz=0'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',                .not. terr%present)
    call ok('ft8_ap_on set, value false',      cfg%ft8_ap_on_set         .and. (.not. cfg%ft8_ap_on))
    call ok('ap_cq_only set, value false',     cfg%ap_cq_only_set        .and. (.not. cfg%ap_cq_only))
    call ok('superfox_mode set, value false',  cfg%superfox_mode_set     .and. (.not. cfg%superfox_mode))
    call ok('hint_mode set, value false',      cfg%hint_mode_set         .and. (.not. cfg%hint_mode))
    call ok('stop_hint set, value false',      cfg%stop_hint_set         .and. (.not. cfg%stop_hint))
    call ok('multithreaded_ft8 set, false',    cfg%multithreaded_ft8_set .and. (.not. cfg%multithreaded_ft8))
    call ok('ap_width_hz set, value 0',        cfg%ap_width_hz_set       .and. cfg%ap_width_hz .eq. 0)
  end subroutine frame_B

  ! Frame C: "mode" substring non-collision. "mode" is a substring of hint_mode /
  ! hound_mode / superfox_mode / tx_mode / submode — the structural key locator
  ! (closing quote in the quoted token + anchor) must keep them distinct. Both
  ! orderings. A raw-index regression would mis-classify these.
  subroutine frame_C()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b1 =                                      &
      '{"t":"configure","mode":"FT8","tx_mode":"JT9","superfox_mode":true,' //    &
      '"hound_mode":false,"hint_mode":true}'
    character(len=*), parameter :: b2 =                                      &
      '{"t":"configure","hint_mode":true,"hound_mode":false,' //                  &
      '"superfox_mode":true,"tx_mode":"JT9","mode":"FT8"}'
    write(*,'(a)') 'Frame C: mode vs tx_mode/superfox_mode/hound_mode/hint_mode'
    call parse_control_frame(b1, action, cfg, terr)
    call ok('order1: terr not present',     .not. terr%present)
    call ok('order1: mode == 8 (FT8)',      cfg%mode_set    .and. cfg%mode    .eq. 8)
    call ok('order1: tx_mode == 9 (JT9)',   cfg%tx_mode_set .and. cfg%tx_mode .eq. 9)
    call ok('order1: superfox_mode true',   cfg%superfox_mode_set .and. cfg%superfox_mode)
    call ok('order1: hound_mode false',     cfg%hound_mode_set    .and. (.not. cfg%hound_mode))
    call ok('order1: hint_mode true',       cfg%hint_mode_set     .and. cfg%hint_mode)
    call parse_control_frame(b2, action, cfg, terr)
    call ok('order2: terr not present',     .not. terr%present)
    call ok('order2: mode == 8 (FT8)',      cfg%mode_set    .and. cfg%mode    .eq. 8)
    call ok('order2: tx_mode == 9 (JT9)',   cfg%tx_mode_set .and. cfg%tx_mode .eq. 9)
    call ok('order2: superfox_mode true',   cfg%superfox_mode_set .and. cfg%superfox_mode)
    call ok('order2: hound_mode false',     cfg%hound_mode_set    .and. (.not. cfg%hound_mode))
    call ok('order2: hint_mode true',       cfg%hint_mode_set     .and. cfg%hint_mode)
  end subroutine frame_C

  ! Frame D: int key carrying a string -> configure_type_error.
  subroutine frame_D()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","ap_width_hz":"wide"}'
    write(*,'(a)') 'Frame D: ap_width_hz:"wide" (string) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',          terr%present)
    call ok('key == ap_width_hz',    trim(terr%key) .eq. 'ap_width_hz')
    call ok('expected == int',       trim(terr%expected) .eq. 'int')
    call ok('got == string',         trim(terr%got) .eq. 'string')
  end subroutine frame_D

  ! Frame E: bool key carrying a number -> configure_type_error (mirrors the
  ! Gate 14 fixture, ft8_ap_on:5).
  subroutine frame_E()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","ft8_ap_on":5}'
    write(*,'(a)') 'Frame E: ft8_ap_on:5 (number) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',     terr%present)
    call ok('key == ft8_ap_on', trim(terr%key) .eq. 'ft8_ap_on')
    call ok('expected == bool', trim(terr%expected) .eq. 'bool')
    call ok('got == number',    trim(terr%got) .eq. 'number')
  end subroutine frame_E

  ! Frame F: value-vs-key aliasing — the mycall VALUE is the literal "ft8_ap_on"
  ! token, with no real ft8_ap_on key. The anchored locator must NOT mistake the
  ! string value for the key: ft8_ap_on stays unset and NO false type error fires
  ! (the benign no-false-decline contract for a Phase 6 key).
  subroutine frame_F()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mycall":"ft8_ap_on"}'
    write(*,'(a)') 'Frame F: mycall value == "ft8_ap_on" token (aliasing guard)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present (no false decline)', .not. terr%present)
    call ok('mycall == ft8_ap_on',  cfg%mycall_set .and. trim(cfg%mycall) .eq. 'ft8_ap_on')
    call ok('ft8_ap_on stays UNSET', .not. cfg%ft8_ap_on_set)
  end subroutine frame_F

  ! Frame G: Phase 6 keys absent -> all unset, no error (keys are optional).
  subroutine frame_G()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mode":"FT8"}'
    write(*,'(a)') 'Frame G: Phase 6 keys absent -> unset'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',           .not. terr%present)
    call ok('multithreaded_ft8 unset',    .not. cfg%multithreaded_ft8_set)
    call ok('ft8_cycles unset',           .not. cfg%ft8_cycles_set)
    call ok('ft8_ap_on unset',            .not. cfg%ft8_ap_on_set)
    call ok('ap_width_hz unset',          .not. cfg%ap_width_hz_set)
    call ok('superfox_mode unset',        .not. cfg%superfox_mode_set)
    call ok('hint_mode unset',            .not. cfg%hint_mode_set)
    call ok('stop_hint unset',            .not. cfg%stop_hint_set)
    call ok('nagain_filter unset',        .not. cfg%nagain_filter_set)
  end subroutine frame_G

end program p6_parse_test
