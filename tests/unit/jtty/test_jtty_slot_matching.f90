program test_jtty_slot_matching

  use jtty_mdec, only: decode, classify_slot_candidate
  implicit none

  real, parameter :: frame_period=2.0
  type(decode) :: existing,candidate
  logical :: match,is_window_dupe,is_history_dupe
  integer :: failures

  failures=0
  call initialize_slot(existing)

  candidate%f1=1501.0
  candidate%tsync=existing%tsync+frame_period
  candidate%decoded='CONTINUATION'
  call classify_slot_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,is_history_dupe)
  call expect(match .and. .not.is_window_dupe .and. .not.is_history_dupe, &
       'an open slot accepts the next frame',failures)

  candidate%f1=1505.0
  candidate%tsync=existing%tsync+frame_period/4.0
  candidate%decoded='SHIFTED REDISCOVERY'
  call classify_slot_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,is_history_dupe)
  call expect(match .and. is_window_dupe .and. .not.is_history_dupe, &
       'an open slot rejects a quarter-frame rediscovery',failures)

  candidate%f1=1505.0
  candidate%tsync=existing%tsync+frame_period
  candidate%decoded='WIDE CONTINUATION'
  call classify_slot_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,is_history_dupe)
  call expect(match .and. .not.is_window_dupe .and. .not.is_history_dupe, &
       'an open slot accepts a 5 Hz next frame',failures)

  existing%is_last_frame=.true.
  candidate%f1=existing%frame_f1(1)
  candidate%tsync=existing%frame_tsync(1)
  candidate%decoded='EXACT REDISCOVERY'
  call classify_slot_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,is_history_dupe)
  call expect(match .and. .not.is_window_dupe .and. is_history_dupe, &
       'a completed slot rejects an exact frame rediscovery',failures)

  candidate%f1=existing%f1
  candidate%tsync=existing%tsync+frame_period
  candidate%decoded='ADJACENT NEW MESSAGE'
  call classify_slot_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,is_history_dupe)
  call expect(.not.match .and. .not.is_window_dupe .and. &
       .not.is_history_dupe, &
       'a completed slot allows an adjacent message',failures)

  if(failures.ne.0) then
     write(*,'(a,i0)') 'test_jtty_slot_matching: failures=',failures
     stop 1
  endif
  write(*,'(a)') 'test_jtty_slot_matching: all checks passed'

contains

  subroutine initialize_slot(value)
    type(decode), intent(out) :: value

    value%f1=1500.0
    value%tsync=10.0
    value%decoded='OPEN MESSAGE'
    value%is_last_frame=.false.
    value%nframes_merged=2
    value%frame_f1(1:2)=(/1500.0,1500.0/)
    value%frame_tsync(1:2)=(/8.0,10.0/)
  end subroutine initialize_slot

  subroutine expect(condition,description,count)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    integer, intent(inout) :: count

    if(.not.condition) then
       count=count+1
       write(*,'(a)') 'FAIL: '//description
    endif
  end subroutine expect

end program test_jtty_slot_matching
