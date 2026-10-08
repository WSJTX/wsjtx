module jtty_receive_context
  use iso_c_binding, only: c_int, c_int16_t, c_int64_t, c_float, c_double, c_char
  use iso_fortran_env, only: int64, real64
  use jtty_fec, only: PAYLOAD_BITS
  use jtty_mdec
  implicit none
  private

  type :: receive_state
     integer :: active_count=0, recent_count=0, pending_count=0, pending_head=1, decode_count=0
     type(message_assembly) :: active(MAX_ACTIVE_MESSAGES)
     type(frame_fingerprint) :: recent(MAX_RECENT_FRAMES)
     type(message_update), allocatable :: pending(:)
  end type receive_state

  type :: receive_retro_state
     logical :: active=.false.
     integer :: count=0, signal=1, step=1
     real :: frequency(MAX_SUBTRACTED)=0.0
     real(real64) :: sync_time(MAX_SUBTRACTED)=0.0_real64
     integer :: payload(PAYLOAD_BITS,MAX_SUBTRACTED)=0
  end type receive_retro_state

  type :: receive_context
     logical :: allocated=.false., running=.false.
     integer :: nsps=384
     integer(int64) :: session_id=0, grid_origin=0, first_search=0, next_search=0
     type(receive_state) :: state
     type(receive_retro_state) :: retro
  end type receive_context

  integer, parameter :: MAX_CONTEXTS=8
  type(receive_context), save :: contexts(MAX_CONTEXTS)

  public :: jtty_rx_create, jtty_rx_destroy, jtty_rx_begin, jtty_rx_process
  public :: jtty_rx_next_required_sample, jtty_rx_next_search_sample
  public :: jtty_rx_take_updates, jtty_rx_take_updates_with_gaps, jtty_rx_end

