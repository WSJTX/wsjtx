! Phase 4 parse-unit regression driver ("unit parse->pack assert").
! Exercises parse_control_frame for the 9 Phase 4
! keys + the tricky cases: bool true/false, his_call vs his_call_standard
! substring non-collision (both orderings), wrong-type detection (bool/string/
! int), and value-vs-key aliasing (a string value equal to a key token must NOT
! be mistaken for that key). Compiled + run by
! test/run-phase4-parse-unit.sh against lib/streaming_control.f90.
!
! SCOPE: this guards the JSON -> configure_fields PARSE (the module-public entry
! point). The configure_fields -> params APPLY routing now lives in the module-
! public streaming_apply::apply_configure_fields (extracted from the
! former nested-internal apply_configure_) and is asserted by
! test/run-apply-routing-unit.sh — so the 9 inert Phase 4 fields' apply routing
! IS now unit-guarded, not just code-review + no-regression.
program p4_parse_test
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
  call frame_H()
  call frame_I()

  write(*,'(a)') '------------------------------------------------------------'
  if (nfail .eq. 0) then
     write(*,'(a)') 'ALL PHASE 4 PARSE-UNIT CHECKS PASSED'
  else
     write(*,'(a,i0,a)') 'PHASE 4 PARSE-UNIT: ', nfail, ' CHECK(S) FAILED'
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

  ! Frame A: all 9 keys, bools true.
  subroutine frame_A()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","mode":"FT8","his_call":"DX1ABC","his_grid":"FN20",' //  &
      '"my_b_call":"KJ5HST/P","his_b_call":"DX1ABC/P","my_call_standard":true,' //&
      '"his_call_standard":true,"tx_audio_offset_hz":1234,' //                 &
      '"jt65_jt9_split_hz":2500,"max_drift_hz":8}'
    write(*,'(a)') 'Frame A: all 9 keys present, bools true'
    call parse_control_frame(b, action, cfg, terr)
    call ok('action == CTRL_CONFIGURE', action .eq. CTRL_CONFIGURE)
    call ok('terr not present',          .not. terr%present)
    call ok('his_call == DX1ABC',        cfg%his_call_set  .and. trim(cfg%his_call)  .eq. 'DX1ABC')
    call ok('his_grid == FN20',          cfg%his_grid_set  .and. trim(cfg%his_grid)  .eq. 'FN20')
    call ok('my_b_call == KJ5HST/P',     cfg%my_b_call_set .and. trim(cfg%my_b_call) .eq. 'KJ5HST/P')
    call ok('his_b_call == DX1ABC/P',    cfg%his_b_call_set.and. trim(cfg%his_b_call).eq. 'DX1ABC/P')
    call ok('my_call_standard == true',  cfg%my_call_standard_set  .and. cfg%my_call_standard)
    call ok('his_call_standard == true', cfg%his_call_standard_set .and. cfg%his_call_standard)
    call ok('tx_audio_offset_hz == 1234',cfg%tx_audio_offset_hz_set .and. cfg%tx_audio_offset_hz .eq. 1234)
    call ok('jt65_jt9_split_hz == 2500', cfg%jt65_jt9_split_hz_set  .and. cfg%jt65_jt9_split_hz  .eq. 2500)
    call ok('max_drift_hz == 8',         cfg%max_drift_hz_set .and. cfg%max_drift_hz .eq. 8)
  end subroutine frame_A

  ! Frame B: bools false (distinguish "set to false" from "absent").
  subroutine frame_B()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","my_call_standard":false,"his_call_standard":false}'
    write(*,'(a)') 'Frame B: bools explicitly false'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',                .not. terr%present)
    call ok('my_call_standard set, value false',  cfg%my_call_standard_set  .and. (.not. cfg%my_call_standard))
    call ok('his_call_standard set, value false', cfg%his_call_standard_set .and. (.not. cfg%his_call_standard))
  end subroutine frame_B

  ! Frame C: his_call AND his_call_standard both present, BOTH orderings.
  ! "his_call" is a substring of "his_call_standard" — the closing quote in the
  ! key token must prevent the shorter key from matching the longer one.
  subroutine frame_C()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b1 =                                      &
      '{"t":"configure","his_call":"W1AW","his_call_standard":true}'
    character(len=*), parameter :: b2 =                                      &
      '{"t":"configure","his_call_standard":true,"his_call":"W1AW"}'
    write(*,'(a)') 'Frame C: his_call vs his_call_standard substring non-collision'
    call parse_control_frame(b1, action, cfg, terr)
    call ok('order1: terr not present',          .not. terr%present)
    call ok('order1: his_call == W1AW',          cfg%his_call_set .and. trim(cfg%his_call) .eq. 'W1AW')
    call ok('order1: his_call_standard == true', cfg%his_call_standard_set .and. cfg%his_call_standard)
    call parse_control_frame(b2, action, cfg, terr)
    call ok('order2: terr not present',          .not. terr%present)
    call ok('order2: his_call == W1AW',          cfg%his_call_set .and. trim(cfg%his_call) .eq. 'W1AW')
    call ok('order2: his_call_standard == true', cfg%his_call_standard_set .and. cfg%his_call_standard)
  end subroutine frame_C

  ! Frame D: bool key carrying a number -> configure_type_error.
  subroutine frame_D()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","my_call_standard":5}'
    write(*,'(a)') 'Frame D: my_call_standard:5 (number) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',          terr%present)
    call ok('key == my_call_standard',trim(terr%key) .eq. 'my_call_standard')
    call ok('expected == bool',      trim(terr%expected) .eq. 'bool')
    call ok('got == number',         trim(terr%got) .eq. 'number')
  end subroutine frame_D

  ! Frame E: bool key carrying a string -> configure_type_error.
  subroutine frame_E()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","his_call_standard":"true"}'
    write(*,'(a)') 'Frame E: his_call_standard:"true" (string) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',           terr%present)
    call ok('key == his_call_standard',trim(terr%key) .eq. 'his_call_standard')
    call ok('expected == bool',       trim(terr%expected) .eq. 'bool')
    call ok('got == string',          trim(terr%got) .eq. 'string')
  end subroutine frame_E

  ! Frame F: string key carrying a number -> configure_type_error.
  subroutine frame_F()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","his_call":123}'
    write(*,'(a)') 'Frame F: his_call:123 (number) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',     terr%present)
    call ok('key == his_call',  trim(terr%key) .eq. 'his_call')
    call ok('expected == string',trim(terr%expected) .eq. 'string')
    call ok('got == number',    trim(terr%got) .eq. 'number')
  end subroutine frame_F

  ! Frame G: int key carrying a string -> configure_type_error.
  subroutine frame_G()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","max_drift_hz":"abc"}'
    write(*,'(a)') 'Frame G: max_drift_hz:"abc" (string) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',        terr%present)
    call ok('key == max_drift_hz', trim(terr%key) .eq. 'max_drift_hz')
    call ok('expected == int',     trim(terr%expected) .eq. 'int')
    call ok('got == string',       trim(terr%got) .eq. 'string')
  end subroutine frame_G

  ! Frame H: value-vs-key aliasing — his_call VALUE equals the "max_drift_hz"
  ! token, with no real max_drift_hz key. The value must NOT be mistaken for the
  ! key: max_drift_hz stays unset (benign extraction miss) and NO false type
  ! error is raised (anchored find_key_ ignores the value). Aliasing guard.
  subroutine frame_H()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","his_call":"max_drift_hz"}'
    write(*,'(a)') 'Frame H: his_call value == "max_drift_hz" token (aliasing guard)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present (no false decline)', .not. terr%present)
    call ok('his_call == max_drift_hz',  cfg%his_call_set .and. trim(cfg%his_call) .eq. 'max_drift_hz')
    call ok('max_drift_hz stays UNSET',  .not. cfg%max_drift_hz_set)
  end subroutine frame_H

  ! Frame I: Phase 4 keys absent -> all unset, no error (keys are optional).
  subroutine frame_I()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mode":"FT8"}'
    write(*,'(a)') 'Frame I: Phase 4 keys absent -> unset'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',          .not. terr%present)
    call ok('his_call unset',            .not. cfg%his_call_set)
    call ok('his_grid unset',            .not. cfg%his_grid_set)
    call ok('my_b_call unset',           .not. cfg%my_b_call_set)
    call ok('his_b_call unset',          .not. cfg%his_b_call_set)
    call ok('my_call_standard unset',    .not. cfg%my_call_standard_set)
    call ok('his_call_standard unset',   .not. cfg%his_call_standard_set)
    call ok('tx_audio_offset_hz unset',  .not. cfg%tx_audio_offset_hz_set)
    call ok('jt65_jt9_split_hz unset',   .not. cfg%jt65_jt9_split_hz_set)
    call ok('max_drift_hz unset',        .not. cfg%max_drift_hz_set)
  end subroutine frame_I

end program p4_parse_test
