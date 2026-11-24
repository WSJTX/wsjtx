! SPDX-License-Identifier: GPL-3.0-or-later
! WSJT-X's a7 rule as lib/streaming_emit.f90 applies it to a decode that
! carries its AP mark as ap, as the FT8 decoder's do (DecodeOutputPlan.cpp
! isFalseDecode and prepareLine): from live audio, an FT8 decode with ap 7
! in the a7 hold is dropped; with the points display on it is kept, hold or
! not, and sent with ap 0; with neither it is kept with its ap.
program test_streaming_emit_a7
  use streaming_emit, only: streaming_emit_set_enabled, streaming_emit_capture, &
       streaming_emit_captured, streaming_emit_decode,                          &
       streaming_emit_set_false_decodes, streaming_emit_set_a7_hold
  implicit none
  ! A pass triggered at 100 s on the audio clock, a hold over it, and none.
  real(8), parameter :: FIRE = 100.d0
  integer :: nfail = 0

  call streaming_emit_set_enabled(.true.)
  call expect('hold, no points display: dropped', .false., .true., '')
  call expect('hold, points display: kept with ap 0', .true., .true., '"ap":0')
  call expect('no hold, points display: ap 0', .true., .false., '"ap":0')
  call expect('no hold, no points display: kept with ap 7', .false., .false., '"ap":7')

  if (nfail .ne. 0) then
     print '(a,i0,a)', 'test_streaming_emit_a7: ', nfail, ' failed'
     error stop 1
  end if
  print '(a)', 'test_streaming_emit_a7: all checks passed'

contains

  ! An FT8 decode with ap 7 from live audio, with no special activity, with
  ! the points display as points and in a hold or not: its line holds mark,
  ! or no line comes when mark is blank.
  subroutine expect(name, points, held, mark)
    character(len=*), intent(in) :: name, mark
    logical,          intent(in) :: points, held
    character(len=:), allocatable :: lines
    logical :: ok

    call streaming_emit_set_false_decodes(.true., .false., .true., points, FIRE)
    if (held) then
       call streaming_emit_set_a7_hold(FIRE - 10.d0, FIRE + 10.d0)
    else
       call streaming_emit_set_a7_hold(huge(1.d0), -huge(1.d0))
    end if
    call streaming_emit_capture()
    call streaming_emit_decode('FT8', 133430, -10, 0.3, 1200, 'K1ABC W9XYZ -10', &
         utc_is_hhmmss=.true., ap=7)
    call streaming_emit_captured(lines)
    if (len(mark) .eq. 0) then
       ok = len(lines) .eq. 0
    else
       ok = index(lines, mark) .gt. 0 .and. index(lines, '"message":"K1ABC W9XYZ -10"') .gt. 0
    end if
    if (ok) then
       print '(2a)', '  PASS  ', name
    else
       print '(4a)', '  FAIL  ', name, ': ', lines
       nfail = nfail + 1
    end if
  end subroutine expect

end program test_streaming_emit_a7
