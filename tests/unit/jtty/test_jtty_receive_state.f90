program test_jtty_receive_state
  use iso_fortran_env, only: int64
  use jtty_mdec, only: decode,message_assembly,MAX_ACTIVE_MESSAGES,nactive,active_messages, &
       nrecent,npending,pending_updates,reset_decode_search_state, &
       discard_pending_updates,start_message,append_active_message, &
       remove_active_message,is_recent_frame,prune_receive_state
  implicit none

  real, parameter :: frame_period=2.0
  type(decode) :: candidate
  type(message_assembly) :: accepted_message
  integer(int64) :: first_id,last_id
  integer :: i,index,nrecent_before,npending_before
  logical :: accepted

  call reset_all()

  candidate=make_candidate(1500.0,10.0,'FIRST FRAME',.false.)
  call start_message(candidate,accepted_message,accepted)
  index=nactive
  call expect(accepted .and. index.eq.1 .and. nactive.eq.1, &
       'an open message enters the active store')
  call expect(npending.eq.1 .and. .not.pending_updates(1)%complete, &
       'an open message publishes its initial state')
  call expect(nrecent.eq.1 .and. is_recent_frame(candidate), &
       'an accepted frame enters recent history')
  first_id=active_messages(1)%message_id

  candidate=make_candidate(1500.0,12.0,' COMPLETE',.true.)
  call append_active_message(1,candidate,1,accepted_message,accepted)
  call expect(nactive.eq.0 .and. npending.eq.1, &
       'completion frees active capacity without losing its update')
  call expect(pending_updates(1)%message_id.eq.first_id .and. &
       pending_updates(1)%complete .and. &
       trim(pending_updates(1)%decoded).eq.'FIRST FRAME COMPLETE', &
       'completion coalesces the final state by logical identity')

  call reset_decode_search_state()
  call expect(nactive.eq.0 .and. nrecent.eq.0 .and. npending.eq.1, &
       'a search reset preserves undelivered updates')
  candidate=make_candidate(1510.0,20.0,'AFTER RESET',.true.)
  call start_message(candidate,accepted_message,accepted)
  call expect(accepted .and. nactive.eq.0 .and. npending.eq.2, &
       'a single-frame completion never occupies active capacity')
  call expect(pending_updates(2)%message_id.gt.first_id, &
       'logical identities remain monotonic across search resets')

  call reset_all()
  do i=1,MAX_ACTIVE_MESSAGES
     candidate=make_candidate(1000.0+real(i),real(i),'OPEN',.false.)
     call start_message(candidate,accepted_message,accepted)
     call expect(accepted .and. nactive.eq.i, &
          'active messages fill capacity without replacement')
  enddo
  last_id=active_messages(MAX_ACTIVE_MESSAGES)%message_id
  nrecent_before=nrecent
  npending_before=npending
  candidate=make_candidate(2000.0,100.0,'REFUSED',.false.)
  call start_message(candidate,accepted_message,accepted)
  call expect(.not.accepted, &
       'a full active store declines a new open message')
  call expect(nactive.eq.MAX_ACTIVE_MESSAGES .and. &
       nrecent.eq.nrecent_before .and. npending.eq.npending_before, &
       'a declined message leaves every store unchanged')
  call remove_active_message(7)
  call start_message(candidate,accepted_message,accepted)
  index=nactive
  call expect(accepted .and. nactive.eq.MAX_ACTIVE_MESSAGES, &
       'freeing one active entry restores capacity')
  call expect(active_messages(index)%message_id.eq.last_id+1_int64, &
       'a declined message does not consume a logical identity')

  call reset_all()
  candidate=make_candidate(1500.0,10.0,'HISTORY',.true.)
  call start_message(candidate,accepted_message,accepted)
  call expect(is_recent_frame(candidate), &
       'completion does not remove its frame fingerprint')
  call prune_receive_state(11.54,frame_period)
  call expect(is_recent_frame(candidate), &
       'history remains until the oldest retro window passes it')
  call prune_receive_state(11.56,frame_period)
  call expect(.not.is_recent_frame(candidate), &
       'history expires after the rediscovery horizon')

  call reset_all()
  candidate=make_candidate(1500.0,10.0,'HISTORY',.true.)
  call start_message(candidate,accepted_message,accepted)
  candidate=make_candidate(1504.0,10.01,'NEAR SIMULTANEOUS',.true.)
  call expect(is_recent_frame(candidate), &
       'recent history rejects a near-simultaneous 4 Hz rediscovery')
  candidate=make_candidate(1511.9,10.01,'INSIDE FREQUENCY LIMIT',.true.)
  call expect(is_recent_frame(candidate), &
       'recent history catches just inside 12 Hz')
  candidate=make_candidate(1512.1,10.01,'OUTSIDE FREQUENCY LIMIT',.true.)
  call expect(.not.is_recent_frame(candidate), &
       'recent history allows just outside 12 Hz')
  candidate=make_candidate(1505.0,10.049,'INSIDE TIME LIMIT',.true.)
  call expect(is_recent_frame(candidate), &
       'recent history catches just inside 0.05 seconds')
  candidate=make_candidate(1505.0,10.051,'OUTSIDE TIME LIMIT',.true.)
  call expect(.not.is_recent_frame(candidate), &
       'recent history allows just outside 0.05 seconds')
  candidate=make_candidate(1500.0,12.0,'ADJACENT MESSAGE',.true.)
  call expect(.not.is_recent_frame(candidate), &
       'recent history allows a message one full frame later')

  call reset_all()
  candidate=make_candidate(1500.0,10.0,'PARTIAL',.false.)
  call start_message(candidate,accepted_message,accepted)
  index=nactive
  active_messages(index)%decoded='PARTIAL GROWN'
  active_messages(index)%k=len_trim(active_messages(index)%decoded)
  call prune_receive_state(17.59,frame_period)
  call expect(nactive.eq.1, &
       'an assembly remains active through its continuation horizon')
  call prune_receive_state(17.61,frame_period)
  call expect(nactive.eq.0, &
       'an abandoned assembly expires beyond its continuation horizon')
  call expect(npending.eq.1 .and. &
       trim(pending_updates(1)%decoded).eq.'PARTIAL GROWN' .and. &
       .not.pending_updates(1)%complete, &
       'expiration preserves the newest partial state for delivery')

  print *, 'test_jtty_receive_state: all checks passed'

contains

  function make_candidate(frequency,tsync,text,complete) result(value)
    real, intent(in) :: frequency,tsync
    character(len=*), intent(in) :: text
    logical, intent(in) :: complete
    type(decode) :: value

    value%f1=frequency
    value%tsync=tsync
    value%decoded=text
    value%is_last_frame=complete
  end function make_candidate

  subroutine reset_all()
    call reset_decode_search_state()
    call discard_pending_updates()
  end subroutine reset_all

  subroutine expect(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if(.not.condition) then
       write(*,'(a)') message
       error stop 1
    endif
  end subroutine expect

end program test_jtty_receive_state
