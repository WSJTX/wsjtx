program test_jtty_message_matching
  use jtty_mdec, only: decode,message_assembly,classify_active_candidate
  implicit none

  real, parameter :: frame_period=2.0
  type(message_assembly) :: existing
  type(decode) :: candidate
  logical :: match,is_window_dupe
  integer :: failures,nframes_gap

  failures=0
  existing%f1=1500.0
  existing%tsync=10.0
  existing%decoded='OPEN MESSAGE'

  candidate%f1=1501.0
  candidate%tsync=existing%tsync+frame_period
  candidate%decoded='CONTINUATION'
  call classify_active_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,nframes_gap)
  call expect(match .and. .not.is_window_dupe .and. nframes_gap.eq.1, &
       'an active message accepts the next frame',failures)

  candidate%f1=1505.0
  candidate%tsync=existing%tsync+frame_period/4.0
  candidate%decoded='SHIFTED REDISCOVERY'
  call classify_active_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,nframes_gap)
  call expect(match .and. is_window_dupe, &
       'an active message rejects a quarter-frame rediscovery',failures)

  candidate%f1=1505.0
  candidate%tsync=existing%tsync+frame_period
  candidate%decoded='WIDE CONTINUATION'
  call classify_active_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,nframes_gap)
  call expect(match .and. .not.is_window_dupe, &
       'an active message accepts a 5 Hz next frame',failures)

  candidate%f1=1501.0
  candidate%tsync=existing%tsync+2*frame_period
  candidate%decoded='ONE MISSED FRAME'
  call classify_active_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,nframes_gap)
  call expect(match .and. .not.is_window_dupe .and. nframes_gap.eq.2, &
       'an active message bridges one missed frame',failures)

  candidate%f1=1501.0
  candidate%tsync=existing%tsync+3*frame_period
  candidate%decoded='TWO MISSED FRAMES'
  call classify_active_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,nframes_gap)
  call expect(match .and. .not.is_window_dupe .and. nframes_gap.eq.3, &
       'an active message bridges two missed frames',failures)

  candidate%f1=1501.0
  candidate%tsync=existing%tsync+4*frame_period
  candidate%decoded='THREE MISSED FRAMES'
  call classify_active_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,nframes_gap)
  call expect(.not.match, &
       'an active message does not bridge beyond the continuation limit', &
       failures)

  candidate%f1=1505.0
  candidate%tsync=existing%tsync+3*frame_period/4.0
  candidate%decoded='LAST RETRO STEP'
  call classify_active_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,nframes_gap)
  call expect(match .and. is_window_dupe, &
       'the final actual retro step is classified as rediscovery',failures)

  candidate%tsync=existing%tsync+5*frame_period/4.0
  candidate%decoded='OUTSIDE RETRO HORIZON'
  call classify_active_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,nframes_gap)
  call expect(.not.match, &
       'a later quarter-phase frame is not mistaken for rediscovery',failures)

  if(failures.ne.0) then
     write(*,'(a,i0)') 'test_jtty_message_matching: failures=',failures
     stop 1
  endif
  write(*,'(a)') 'test_jtty_message_matching: all checks passed'

contains

  subroutine expect(condition,description,count)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    integer, intent(inout) :: count

    if(.not.condition) then
       count=count+1
       write(*,'(a)') 'FAIL: '//description
    endif
  end subroutine expect

end program test_jtty_message_matching
