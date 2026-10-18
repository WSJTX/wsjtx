! Phase 8 parse-unit regression driver. Exercises parse_control_frame for the 5
! Phase 8 string & scaled / derived keys (utc/date/n_trials/candthin_threshold/
! dt_center_seconds), which are MORE than flat value-extract: utc/date are ISO
! strings tokenized into the legacy int (utc->nutc HHMMSS, date->yymmdd), n_trials
! is ENCODED to nranera, and two keys carry a PARSE-TIME VALUE check that emits
! configure_type_error (n_trials not in {10^N,3*10^N}; date year>=2100). This
! driver asserts the derivations, the encoding table (incl. the n_trials=1 ->
! nranera=0 edge), the two value errors, the wrong-TYPE detection, malformed
! ISO silent-skip, that nutc AND utc are BOTH kept at parse (precedence is resolved
! at apply — see run-apply-routing-unit.sh frame_utc_precedence), and value-vs-key
! aliasing. Compiled + run by run-phase8-parse-unit.sh.
!
! SCOPE: this guards the JSON -> configure_fields PARSE. The configure_fields ->
! params APPLY routing (incl. the utc-wins precedence, the /100 scaling, and the
! flat date/n_trials pass-throughs) lives in streaming_apply::apply_configure_fields
! and is asserted by run-apply-routing-unit.sh. Four of the five keys
! are inert on the FT8/JT9 fixtures (date superfox-only, n_trials JT65-only,
! candthin/dtcenter multithread-variant-only), so this parse-unit +
! the apply-routing unit are their coverage; utc is decode-observable (Gates 19/20).
program p8_parse_test
  use streaming_control, only: parse_control_frame, configure_fields,        &
       control_type_error, CTRL_CONFIGURE, CTRL_PARSE_ERR
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
  call frame_H2()
  call frame_I()
  call frame_J()

  write(*,'(a)') '------------------------------------------------------------'
  if (nfail .eq. 0) then
     write(*,'(a)') 'ALL PHASE 8 PARSE-UNIT CHECKS PASSED'
  else
     write(*,'(a,i0,a)') 'PHASE 8 PARSE-UNIT: ', nfail, ' CHECK(S) FAILED'
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

  ! Frame A: all 5 keys present, distinct values, ALL valid. Asserts each
  ! derivation: utc "13:34:30" -> nutc 133430; date "2021-07-03" -> yymmdd 210703
  ! ((2021-2000)*10000+7*100+3); n_trials 1000 -> nranera 6; the two reals stored.
  subroutine frame_A()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","utc":"13:34:30","date":"2021-07-03",' //                  &
      '"n_trials":1000,"candthin_threshold":1.5,"dt_center_seconds":0.2}'
    write(*,'(a)') 'Frame A: all 5 keys valid (utc/date/n_trials/candthin/dtcenter)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('action == CTRL_CONFIGURE', action .eq. CTRL_CONFIGURE)
    call ok('terr not present',          .not. terr%present)
    call ok('utc "13:34:30" -> utc_nutc = 133430', cfg%utc_set .and. cfg%utc_nutc .eq. 133430)
    call ok('date "2021-07-03" -> date_yymmdd = 210703', cfg%date_set .and. cfg%date_yymmdd .eq. 210703)
    call ok('n_trials 1000 -> nranera = 6', cfg%n_trials_set .and. cfg%n_trials_nranera .eq. 6)
    call ok('candthin_threshold == 1.5', cfg%candthin_threshold_set .and. &
         abs(cfg%candthin_threshold - 1.5d0) .lt. 1.0d-10)
    call ok('dt_center_seconds == 0.2', cfg%dt_center_seconds_set .and. &
         abs(cfg%dt_center_seconds - 0.2d0) .lt. 1.0d-10)
  end subroutine frame_A

  ! Frame B: the n_trials -> nranera encoding TABLE. Each value is parsed in
  ! its own frame so an encoder bug surfaces per-value. Covers both legs (10^N and
  ! 3*10^N) and the n_trials=1 -> nranera=0 edge (the legacy quirk).
  subroutine frame_B()
    write(*,'(a)') 'Frame B: n_trials -> nranera encoding table'
    call enc_ok('n_trials 1 -> nranera 0',      1,     .true.,  0)   ! edge case (decoder reads 0 trials)
    call enc_ok('n_trials 3 -> nranera 1',      3,     .true.,  1)
    call enc_ok('n_trials 10 -> nranera 2',     10,    .true.,  2)
    call enc_ok('n_trials 30 -> nranera 3',     30,    .true.,  3)
    call enc_ok('n_trials 100 -> nranera 4',    100,   .true.,  4)
    call enc_ok('n_trials 300 -> nranera 5',    300,   .true.,  5)
    call enc_ok('n_trials 1000 -> nranera 6',   1000,  .true.,  6)
    call enc_ok('n_trials 3000 -> nranera 7',   3000,  .true.,  7)
    call enc_ok('n_trials 10000 -> nranera 8',  10000, .true.,  8)
  end subroutine frame_B

  ! Helper: parse {"t":"configure","n_trials":<n>} and assert set-state + nranera.
  subroutine enc_ok(label, n, want_set, want_nranera)
    character(len=*), intent(in) :: label
    integer,          intent(in) :: n, want_nranera
    logical,          intent(in) :: want_set
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=16) :: ns
    character(len=64) :: b
    write(ns, '(i0)') n
    b = '{"t":"configure","n_trials":' // trim(ns) // '}'
    call parse_control_frame(trim(b), action, cfg, terr)
    if (want_set) then
       call ok(label, (.not. terr%present) .and. cfg%n_trials_set .and. &
            cfg%n_trials_nranera .eq. want_nranera)
    else
       call ok(label, terr%present .and. (.not. cfg%n_trials_set))
    end if
  end subroutine enc_ok

  ! Frame C: n_trials VALUE-invalid (not 10^N/3*10^N) -> configure_type_error. The
  ! JSON type is correct (a number), so this is the schema's first VALUE error (the
  ! envelope reused). Tests 500 (the Gate-21 value), 0, and a negative.
  subroutine frame_C()
    write(*,'(a)') 'Frame C: n_trials value-invalid -> type error'
    call inval_ok('n_trials 500 -> value error',  500)
    call inval_ok('n_trials 0 -> value error',    0)
    call inval_ok('n_trials 50 -> value error',   50)
    call inval_ok('n_trials 7 -> value error',    7)
  end subroutine frame_C

  subroutine inval_ok(label, n)
    character(len=*), intent(in) :: label
    integer,          intent(in) :: n
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=16) :: ns
    character(len=64) :: b
    write(ns, '(i0)') n
    b = '{"t":"configure","n_trials":' // trim(ns) // '}'
    call parse_control_frame(trim(b), action, cfg, terr)
    ! Assert the FULL envelope incl. expected, so the 'int 10^N|3*10^N' constraint
    ! string is guarded at the parse-unit level (not only by Gate 21's cross-file
    ! grep — a brittle coupling).
    call ok(label, terr%present .and. (.not. cfg%n_trials_set) .and. &
         trim(terr%key) .eq. 'n_trials' .and. &
         trim(terr%expected) .eq. 'int 10^N|3*10^N' .and. &
         trim(terr%got) .eq. 'number')
  end subroutine inval_ok

  ! Frame D: date year >= 2100 -> configure_type_error (the YYMMDD encoding
  ! caps at 2099). The parse succeeds (well-formed ISO) but the value is rejected.
  subroutine frame_D()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","date":"2200-01-01"}'
    write(*,'(a)') 'Frame D: date year>=2100 -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',             terr%present)
    call ok('key == date',              trim(terr%key) .eq. 'date')
    call ok('expected == year<2100',    trim(terr%expected) .eq. 'year<2100')
    call ok('got == string',            trim(terr%got) .eq. 'string')
    call ok('date stays UNSET',         .not. cfg%date_set)
  end subroutine frame_D

  ! Frame E: wrong-TYPE detection (distinct from the value checks above). n_trials
  ! as a string and candthin_threshold as a string -> check_type_ rejects (int /
  ! real expected, got string).
  subroutine frame_E()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b1 = '{"t":"configure","n_trials":"abc"}'
    character(len=*), parameter :: b2 = '{"t":"configure","candthin_threshold":"x"}'
    write(*,'(a)') 'Frame E: wrong-type keys (n_trials/candthin as string) -> type error'
    call parse_control_frame(b1, action, cfg, terr)
    call ok('n_trials string: terr present',      terr%present)
    call ok('n_trials string: key == n_trials',   trim(terr%key) .eq. 'n_trials')
    call ok('n_trials string: expected == int',   trim(terr%expected) .eq. 'int')
    call ok('n_trials string: got == string',     trim(terr%got) .eq. 'string')
    call parse_control_frame(b2, action, cfg, terr)
    call ok('candthin string: terr present',          terr%present)
    call ok('candthin string: key == candthin_threshold', trim(terr%key) .eq. 'candthin_threshold')
    call ok('candthin string: expected == real',      trim(terr%expected) .eq. 'real')
    call ok('candthin string: got == string',         trim(terr%got) .eq. 'string')
  end subroutine frame_E

  ! Frame F: malformed ISO strings -> value-domain SILENT SKIP (key left unset, NO
  ! type error — these are well-typed strings, just not parseable HH:MM:SS /
  ! YYYY-MM-DD). Mirrors the bad-mode-name silent skip.
  subroutine frame_F()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","utc":"13:34","date":"2021-07","mode":"FT8"}'
    write(*,'(a)') 'Frame F: malformed ISO (utc "13:34" / date "2021-07") -> silent skip'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present (no false decline)', .not. terr%present)
    call ok('utc "13:34" (no seconds) -> utc UNSET',  .not. cfg%utc_set)
    call ok('date "2021-07" (no day)  -> date UNSET', .not. cfg%date_set)
    call ok('mode still parsed (FT8)', cfg%mode_set .and. cfg%mode .eq. 8)
  end subroutine frame_F

  ! Frame G: nutc AND utc both present -> parse keeps BOTH (the legacy int nutc and
  ! the derived utc_nutc). Precedence (utc wins) is resolved at APPLY, not parse —
  ! the parse-unit only proves both legs are captured (see frame_utc_precedence in
  ! run-apply-routing-unit.sh for the apply-side utc-wins assertion).
  subroutine frame_G()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","nutc":120000,"utc":"13:34:30"}'
    write(*,'(a)') 'Frame G: nutc + utc both kept at parse (precedence is apply-side)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',                .not. terr%present)
    call ok('legacy nutc kept (120000)',       cfg%nutc_set .and. cfg%nutc .eq. 120000)
    call ok('utc derived kept (utc_nutc 133430)', cfg%utc_set .and. cfg%utc_nutc .eq. 133430)
  end subroutine frame_G

  ! Frame H: value-vs-key aliasing — the mycall VALUE is the literal "n_trials"
  ! token (8 chars, fits mycall len=12) with no real n_trials key. The anchored
  ! locator must NOT mistake the string value for the key: n_trials stays unset and
  ! NO false type error fires.
  subroutine frame_H()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mycall":"n_trials"}'
    write(*,'(a)') 'Frame H: mycall value == "n_trials" token (aliasing guard)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present (no false decline)', .not. terr%present)
    call ok('mycall == n_trials',     cfg%mycall_set .and. trim(cfg%mycall) .eq. 'n_trials')
    call ok('n_trials stays UNSET',   .not. cfg%n_trials_set)
  end subroutine frame_H

  subroutine frame_H2()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","mycall":"n_trials","n_trials":1000}'
    write(*,'(a)') 'Frame H2: value token before real n_trials key'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',       .not. terr%present)
    call ok('mycall == n_trials',     cfg%mycall_set .and. trim(cfg%mycall) .eq. 'n_trials')
    call ok('n_trials parsed',        cfg%n_trials_set .and. cfg%n_trials_nranera .eq. 6)
  end subroutine frame_H2

  ! Frame I: Phase 8 keys absent -> all unset, no error (keys are optional).
  subroutine frame_I()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mode":"FT8"}'
    write(*,'(a)') 'Frame I: Phase 8 keys absent -> unset'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',          .not. terr%present)
    call ok('utc unset',                 .not. cfg%utc_set)
    call ok('date unset',                .not. cfg%date_set)
    call ok('n_trials unset',            .not. cfg%n_trials_set)
    call ok('candthin_threshold unset',  .not. cfg%candthin_threshold_set)
    call ok('dt_center_seconds unset',   .not. cfg%dt_center_seconds_set)
  end subroutine frame_I

  ! Frame J: truncated values exercise every scalar getter at end-of-buffer.
  subroutine frame_J()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    write(*,'(a)') 'Frame J: truncated key values at end of frame'

    call parse_control_frame('{"t":', action, cfg, terr)
    call ok('truncated required t -> parse error', action .eq. CTRL_PARSE_ERR)

    call parse_control_frame('{"t":"configure","mode":', action, cfg, terr)
    call ok('truncated string key: action == CTRL_CONFIGURE', action .eq. CTRL_CONFIGURE)
    call ok('truncated string key: terr not present', .not. terr%present)
    call ok('truncated string key: mode unset', .not. cfg%mode_set)

    call parse_control_frame('{"t":"configure","depth":', action, cfg, terr)
    call ok('truncated int key: action == CTRL_CONFIGURE', action .eq. CTRL_CONFIGURE)
    call ok('truncated int key: terr not present', .not. terr%present)
    call ok('truncated int key: depth unset', .not. cfg%depth_set)

    call parse_control_frame('{"t":"configure","trperiod":   ', action, cfg, terr)
    call ok('space-only real key: action == CTRL_CONFIGURE', action .eq. CTRL_CONFIGURE)
    call ok('space-only real key: terr not present', .not. terr%present)
    call ok('space-only real key: trperiod unset', .not. cfg%trperiod_set)

    call parse_control_frame('{"t":"configure","my_call_standard":', action, cfg, terr)
    call ok('truncated bool key: action == CTRL_CONFIGURE', action .eq. CTRL_CONFIGURE)
    call ok('truncated bool key: terr not present', .not. terr%present)
    call ok('truncated bool key: my_call_standard unset', .not. cfg%my_call_standard_set)
  end subroutine frame_J

end program p8_parse_test
