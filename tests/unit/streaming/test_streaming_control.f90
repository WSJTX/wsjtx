! SPDX-License-Identifier: GPL-3.0-or-later
program test_streaming_control
  use streaming_control, only: parse_control_frame, configure_fields,       &
       control_type_error, CTRL_UNKNOWN, CTRL_CONFIGURE, CTRL_HALT,         &
       CTRL_PARSE_ERR, CTRL_DISCONTINUITY
  implicit none

  integer :: nfail = 0

  call expect_action('{"t":"discontinuity"}', CTRL_DISCONTINUITY)
  call expect_action('{"t":"halt"}', CTRL_HALT)
  call expect_action('{"t":"configure","rxfreq":1500}', CTRL_CONFIGURE)
  call expect_action('{"t":"a_future_control"}', CTRL_UNKNOWN)
  call expect_action('{"t":"discontinuities"}', CTRL_UNKNOWN)
  call expect_action('{"t":"discontinuit"}', CTRL_UNKNOWN)
  call expect_action('{"x":"discontinuity"}', CTRL_PARSE_ERR)

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

end program test_streaming_control
