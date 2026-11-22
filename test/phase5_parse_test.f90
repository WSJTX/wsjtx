! Exercise parsing of the Phase 5 decoder-tuning keys, including wrong
! types, absent keys, and values that happen to match key names.
! Apply routing is covered by test/run-apply-routing-unit.sh.
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

  ! Frame A: all nine keys, bools true.
  subroutine frame_A()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","dt_tolerance_seconds":0.1,"eme_delay_seconds":1.5,' //   &
      '"kin_samples":64800,"nzhsym_per_period":50,' //      &
      '"min_width":1,"min_sync":2,"n_2pass":1,' //             &
      '"nagain_flag":true,"clear_average":true}'
    write(*,'(a)') 'Frame A: all nine keys present, bools true'
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
    call ok('min_width == 1',              cfg%min_width_set .and. cfg%min_width .eq. 1)
    call ok('min_sync == 2',               cfg%min_sync_set  .and. cfg%min_sync  .eq. 2)
    call ok('n_2pass == 1',                cfg%n_2pass_set   .and. cfg%n_2pass   .eq. 1)
    call ok('nagain_flag == true',         cfg%nagain_flag_set   .and. cfg%nagain_flag)
    call ok('clear_average == true',       cfg%clear_average_set .and. cfg%clear_average)
  end subroutine frame_A

  ! Frame B: explicitly false booleans and a negative real.
  subroutine frame_B()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","nagain_flag":false,' //              &
      '"clear_average":false,"dt_tolerance_seconds":-0.5}'
    write(*,'(a)') 'Frame B: bools false, negative real'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',                  .not. terr%present)
    call ok('nagain_flag set, value false',      cfg%nagain_flag_set   .and. (.not. cfg%nagain_flag))
    call ok('clear_average set, value false',    cfg%clear_average_set .and. (.not. cfg%clear_average))
    call ok('dt_tolerance_seconds == -0.5',      cfg%dt_tolerance_seconds_set .and. &
         abs(cfg%dt_tolerance_seconds + 0.5d0) .lt. 1.d-9)
  end subroutine frame_B

  ! Frame C: min_width and min_sync are distinct in both orders.
  subroutine frame_C()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b1 =                                      &
      '{"t":"configure","mode":"FT8","min_width":3,"min_sync":4}'
    character(len=*), parameter :: b2 =                                      &
      '{"t":"configure","mode":"FT8","min_sync":4,"min_width":3}'
    write(*,'(a)') 'Frame C: min_width and min_sync in both orders'
    call parse_control_frame(b1, action, cfg, terr)
    call ok('order1: terr not present',  .not. terr%present)
    call ok('order1: mode == 8 (FT8)',   cfg%mode_set    .and. cfg%mode    .eq. 8)
    call ok('order1: min_width == 3',    cfg%min_width_set .and. cfg%min_width .eq. 3)
    call ok('order1: min_sync == 4',     cfg%min_sync_set  .and. cfg%min_sync  .eq. 4)
    call parse_control_frame(b2, action, cfg, terr)
    call ok('order2: terr not present',  .not. terr%present)
    call ok('order2: mode == 8 (FT8)',   cfg%mode_set    .and. cfg%mode    .eq. 8)
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
    character(len=*), parameter :: b = '{"t":"configure","min_width":"big"}'
    write(*,'(a)') 'Frame E: min_width:"big" (string) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',           terr%present)
    call ok('key == min_width',   trim(terr%key) .eq. 'min_width')
    call ok('expected == int',        trim(terr%expected) .eq. 'int')
    call ok('got == string',          trim(terr%got) .eq. 'string')
  end subroutine frame_E

  ! Frame F: bool key carrying a number -> configure_type_error.
  subroutine frame_F()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","clear_average":5}'
    write(*,'(a)') 'Frame F: clear_average:5 (number) -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',       terr%present)
    call ok('key == clear_average', trim(terr%key) .eq. 'clear_average')
    call ok('expected == bool',   trim(terr%expected) .eq. 'bool')
    call ok('got == number',      trim(terr%got) .eq. 'number')
  end subroutine frame_F

  ! Frame G: a value matching a key name does not set that key.
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
    call ok('min_width unset',               .not. cfg%min_width_set)
    call ok('min_sync unset',                .not. cfg%min_sync_set)
    call ok('n_2pass unset',                 .not. cfg%n_2pass_set)
    call ok('nagain_flag unset',             .not. cfg%nagain_flag_set)
    call ok('clear_average unset',           .not. cfg%clear_average_set)
  end subroutine frame_H

  ! Retired keys are ignored, even when their values would have failed type checks.
  subroutine frame_I()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                      &
      '{"t":"configure","jt65_jt9_split_hz":"wide","npts_c0_array":"big",' // &
      '"robust_mode":5,"tx_mode":true,"min_sync":3}'
    write(*,'(a)') 'Frame I: retired keys are ignored'
    call parse_control_frame(b, action, cfg, terr)
    call ok('action == CTRL_CONFIGURE', action .eq. CTRL_CONFIGURE)
    call ok('terr not present', .not. terr%present)
    call ok('min_sync == 3', cfg%min_sync_set .and. cfg%min_sync .eq. 3)
  end subroutine frame_I

end program p5_parse_test
