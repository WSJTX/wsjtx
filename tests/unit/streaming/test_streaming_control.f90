! SPDX-License-Identifier: GPL-3.0-or-later
program test_streaming_control
  use streaming_control, only: parse_control_frame, configure_fields,       &
       control_type_error, CTRL_UNKNOWN, CTRL_CONFIGURE, CTRL_HALT,         &
       CTRL_PARSE_ERR, CTRL_DISCONTINUITY, MODE_JTTY
  implicit none

  integer :: nfail = 0
  character(len=12) :: code

  call expect_action('{"t":"discontinuity"}', CTRL_DISCONTINUITY)
  call expect_action('{"t":"halt"}', CTRL_HALT)
  call expect_action('{"t":"configure","rxfreq":1500}', CTRL_CONFIGURE)
  call expect_action('{"t":"a_future_control"}', CTRL_UNKNOWN)
  call expect_action('{"t":"discontinuities"}', CTRL_UNKNOWN)
  call expect_action('{"t":"discontinuit"}', CTRL_UNKNOWN)
  call expect_action('{"x":"discontinuity"}', CTRL_PARSE_ERR)

  ! JTTY is selected by its name only, and only by "mode".
  call expect_mode('{"t":"configure","mode":"JTTY"}', .true., MODE_JTTY)
  call expect_mode('{"t":"configure","mode":"jtty"}', .true., MODE_JTTY)
  call expect_mode('{"t":"configure","mode":"FT8"}', .true., 8)
  write(code, '(i0)') MODE_JTTY
  call expect_mode('{"t":"configure","mode":' // trim(code) // '}', .false., 0)
  call expect_tx_mode_unset('{"t":"configure","tx_mode":"JTTY"}')

  ! String values decode JSON escapes to UTF-8 (RFC 8259 section 7, RFC 3629).
  call expect_call('{"t":"configure","mycall":"K1\/ABC"}', 'K1/ABC')
  call expect_call('{"t":"configure","mycall":"AB\\","mygrid":"EM18"}', 'AB\', 'EM18')
  call expect_call('{"t":"configure","mycall":"A\"B","mygrid":"EM18"}', 'A"B', 'EM18')
  call expect_call('{"t":"configure","mycall":"\b\f\n\r\t"}',                 &
       achar(8) // achar(12) // achar(10) // achar(13) // achar(9))
  call expect_call('{"t":"configure","mycall":"\u004B1ABC"}', 'K1ABC')
  call expect_call('{"t":"configure","mycall":"\u00e9"}', char(195) // char(169))
  call expect_call('{"t":"configure","mycall":"\u20AC"}',                     &
       char(226) // char(130) // char(172))
  call expect_call('{"t":"configure","mycall":"\ud83d\ude00"}',               &
       char(240) // char(159) // char(152) // char(128))
  call expect_call('{"t":"configure","mycall":"\uD83D\uDE00"}',               &
       char(240) // char(159) // char(152) // char(128))
  call expect_call('{"t":"configure","mycall":"A\u0000B"}', 'A' // achar(0) // 'B')
  ! The bounds of each UTF-8 length and of the surrogates, and every hex digit case.
  call expect_call('{"t":"configure","mycall":"\u0080"}', char(194) // char(128))
  call expect_call('{"t":"configure","mycall":"\u07ff"}', char(223) // char(191))
  call expect_call('{"t":"configure","mycall":"\u0800"}', char(224) // char(160) // char(128))
  call expect_call('{"t":"configure","mycall":"\uFFFF"}', char(239) // char(191) // char(191))
  call expect_call('{"t":"configure","mycall":"\uabcd"}', char(234) // char(175) // char(141))
  call expect_call('{"t":"configure","mycall":"\ud800\udc00"}',              &
       char(240) // char(144) // char(128) // char(128))
  call expect_call('{"t":"configure","mycall":"\uDBFF\uDFFF"}',              &
       char(244) // char(143) // char(191) // char(191))
  call expect_action('{"t":"ha\u006ct"}', CTRL_HALT)
  call expect_action('{"t":"ha\x"}', CTRL_PARSE_ERR)
  call expect_mode('{"t":"configure","mode":"JT\u0054Y"}', .true., MODE_JTTY)
  ! An escaped quote neither ends the value nor starts a key.
  call expect_call('{"t":"configure","mycall":"A\",\"mygrid\":\"XX","mygrid":"EM18"}', &
       'A","mygrid":', 'EM18')
  ! A string that does not decode is unreadable: the key stays unset.
  call expect_call('{"t":"configure","mycall":"A\x"}', '')
  call expect_call('{"t":"configure","mycall":"A\u12"}', '')
  call expect_call('{"t":"configure","mycall":"A\u12G4"}', '')
  call expect_call('{"t":"configure","mycall":"\ud83d"}', '')
  call expect_call('{"t":"configure","mycall":"\ude00"}', '')
  call expect_call('{"t":"configure","mycall":"\ud83dA"}', '')
  call expect_call('{"t":"configure","mycall":"\ud83d\u0041"}', '')
  call expect_call('{"t":"configure","mycall":"\ud83d\\de00"}', '')
  call expect_call('{"t":"configure","mycall":"\udfff"}', '')
  ! Frames that end inside an escape or a string.
  call expect_call('{"t":"configure","mycall":"A\u12', '')
  call expect_call('{"t":"configure","mycall":"\ud83d', '')
  call expect_call('{"t":"configure","mycall":"A\', '')
  call expect_call('{"t":"configure","mycall":"ABC}', '')
  call expect_call('{"t":"configure","mycall":"' // repeat('K', 200) // '"}', repeat('K', 12))

  if (nfail .eq. 0) then
     print '(a)', 'ALL STREAMING CONTROL CHECKS PASSED'
  else
     print '(a,i0,a)', 'STREAMING CONTROL: ', nfail, ' CHECK(S) FAILED'
     call exit(1)
  end if

contains

  subroutine expect_action(frame, expected)
    character(len=*), intent(in) :: frame
    integer,          intent(in) :: expected
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action

    call parse_control_frame(frame, action, cfg, terr)
    if (action .eq. expected) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a,a,i0)', '  FAIL  ', frame, ' -> action ', action
       nfail = nfail + 1
    end if
  end subroutine expect_action

  ! set: the frame selects mode; otherwise its mode is reported invalid.
  subroutine expect_mode(frame, set, mode)
    character(len=*), intent(in) :: frame
    logical,          intent(in) :: set
    integer,          intent(in) :: mode
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action

    call parse_control_frame(frame, action, cfg, terr)
    if (action .eq. CTRL_CONFIGURE .and. .not. terr%present .and.            &
         (cfg%mode_set .eqv. set) .and. (cfg%mode_invalid .neqv. set) .and.  &
         (.not. set .or. cfg%mode .eq. mode)) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a,a,l1,a,i0)', '  FAIL  ', frame, ' -> mode_set ', cfg%mode_set, &
            ' mode ', cfg%mode
       nfail = nfail + 1
    end if
  end subroutine expect_mode

  subroutine expect_tx_mode_unset(frame)
    character(len=*), intent(in) :: frame
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action

    call parse_control_frame(frame, action, cfg, terr)
    if (action .eq. CTRL_CONFIGURE .and. .not. terr%present .and. .not. cfg%tx_mode_set) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a,a,i0)', '  FAIL  ', frame, ' -> tx_mode ', cfg%tx_mode
       nfail = nfail + 1
    end if
  end subroutine expect_tx_mode_unset

  ! The frame sets mycall to these bytes ('' expects it unset), and mygrid
  ! to grid when one is given; no type error either way.
  subroutine expect_call(frame, mycall, grid)
    character(len=*), intent(in)           :: frame, mycall
    character(len=*), intent(in), optional :: grid
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action, k
    logical :: ok

    call parse_control_frame(frame, action, cfg, terr)
    ok = action .eq. CTRL_CONFIGURE .and. .not. terr%present .and.            &
         (cfg%mycall_set .eqv. len(mycall) .gt. 0) .and. cfg%mycall .eq. mycall
    if (present(grid)) ok = ok .and. cfg%mygrid_set .and. cfg%mygrid .eq. grid
    if (ok) then
       print '(a,a)', '  PASS  ', frame
    else
       print '(a,a,a,l1,a,12(1x,i0))', '  FAIL  ', frame, ' -> mycall_set ',   &
            cfg%mycall_set, ' bytes', [(iachar(cfg%mycall(k:k)), k = 1, 12)]
       nfail = nfail + 1
    end if
  end subroutine expect_call

end program test_streaming_control