contains

  logical function valid_handle(handle)
    integer(c_int), intent(in) :: handle

    valid_handle=.false.
    if(handle.lt.1 .or. handle.gt.MAX_CONTEXTS) return
    valid_handle=contexts(handle)%allocated
  end function valid_handle

  ! DSP scratch is shared; exchange only persistent reception state between whole steps.
  subroutine exchange_state(state)
    type(receive_state), intent(inout) :: state
    type(message_assembly) :: active_swap(MAX_ACTIVE_MESSAGES)
    type(frame_fingerprint) :: recent_swap(MAX_RECENT_FRAMES)
    type(message_update), allocatable :: pending_swap(:)
    integer :: value

    active_swap=active_messages
    active_messages=state%active
    state%active=active_swap
    recent_swap=recent_frames
    recent_frames=state%recent
    state%recent=recent_swap
    call move_alloc(pending_updates,pending_swap)
    call move_alloc(state%pending,pending_updates)
    call move_alloc(pending_swap,state%pending)
    value=nactive
    nactive=state%active_count
    state%active_count=value
    value=nrecent
    nrecent=state%recent_count
    state%recent_count=value
    value=npending
    npending=state%pending_count
    state%pending_count=value
    value=pending_first
    pending_first=state%pending_head
    state%pending_head=value
    value=ndecodes
    ndecodes=state%decode_count
    state%decode_count=value
  end subroutine exchange_state

  integer(c_int) function jtty_rx_create() bind(C,name='jtty_rx_create')
    integer :: i

    jtty_rx_create=0
    do i=1,MAX_CONTEXTS
       if(contexts(i)%allocated) cycle
       contexts(i)=receive_context()
       contexts(i)%allocated=.true.
       jtty_rx_create=i
       return
    enddo
  end function jtty_rx_create

  subroutine jtty_rx_destroy(handle) bind(C,name='jtty_rx_destroy')
    integer(c_int), value :: handle

    if(.not.valid_handle(handle)) return
    contexts(handle)=receive_context()
  end subroutine jtty_rx_destroy

  subroutine jtty_rx_begin(handle,session_id,grid_origin,first_search,nsps) bind(C,name='jtty_rx_begin')
    integer(c_int), value :: handle,nsps
    integer(c_int64_t), value :: session_id,grid_origin,first_search
    integer(int64) :: start,step

    if(.not.valid_handle(handle)) return
    call jtty_rx_end(handle,UPDATE_RECEPTION_ENDED)
    if(nsps.ne.240 .and. nsps.ne.320 .and. nsps.ne.384 .and. nsps.ne.480) return
    step=int(59*nsps/4,int64)
    start=max(first_search,grid_origin)
    start=start+modulo(grid_origin-start,step)
    contexts(handle)%session_id=session_id
    contexts(handle)%grid_origin=grid_origin
    contexts(handle)%first_search=start
    contexts(handle)%next_search=start
    contexts(handle)%nsps=nsps
    contexts(handle)%state%active_count=0
    contexts(handle)%state%recent_count=0
    contexts(handle)%state%decode_count=0
    contexts(handle)%retro=receive_retro_state()
    contexts(handle)%running=.true.
  end subroutine jtty_rx_begin

  integer(c_int64_t) function jtty_rx_next_search_sample(handle) bind(C,name='jtty_rx_next_search_sample')
    integer(c_int), value :: handle

    jtty_rx_next_search_sample=-1_c_int64_t
    if(.not.valid_handle(handle)) return
    jtty_rx_next_search_sample=contexts(handle)%next_search
  end function jtty_rx_next_search_sample

  integer(c_int64_t) function jtty_rx_next_required_sample(handle) bind(C,name='jtty_rx_next_required_sample')
    integer(c_int), value :: handle
    integer(int64) :: step

    jtty_rx_next_required_sample=-1_c_int64_t
    if(.not.valid_handle(handle)) return
    step=int(59*contexts(handle)%nsps/4,int64)
    jtty_rx_next_required_sample=max(contexts(handle)%first_search, &
         contexts(handle)%next_search-int(MAX_RETRO_STEPS,int64)*step)
  end function jtty_rx_next_required_sample

  integer(c_int) function jtty_rx_process(handle,pcm,count,first_sample,stop_sample,max_steps, &
       nfa,nfb,f0,ftol) bind(C,name='jtty_rx_process')
    integer(c_int), value :: handle,count,max_steps,nfa,nfb
    integer(c_int16_t), intent(in) :: pcm(*)
    integer(c_int64_t), value :: first_sample,stop_sample
    real(c_float), value :: f0,ftol
    integer :: nframe,nchunk,step,index,signal,retro_step
    integer(int64) :: limit,old_origin,old_first_search,search
    logical :: old_explicit,performed

    jtty_rx_process=-1
    if(.not.valid_handle(handle)) return
    if(.not.contexts(handle)%running .or. count.lt.0) return
    if(first_sample.gt.jtty_rx_next_required_sample(handle)) then
       jtty_rx_process=-2
       return
    endif
    jtty_rx_process=0
    if(max_steps.le.0 .or. count.eq.0) return
    nframe=59*contexts(handle)%nsps
    nchunk=nframe+nframe/4
    step=nframe/4
    limit=min(stop_sample,first_sample+int(count,int64))
    old_origin=sample_origin
    old_first_search=first_search_sample
    old_explicit=explicit_receive_context
    sample_origin=first_sample
    first_search_sample=contexts(handle)%first_search
    explicit_receive_context=.true.
    call exchange_state(contexts(handle)%state)

    do while(jtty_rx_process.lt.max_steps)
       performed=.false.
       do while(contexts(handle)%retro%active .and. .not.performed)
          signal=contexts(handle)%retro%signal
          retro_step=contexts(handle)%retro%step
          if(signal.gt.contexts(handle)%retro%count) then
             contexts(handle)%retro=receive_retro_state()
             contexts(handle)%next_search=contexts(handle)%next_search+int(step,int64)
             exit
          endif
          contexts(handle)%retro%step=retro_step+1
          if(retro_step.gt.MAX_RETRO_STEPS) then
             contexts(handle)%retro%signal=signal+1
             contexts(handle)%retro%step=1
             cycle
          endif
          search=contexts(handle)%next_search-int(retro_step*step,int64)
          if(search.lt.contexts(handle)%first_search) cycle
          index=int(search-first_sample)+1
          if(index.lt.1 .or. index+nchunk-1.gt.count) then
             jtty_rx_process=-2
             exit
          endif
          interferer_pending=.true.
          interferer_f1=contexts(handle)%retro%frequency(signal)
          interferer_tsync=contexts(handle)%retro%sync_time(signal)
          interferer_payload=contexts(handle)%retro%payload(:,signal)
          call jtty_mdecode(index,0,pcm(index),nchunk,contexts(handle)%nsps,-1, &
               nfa,nfb,f0,ftol,4.6)
          jtty_rx_process=jtty_rx_process+1
          performed=.true.
       enddo
       if(jtty_rx_process.lt.0) exit
       if(performed) cycle
       if(contexts(handle)%next_search+int(nchunk,int64).gt.limit) exit
       index=int(contexts(handle)%next_search-first_sample)+1
       call prune_receive_state(sample_time(index),real(nframe)/12000.0)
       interferer_pending=.false.
       call jtty_mdecode(index,0,pcm(index),nchunk,contexts(handle)%nsps,-1, &
            nfa,nfb,f0,ftol,4.6)
       jtty_rx_process=jtty_rx_process+1
       if(nsubtracted.gt.0) then
          contexts(handle)%retro%active=.true.
          contexts(handle)%retro%count=nsubtracted
          contexts(handle)%retro%signal=1
          contexts(handle)%retro%step=1
          contexts(handle)%retro%frequency(1:nsubtracted)=subtracted_f1(1:nsubtracted)
          contexts(handle)%retro%sync_time(1:nsubtracted)=subtracted_tsync(1:nsubtracted)
          contexts(handle)%retro%payload(:,1:nsubtracted)=subtracted_payload(:,1:nsubtracted)
       else
          contexts(handle)%next_search=contexts(handle)%next_search+int(step,int64)
       endif
    enddo

    call exchange_state(contexts(handle)%state)
    sample_origin=old_origin
    first_search_sample=old_first_search
    explicit_receive_context=old_explicit
  end function jtty_rx_process

  subroutine jtty_rx_end(handle,reason) bind(C,name='jtty_rx_end')
    integer(c_int), value :: handle,reason
    integer :: i,terminal

    if(.not.valid_handle(handle)) return
    if(.not.contexts(handle)%running) return
    terminal=UPDATE_RECEPTION_ENDED
    if(reason.eq.UPDATE_EXPIRED) terminal=UPDATE_EXPIRED
    call exchange_state(contexts(handle)%state)
    do i=1,nactive
       call queue_message_update(active_messages(i),.false.,terminal)
    enddo
    call reset_decode_search_state()
    call exchange_state(contexts(handle)%state)
    contexts(handle)%retro=receive_retro_state()
    contexts(handle)%running=.false.
  end subroutine jtty_rx_end

  integer(c_int) function jtty_rx_take_updates(handle,capacity,text,ids,frequencies,starts,latest,terminals,snrs) &
       bind(C,name='jtty_rx_take_updates')
    integer(c_int), value :: handle,capacity
    character(kind=c_char), intent(out) :: text(*)
    integer(c_int64_t), intent(out) :: ids(*)
    real(c_float), intent(out) :: frequencies(*)
    real(c_double), intent(out) :: starts(*),latest(*)
    integer(c_int), intent(out) :: terminals(*)
    integer(c_int), intent(out) :: snrs(*)

    jtty_rx_take_updates=take_pending_updates(handle,capacity,text,ids,frequencies,starts,latest, &
         terminals,snrs)
  end function jtty_rx_take_updates

  ! jtty_rx_take_updates for Fortran callers, plus where each update's text
  ! marks missed frames: gaps(1:ngaps(i),i) as display_message_layout reports.
  integer(c_int) function jtty_rx_take_updates_with_gaps(handle,capacity,text,ids,frequencies, &
       starts,latest,terminals,snrs,gaps,ngaps)
    integer(c_int), intent(in) :: handle,capacity
    character(kind=c_char), intent(out) :: text(*)
    integer(c_int64_t), intent(out) :: ids(*)
    real(c_float), intent(out) :: frequencies(*)
    real(c_double), intent(out) :: starts(*),latest(*)
    integer(c_int), intent(out) :: terminals(*)
    integer(c_int), intent(out) :: snrs(*)
    integer, intent(out) :: gaps(MAX_DISPLAY_GAPS,*),ngaps(*)

    jtty_rx_take_updates_with_gaps=take_pending_updates(handle,capacity,text,ids,frequencies, &
         starts,latest,terminals,snrs,gaps,ngaps)
  end function jtty_rx_take_updates_with_gaps

  integer(c_int) function take_pending_updates(handle,capacity,text,ids,frequencies,starts,latest, &
       terminals,snrs,gaps,ngaps)
    integer(c_int), intent(in) :: handle,capacity
    character(kind=c_char), intent(out) :: text(*)
    integer(c_int64_t), intent(out) :: ids(*)
    real(c_float), intent(out) :: frequencies(*)
    real(c_double), intent(out) :: starts(*),latest(*)
    integer(c_int), intent(out) :: terminals(*)
    integer(c_int), intent(out) :: snrs(*)
    integer, optional, intent(out) :: gaps(MAX_DISPLAY_GAPS,*),ngaps(*)
    character(len=80) :: message
    integer :: message_gaps(MAX_DISPLAY_GAPS),nmessage_gaps
    integer :: i,j,index

    take_pending_updates=0
    if(.not.valid_handle(handle) .or. capacity.le.0) return
    associate(state=>contexts(handle)%state)
       take_pending_updates=min(capacity,state%pending_count)
       do i=1,take_pending_updates
          index=state%pending_head+i-1
          call display_message_layout(state%pending(index)%decoded,message,message_gaps,nmessage_gaps)
          if(present(gaps) .and. present(ngaps)) then
             gaps(:,i)=message_gaps
             ngaps(i)=nmessage_gaps
          endif
          do j=1,80
             text((i-1)*80+j)=message(j:j)
          enddo
          ids(i)=state%pending(index)%message_id
          frequencies(i)=state%pending(index)%f1
          starts(i)=state%pending(index)%start_tsync
          latest(i)=state%pending(index)%latest_tsync
          terminals(i)=state%pending(index)%terminal
          snrs(i)=nint(state%pending(index)%snr)
       enddo
       state%pending_head=state%pending_head+take_pending_updates
       state%pending_count=state%pending_count-take_pending_updates
       if(state%pending_count.eq.0) then
          state%pending_head=1
          if(allocated(state%pending)) then
             if(size(state%pending).gt.MAX_ACTIVE_MESSAGES) then
                deallocate(state%pending)
                allocate(state%pending(MAX_ACTIVE_MESSAGES))
             endif
          endif
       endif
    end associate
  end function take_pending_updates

end module jtty_receive_context
