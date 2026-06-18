! Phase 7 parse-unit regression driver. Exercises parse_control_frame for the 6
! Phase 7 diagnostic / sequencing keys (4 ints + 2 bools) + the tricky cases: bool
! true/false (distinguishing "set false" from "absent"), int parse, wrong-type
! detection (int-as-string, bool-as-number), the "mode" substring non-collision
! introduced by mode_changed (it contains "mode", which is also a key and a
! substring of tx_mode), and value-vs-key aliasing.
! Compiled + run by test/run-phase7-parse-unit.sh against lib/streaming_control.f90.
!
! SCOPE: this guards the JSON -> configure_fields PARSE (the module-public entry
! point). The configure_fields -> params APPLY routing lives in the module-public
! streaming_apply::apply_configure_fields and is asserted by
! test/run-apply-routing-unit.sh (which includes the 6 Phase 7 slot asserts). All
! 6 Phase 7 keys are inert on the FT8/JT9 fixtures (five read only
! inside the multithreaded-FT8 block decoder.f90:192..1148 which streaming forces
! off; qso_progress_state reaches the single-pass FT8 decoder but only steers AP
! passes and the fixture has no AP-only decodes), so no decode-output gate can
! assert their values; this parse-unit + the apply-routing unit are their coverage.
program p7_parse_test
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
     write(*,'(a)') 'ALL PHASE 7 PARSE-UNIT CHECKS PASSED'
  else
     write(*,'(a,i0,a)') 'PHASE 7 PARSE-UNIT: ', nfail, ' CHECK(S) FAILED'
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

  ! Frame A: all 6 keys present — 4 ints at distinct values, 2 bools true.
  subroutine frame_A()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","last_tx_seconds_ago":4,"currently_txing":true,' //       &
      '"mode_changed":true,"qso_progress_state":5,"sec_band_changed":7,' //       &
      '"delay_units":6}'
    write(*,'(a)') 'Frame A: all 6 keys present (4 ints distinct, 2 bools true)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('action == CTRL_CONFIGURE', action .eq. CTRL_CONFIGURE)
    call ok('terr not present',          .not. terr%present)
    ! ints
    call ok('last_tx_seconds_ago == 4', cfg%last_tx_seconds_ago_set .and. cfg%last_tx_seconds_ago .eq. 4)
    call ok('qso_progress_state == 5',  cfg%qso_progress_state_set  .and. cfg%qso_progress_state  .eq. 5)
    call ok('sec_band_changed == 7',    cfg%sec_band_changed_set    .and. cfg%sec_band_changed    .eq. 7)
    call ok('delay_units == 6',         cfg%delay_units_set         .and. cfg%delay_units         .eq. 6)
    ! bools (true)
    call ok('currently_txing true',     cfg%currently_txing_set     .and. cfg%currently_txing)
    call ok('mode_changed true',        cfg%mode_changed_set        .and. cfg%mode_changed)
  end subroutine frame_A

  ! Frame B: bools false (distinguish "set false" from "absent"); ints at 0.
  subroutine frame_B()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","currently_txing":false,"mode_changed":false,' //         &
      '"last_tx_seconds_ago":0,"delay_units":0}'
    write(*,'(a)') 'Frame B: bools false (set, not absent); ints 0'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',                  .not. terr%present)
    call ok('currently_txing set, value false',  cfg%currently_txing_set     .and. (.not. cfg%currently_txing))
    call ok('mode_changed set, value false',     cfg%mode_changed_set        .and. (.not. cfg%mode_changed))
    call ok('last_tx_seconds_ago set, value 0',  cfg%last_tx_seconds_ago_set .and. cfg%last_tx_seconds_ago .eq. 0)
    call ok('delay_units set, value 0',          cfg%delay_units_set         .and. cfg%delay_units .eq. 0)
  end subroutine frame_B

  ! Frame C: "mode" substring non-collision. "mode" is itself a key AND a
  ! substring of tx_mode / mode_changed — the structural key locator (quoted token
  ! + anchor) must keep them distinct in BOTH orderings. A raw-index regression
  ! would mis-classify mode_changed as mode (or vice-versa).
  subroutine frame_C()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b1 =                                      &
      '{"t":"configure","mode":"FT8","tx_mode":"JT9","mode_changed":true}'
    character(len=*), parameter :: b2 =                                      &
      '{"t":"configure","mode_changed":true,"tx_mode":"JT9","mode":"FT8"}'
    write(*,'(a)') 'Frame C: mode vs tx_mode vs mode_changed (substring non-collision)'
    call parse_control_frame(b1, action, cfg, terr)
    call ok('order1: terr not present',     .not. terr%present)
    call ok('order1: mode == 8 (FT8)',      cfg%mode_set         .and. cfg%mode    .eq. 8)
    call ok('order1: tx_mode == 9 (JT9)',   cfg%tx_mode_set      .and. cfg%tx_mode .eq. 9)
    call ok('order1: mode_changed true',    cfg%mode_changed_set .and. cfg%mode_changed)
    call parse_control_frame(b2, action, cfg, terr)
    call ok('order2: terr not present',     .not. terr%present)
    call ok('order2: mode == 8 (FT8)',      cfg%mode_set         .and. cfg%mode    .eq. 8)
    call ok('order2: tx_mode == 9 (JT9)',   cfg%tx_mode_set      .and. cfg%tx_mode .eq. 9)
    call ok('order2: mode_changed true',    cfg%mode_changed_set .and. cfg%mode_changed)
  end subroutine frame_C

  ! Frame D: int key carrying a string -> configure_type_error (mirrors the
  ! Gate 17 fixture, last_tx_seconds_ago:"soon").
  subroutine frame_D()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","last_tx_seconds_ago":"soon"}'
    write(*,'(a)') 'Frame D: last_tx_seconds_ago:"soon" (string) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',                  terr%present)
    call ok('key == last_tx_seconds_ago',    trim(terr%key) .eq. 'last_tx_seconds_ago')
    call ok('expected == int',               trim(terr%expected) .eq. 'int')
    call ok('got == string',                 trim(terr%got) .eq. 'string')
  end subroutine frame_D

  ! Frame E: bool key carrying a number -> configure_type_error.
  subroutine frame_E()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","currently_txing":5}'
    write(*,'(a)') 'Frame E: currently_txing:5 (number) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',          terr%present)
    call ok('key == currently_txing', trim(terr%key) .eq. 'currently_txing')
    call ok('expected == bool',      trim(terr%expected) .eq. 'bool')
    call ok('got == number',         trim(terr%got) .eq. 'number')
  end subroutine frame_E

  ! Frame F: value-vs-key aliasing — the mycall VALUE is the literal
  ! "delay_units" token (a Phase 7 key name; 11 chars, fits mycall's len=12), with
  ! no real delay_units key. The anchored locator must NOT mistake the string
  ! value for the key: delay_units stays unset and NO false type error fires
  ! (the benign no-false-decline contract for a Phase 7 key).
  subroutine frame_F()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mycall":"delay_units"}'
    write(*,'(a)') 'Frame F: mycall value == "delay_units" token (aliasing guard)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present (no false decline)', .not. terr%present)
    call ok('mycall == delay_units',  cfg%mycall_set .and. trim(cfg%mycall) .eq. 'delay_units')
    call ok('delay_units stays UNSET', .not. cfg%delay_units_set)
  end subroutine frame_F

  ! Frame G: Phase 7 keys absent -> all unset, no error (keys are optional).
  subroutine frame_G()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mode":"FT8"}'
    write(*,'(a)') 'Frame G: Phase 7 keys absent -> unset'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',           .not. terr%present)
    call ok('last_tx_seconds_ago unset',  .not. cfg%last_tx_seconds_ago_set)
    call ok('currently_txing unset',      .not. cfg%currently_txing_set)
    call ok('mode_changed unset',         .not. cfg%mode_changed_set)
    call ok('qso_progress_state unset',   .not. cfg%qso_progress_state_set)
    call ok('sec_band_changed unset',     .not. cfg%sec_band_changed_set)
    call ok('delay_units unset',          .not. cfg%delay_units_set)
  end subroutine frame_G

end program p7_parse_test
