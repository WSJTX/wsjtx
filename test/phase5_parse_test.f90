! Phase 5 parse-unit regression driver ("unit parse->pack assert").
! Exercises parse_control_frame for the 12 Phase 5
! decoder-tuning keys + the tricky cases: real(8) keys (dt_tolerance_seconds /
! eme_delay_seconds), the tx_mode enum (string JT9/JT65 -> 9/65 and raw int),
! bool true/false (robust_mode / nagain_flag / clear_average), wrong-type
! detection (real/int/bool), the "mode" vs "tx_mode" + "min_width" vs "min_sync"
! substring non-collision, and value-vs-key aliasing (a string value equal to a
! Phase-5 key token must NOT be mistaken for that key).
! Compiled + run by test/run-phase5-parse-unit.sh against lib/streaming_control.f90.
!
! SCOPE: this guards the JSON -> configure_fields PARSE (the module-public entry
! point). The configure_fields -> params APPLY routing now lives in the module-
! public streaming_apply::apply_configure_fields (extracted from the
! former nested-internal apply_configure_) and is asserted by
! test/run-apply-routing-unit.sh. For Phase 5, 10 of 12 fields are observably
! inert on the FT8/JT9 fixtures; the two observable keys (nagain_flag -> FT8,
! npts_c0_array -> JT9) ALSO get a decode-effect gate in run-schema-v1-fixtures.sh
! (Gates 11/12), which proves they reach the decoder (apply routed them to a
! decode-affecting param).
program p5_parse_test
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
     write(*,'(a)') 'ALL PHASE 5 PARSE-UNIT CHECKS PASSED'
  else
     write(*,'(a,i0,a)') 'PHASE 5 PARSE-UNIT: ', nfail, ' CHECK(S) FAILED'
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

  ! Frame A: all 12 keys, bools true, tx_mode as the "JT65" enum string.
  subroutine frame_A()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","dt_tolerance_seconds":0.1,"eme_delay_seconds":1.5,' //   &
      '"kin_samples":64800,"nzhsym_per_period":50,"npts_c0_array":74736,' //      &
      '"min_width":1,"min_sync":2,"n_2pass":1,"robust_mode":true,' //             &
      '"nagain_flag":true,"tx_mode":"JT65","clear_average":true}'
    write(*,'(a)') 'Frame A: all 12 keys present, bools true, tx_mode="JT65"'
    call parse_control_frame(b, action, cfg, terr)
    call ok('action == CTRL_CONFIGURE', action .eq. CTRL_CONFIGURE)
    call ok('terr not present',          .not. terr%present)
    ! 0.1 is NOT exactly representable in real(4); the tight 1e-10 tolerance also
    ! proves the configure_fields member is real(8) (a real(4) member would hold
    ! ~0.10000000149 and fail). The narrowing to real(c_float) happens at APPLY.
    call ok('dt_tolerance_seconds == 0.1 (real8)', cfg%dt_tolerance_seconds_set .and. &
         abs(cfg%dt_tolerance_seconds - 0.1d0) .lt. 1.d-10)
    call ok('eme_delay_seconds == 1.5',    cfg%eme_delay_seconds_set .and.    &
         abs(cfg%eme_delay_seconds - 1.5d0) .lt. 1.d-9)
    call ok('kin_samples == 64800',        cfg%kin_samples_set .and. cfg%kin_samples .eq. 64800)
    call ok('nzhsym_per_period == 50',     cfg%nzhsym_per_period_set .and. cfg%nzhsym_per_period .eq. 50)
    call ok('npts_c0_array == 74736',      cfg%npts_c0_array_set .and. cfg%npts_c0_array .eq. 74736)
    call ok('min_width == 1',              cfg%min_width_set .and. cfg%min_width .eq. 1)
    call ok('min_sync == 2',               cfg%min_sync_set  .and. cfg%min_sync  .eq. 2)
    call ok('n_2pass == 1',                cfg%n_2pass_set   .and. cfg%n_2pass   .eq. 1)
    call ok('robust_mode == true',         cfg%robust_mode_set   .and. cfg%robust_mode)
    call ok('nagain_flag == true',         cfg%nagain_flag_set   .and. cfg%nagain_flag)
    call ok('tx_mode == 65 (JT65)',        cfg%tx_mode_set   .and. cfg%tx_mode .eq. 65)
    call ok('clear_average == true',       cfg%clear_average_set .and. cfg%clear_average)
  end subroutine frame_A

  ! Frame B: bools false (distinguish "set to false" from "absent");
  ! tx_mode as a raw int (9); a negative real.
  subroutine frame_B()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","robust_mode":false,"nagain_flag":false,' //              &
      '"clear_average":false,"tx_mode":9,"dt_tolerance_seconds":-0.5}'
    write(*,'(a)') 'Frame B: bools false, tx_mode=9 (int), negative real'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',                  .not. terr%present)
    call ok('robust_mode set, value false',      cfg%robust_mode_set   .and. (.not. cfg%robust_mode))
    call ok('nagain_flag set, value false',      cfg%nagain_flag_set   .and. (.not. cfg%nagain_flag))
    call ok('clear_average set, value false',    cfg%clear_average_set .and. (.not. cfg%clear_average))
    call ok('tx_mode == 9 (int)',                cfg%tx_mode_set .and. cfg%tx_mode .eq. 9)
    call ok('dt_tolerance_seconds == -0.5',      cfg%dt_tolerance_seconds_set .and. &
         abs(cfg%dt_tolerance_seconds + 0.5d0) .lt. 1.d-9)
  end subroutine frame_B

  ! Frame C: substring non-collision. "mode" is a substring of "tx_mode" and
  ! "min_width"/"min_sync" share the "min_" prefix — the structural key locator
  ! (closing quote in the token + anchor) must keep them distinct, both orderings.
  subroutine frame_C()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b1 =                                      &
      '{"t":"configure","mode":"FT8","tx_mode":"JT9","min_width":3,"min_sync":4}'
    character(len=*), parameter :: b2 =                                      &
      '{"t":"configure","tx_mode":"JT9","mode":"FT8","min_sync":4,"min_width":3}'
    write(*,'(a)') 'Frame C: mode vs tx_mode + min_width vs min_sync non-collision'
    call parse_control_frame(b1, action, cfg, terr)
    call ok('order1: terr not present',  .not. terr%present)
    call ok('order1: mode == 8 (FT8)',   cfg%mode_set    .and. cfg%mode    .eq. 8)
    call ok('order1: tx_mode == 9 (JT9)',cfg%tx_mode_set .and. cfg%tx_mode .eq. 9)
    call ok('order1: min_width == 3',    cfg%min_width_set .and. cfg%min_width .eq. 3)
    call ok('order1: min_sync == 4',     cfg%min_sync_set  .and. cfg%min_sync  .eq. 4)
    call parse_control_frame(b2, action, cfg, terr)
    call ok('order2: terr not present',  .not. terr%present)
    call ok('order2: mode == 8 (FT8)',   cfg%mode_set    .and. cfg%mode    .eq. 8)
    call ok('order2: tx_mode == 9 (JT9)',cfg%tx_mode_set .and. cfg%tx_mode .eq. 9)
    call ok('order2: min_width == 3',    cfg%min_width_set .and. cfg%min_width .eq. 3)
    call ok('order2: min_sync == 4',     cfg%min_sync_set  .and. cfg%min_sync  .eq. 4)
  end subroutine frame_C

  ! Frame D: real key carrying a string -> configure_type_error.
  subroutine frame_D()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","dt_tolerance_seconds":"abc"}'
    write(*,'(a)') 'Frame D: dt_tolerance_seconds:"abc" (string) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',                terr%present)
    call ok('key == dt_tolerance_seconds', trim(terr%key) .eq. 'dt_tolerance_seconds')
    call ok('expected == real',            trim(terr%expected) .eq. 'real')
    call ok('got == string',               trim(terr%got) .eq. 'string')
  end subroutine frame_D

  ! Frame E: int key carrying a string -> configure_type_error.
  subroutine frame_E()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","npts_c0_array":"big"}'
    write(*,'(a)') 'Frame E: npts_c0_array:"big" (string) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',           terr%present)
    call ok('key == npts_c0_array',   trim(terr%key) .eq. 'npts_c0_array')
    call ok('expected == int',        trim(terr%expected) .eq. 'int')
    call ok('got == string',          trim(terr%got) .eq. 'string')
  end subroutine frame_E

  ! Frame F: bool key carrying a number -> configure_type_error.
  subroutine frame_F()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","robust_mode":5}'
    write(*,'(a)') 'Frame F: robust_mode:5 (number) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',       terr%present)
    call ok('key == robust_mode', trim(terr%key) .eq. 'robust_mode')
    call ok('expected == bool',   trim(terr%expected) .eq. 'bool')
    call ok('got == number',      trim(terr%got) .eq. 'number')
  end subroutine frame_F

  ! Frame G: value-vs-key aliasing — the mycall VALUE is the literal "n_2pass"
  ! token, with no real n_2pass key. The anchored locator must NOT mistake the
  ! value for the key: n_2pass stays unset (benign extraction miss) and NO false
  ! type error is raised. (Documents the benign aliasing
  ! behavior for an int key whose token appears as a string value.) NOTE: the
  ! primary anchoring-REGRESSION teeth live in Frame C — a value's closing quote
  ! is always followed by a structural char ('}'/','), so a hypothetical raw-
  ! index regression would classify this value as 'absent' and also not decline;
  ! Frame C (mode-inside-tx_mode, order2) is what actually fails on a raw-index
  ! regression. This frame documents the benign no-false-decline contract.
  subroutine frame_G()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mycall":"n_2pass"}'
    write(*,'(a)') 'Frame G: mycall value == "n_2pass" token (aliasing guard)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present (no false decline)', .not. terr%present)
    call ok('mycall == n_2pass',  cfg%mycall_set .and. trim(cfg%mycall) .eq. 'n_2pass')
    call ok('n_2pass stays UNSET', .not. cfg%n_2pass_set)
  end subroutine frame_G

  ! Frame H: Phase 5 keys absent -> all unset, no error (keys are optional).
  subroutine frame_H()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mode":"FT8"}'
    write(*,'(a)') 'Frame H: Phase 5 keys absent -> unset'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',              .not. terr%present)
    call ok('dt_tolerance_seconds unset',    .not. cfg%dt_tolerance_seconds_set)
    call ok('eme_delay_seconds unset',       .not. cfg%eme_delay_seconds_set)
    call ok('kin_samples unset',             .not. cfg%kin_samples_set)
    call ok('nzhsym_per_period unset',       .not. cfg%nzhsym_per_period_set)
    call ok('npts_c0_array unset',           .not. cfg%npts_c0_array_set)
    call ok('min_width unset',               .not. cfg%min_width_set)
    call ok('min_sync unset',                .not. cfg%min_sync_set)
    call ok('n_2pass unset',                 .not. cfg%n_2pass_set)
    call ok('robust_mode unset',             .not. cfg%robust_mode_set)
    call ok('nagain_flag unset',             .not. cfg%nagain_flag_set)
    call ok('tx_mode unset',                 .not. cfg%tx_mode_set)
    call ok('clear_average unset',           .not. cfg%clear_average_set)
  end subroutine frame_H

  ! Frame I: npts_c0_array feeds JT9 npts8. Values outside the downsam9
  ! FFT input domain are ignored rather than routed to the decoder.
  subroutine frame_I()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b1 = '{"t":"configure","npts_c0_array":-1}'
    character(len=*), parameter :: b2 = '{"t":"configure","npts_c0_array":0}'
    character(len=*), parameter :: b3 = '{"t":"configure","npts_c0_array":81649}'
    character(len=*), parameter :: b4 = '{"t":"configure","npts_c0_array":81648}'
    write(*,'(a)') 'Frame I: npts_c0_array domain guard'
    call parse_control_frame(b1, action, cfg, terr)
    call ok('negative npts_c0_array ignored', .not. terr%present .and. .not. cfg%npts_c0_array_set)
    call parse_control_frame(b2, action, cfg, terr)
    call ok('zero npts_c0_array ignored',     .not. terr%present .and. .not. cfg%npts_c0_array_set)
    call parse_control_frame(b3, action, cfg, terr)
    call ok('oversize npts_c0_array ignored', .not. terr%present .and. .not. cfg%npts_c0_array_set)
    call parse_control_frame(b4, action, cfg, terr)
    call ok('max npts_c0_array accepted',     .not. terr%present .and. cfg%npts_c0_array_set .and. &
         cfg%npts_c0_array .eq. 81648)
  end subroutine frame_I

end program p5_parse_test
