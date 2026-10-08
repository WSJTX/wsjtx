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

end program test_streaming_control
