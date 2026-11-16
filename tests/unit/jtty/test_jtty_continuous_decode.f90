program test_jtty_continuous_decode
  use iso_c_binding, only: c_int, c_int16_t, c_int64_t, c_float, c_double, c_char
  use jtty_receive_context
  use jtty_mdec, only: UPDATE_GROWING, UPDATE_COMPLETE, UPDATE_EXPIRED, UPDATE_RECEPTION_ENDED
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mod, only: MAX_FRAMES
  implicit none

  integer, parameter :: nsps=384, rate=12000, step=59*nsps/4, block=3456
  integer, parameter :: max_events=256
  integer, parameter :: review_interval=16*rate
  integer(c_int64_t), parameter :: high_origin=2_c_int64_t**40
  character(len=*), parameter :: wanted='WB9XYZ 599 0123'
  character(len=*), parameter :: overlap_strong='MAYBE YOU SHOULD HELP'
  character(len=*), parameter :: overlap_weak='CLAUDE CO 3 MIN TRANSITION'
  type :: update_event
     integer(c_int64_t) :: id=0
     character(len=80) :: text=''
     real(c_double) :: start=0,latest=0
     real(c_float) :: frequency=0
     integer(c_int) :: terminal=0
  end type update_event
  type(update_event) :: reference(max_events),rotated(max_events),interleaved(max_events)
  integer(c_int16_t), allocatable :: pcm(:),partial(:)
  integer :: nreference,nrotated,ninterleaved

  allocate(pcm(200*rate))
  pcm=0
  call add_message(pcm,2401,.false.)
  call add_message(pcm,374*step+1,.true.)

  call run_stream(pcm,0_c_int64_t,0,.false.,reference,nreference)
  call run_stream(pcm,0_c_int64_t,180*rate,.false.,rotated,nrotated)
  call compare_events(reference,nreference,rotated,nrotated,0.0_c_double)
  call run_stream(pcm,high_origin,7*rate,.true.,interleaved,ninterleaved)
  call compare_events(reference,nreference,interleaved,ninterleaved,real(high_origin,c_double)/rate)
  call expect(count_complete(reference,nreference).eq.2, &
       'strong and damaged-sync messages complete inside and across the 180-second boundary')
  call expect(count_growth(reference,nreference).ge.2,'multi-frame messages publish growing updates')

  allocate(partial(15*rate))
  partial=0
  partial(1:3*rate)=pcm(1:3*rate)
  call check_terminal(partial,3*rate,UPDATE_RECEPTION_ENDED)
  call check_terminal(partial,size(partial),UPDATE_EXPIRED)

  pcm=0
  call mix_message(pcm,2401,overlap_strong,1500.0,14000.0)
  call mix_message(pcm,7201,overlap_weak,1504.0,12000.0)
  call mix_message(pcm,374*step+2401,overlap_strong,1500.0,14000.0)
  call mix_message(pcm,374*step+7201,overlap_weak,1504.0,12000.0)
  call run_stream(pcm,0_c_int64_t,0,.false.,reference,nreference,2800)
  call expect_overlap(reference,nreference)
  call run_stream(pcm,0_c_int64_t,180*rate,.false.,rotated,nrotated,2800)
  call compare_events(reference,nreference,rotated,nrotated,0.0_c_double)
  call run_stream(pcm,high_origin,7*rate,.true.,interleaved,ninterleaved,2800)
  call compare_events(reference,nreference,interleaved,ninterleaved,real(high_origin,c_double)/rate)

  print *, 'test_jtty_continuous_decode: all checks passed'
  deallocate(pcm,partial)

contains

  subroutine add_message(samples,start,damaged_sync)
    integer(c_int16_t), intent(inout) :: samples(:)
    integer, intent(in) :: start
    logical, intent(in) :: damaged_sync
    integer :: tones(MAX_FRAMES*(size(is13)+TOTAL_K)),qrm_tones(size(is13)+TOTAL_K)
    integer :: symbols,n,qrm_symbols,i,offset,span,value
    real, allocatable :: wave(:),qrm(:),combined(:)
    complex, allocatable :: analytic(:)
    character(len=80) :: message

    message=wanted
    call genjtty(message,tones,symbols)
    call expect(symbols.eq.3*59,'fixture retains its three-frame encoding')
    if(damaged_sync) tones(60:60+size(is13)-1)=mod(is13+2,4)
    n=symbols*nsps
    allocate(wave(n),analytic(n+59*nsps),combined(n+59*nsps))
    call gen_jttywave(tones,symbols,nsps,2.0,12000.0,1500.0,analytic,wave,0,n)
    combined=0
    combined(1:n)=wave
    if(damaged_sync) then
       message='N2PPI'
       call genjtty(message,qrm_tones,qrm_symbols)
       allocate(qrm(qrm_symbols*nsps))
       call gen_jttywave(qrm_tones,qrm_symbols,nsps,2.0,12000.0,1540.0,analytic,qrm,0,size(qrm))
       offset=1
       do while(offset.le.size(combined))
          span=min(size(qrm),size(combined)-offset+1)
          combined(offset:offset+span-1)=combined(offset:offset+span-1)+0.3*qrm(1:span)
          offset=offset+span
       enddo
    endif
    do i=1,size(combined)
       value=nint(30000.0*combined(i))
       samples(start+i-1)=int(max(-32767,min(32767,value)),c_int16_t)
    enddo
  end subroutine add_message

  subroutine mix_message(samples,start,text,frequency,amplitude)
    integer(c_int16_t), intent(inout) :: samples(:)
    integer, intent(in) :: start
    character(len=*), intent(in) :: text
    real, intent(in) :: frequency,amplitude
    integer :: tones(MAX_FRAMES*(size(is13)+TOTAL_K)),symbols,n,i,mixed
    real, allocatable :: wave(:)
    complex, allocatable :: analytic(:)
    character(len=80) :: message

    message=text
    call genjtty(message,tones,symbols)
    n=symbols*nsps
    allocate(wave(n),analytic(n))
    call gen_jttywave(tones,symbols,nsps,2.0,12000.0,frequency,analytic,wave,0,n)
    call expect(start+n-1+59*nsps.le.size(samples), &
         'the overlapping transmission and final search/retro padding fit the fixture')
    do i=1,n
       mixed=int(samples(start+i-1))+nint(amplitude*wave(i))
       samples(start+i-1)=int(max(-32767,min(32767,mixed)),c_int16_t)
    enddo
  end subroutine mix_message

  subroutine run_stream(samples,origin,storage_size,review,events,event_count,high_frequency)
    integer(c_int16_t), intent(in) :: samples(:)
    integer(c_int64_t), intent(in) :: origin
    integer, intent(in) :: storage_size
    logical, intent(in) :: review
    type(update_event), intent(out) :: events(:)
    integer, intent(out) :: event_count
    integer, optional, intent(in) :: high_frequency
    integer(c_int) :: live,manual,processed,ignored
    integer :: available,first,rotations,discard_count,nfb,review_restarts
    integer :: next_review_sample,last_review_restart
    integer(c_int64_t) :: required
    type(update_event) :: discarded(max_events)

    event_count=0
    nfb=1499
    if(present(high_frequency)) nfb=high_frequency
    rotations=0
    review_restarts=0
    next_review_sample=0
    last_review_restart=0
    first=1
    live=jtty_rx_create()
    call expect(live.ne.0,'live decoder context is available')
    call jtty_rx_begin(live,1_c_int64_t,origin,origin,nsps)
    manual=0
    if(review) then
       manual=jtty_rx_create()
       call expect(manual.ne.0,'review has independent decoder state')
       call jtty_rx_begin(manual,2_c_int64_t,0_c_int64_t,0_c_int64_t,nsps)
    endif
    available=0
    do while(available.lt.size(samples))
       available=min(available+block,size(samples))
       do
          processed=jtty_rx_process(live,samples(first:),available-first+1, &
               origin+int(first-1,c_int64_t),origin+int(available,c_int64_t),1,200,nfb,1500.0,50.0)
          call expect(processed.ge.0,'retained audio covers the forward and retro search windows')
          call collect(live,events,event_count)
          if(processed.eq.0) exit
          if(review .and. available.ge.next_review_sample) then
             ignored=jtty_rx_process(manual,samples,9*rate,0_c_int64_t,int(9*rate,c_int64_t), &
                  1,200,nfb,1500.0,50.0)
             call expect(ignored.ge.0,'review decoding remains independently valid')
             discard_count=0
             call collect(manual,discarded,discard_count)
             if(ignored.eq.0) then
                call jtty_rx_end(manual,UPDATE_RECEPTION_ENDED)
                discard_count=0
                call collect(manual,discarded,discard_count)
                call jtty_rx_begin(manual,2_c_int64_t,0_c_int64_t,0_c_int64_t,nsps)
                review_restarts=review_restarts+1
                last_review_restart=available
                next_review_sample=next_review_sample+review_interval
             endif
          endif
       enddo
       if(storage_size.gt.0 .and. available-first+1.ge.storage_size) then
          required=jtty_rx_next_required_sample(live)-origin
          if(required.gt.int(first-1,c_int64_t)) then
             first=int(required)+1
             rotations=rotations+1
          endif
       endif
    enddo
    if(storage_size.gt.0) call expect(rotations.gt.0,'the fixture actually rotates storage')
    if(storage_size.eq.7*rate) call expect(rotations.gt.20,'short storage repeats many rotations')
    if(review) call expect(review_restarts.gt.10,'review decoding remains active through later storage boundaries')
    if(review) call expect(last_review_restart.ge.180*rate,'review decoding continues past the 180-second boundary')
    call jtty_rx_end(live,UPDATE_RECEPTION_ENDED)
    call collect(live,events,event_count)
    call jtty_rx_destroy(live)
    if(manual.ne.0) call jtty_rx_destroy(manual)
  end subroutine run_stream

  subroutine collect(handle,events,event_count)
    integer(c_int), intent(in) :: handle
    type(update_event), intent(inout) :: events(:)
    integer, intent(inout) :: event_count
    character(kind=c_char) :: text(80*30)
    integer(c_int64_t) :: ids(30)
    real(c_float) :: frequencies(30)
    real(c_double) :: starts(30),latest(30)
    integer(c_int) :: terminals(30),snrs(30),n
    integer :: i,j

    do
       n=jtty_rx_take_updates(handle,30,text,ids,frequencies,starts,latest,terminals,snrs)
       do i=1,n
          call expect(event_count.lt.size(events),'update collection remains bounded')
          event_count=event_count+1
          do j=1,80
             events(event_count)%text(j:j)=text((i-1)*80+j)
          enddo
          events(event_count)%id=ids(i)
          events(event_count)%frequency=frequencies(i)
          events(event_count)%start=starts(i)
          events(event_count)%latest=latest(i)
          events(event_count)%terminal=terminals(i)
       enddo
       if(n.lt.30) exit
    enddo
  end subroutine collect

  subroutine compare_events(expected,nexpected,actual,nactual,time_offset)
    type(update_event), intent(in) :: expected(:),actual(:)
    integer, intent(in) :: nexpected,nactual
    real(c_double), intent(in) :: time_offset
    integer :: i,j

    call expect(nexpected.eq.nactual,'storage and context changes preserve every message update')
    do i=1,nexpected
       call expect(expected(i)%text.eq.actual(i)%text,'storage and context changes preserve exact text')
       call expect(expected(i)%terminal.eq.actual(i)%terminal,'terminal reasons remain unchanged')
       call expect(abs(expected(i)%frequency-actual(i)%frequency).lt.0.001,'frequency estimates remain unchanged')
       call expect(abs(expected(i)%start-(actual(i)%start-time_offset)).lt.0.000001_c_double, &
            'message start timestamps remain stable at large sample origins')
       call expect(abs(expected(i)%latest-(actual(i)%latest-time_offset)).lt.0.000001_c_double, &
            'latest-frame timestamps remain stable at large sample origins')
       do j=1,i
          call expect((expected(i)%id.eq.expected(j)%id).eqv.(actual(i)%id.eq.actual(j)%id), &
               'growing updates preserve logical identity without merging separate messages')
       enddo
    enddo
  end subroutine compare_events

  subroutine expect_overlap(events,n)
    type(update_event), intent(in) :: events(:)
    integer, intent(in) :: n
    integer :: i
    logical :: complete

    complete=count_complete(events,n,overlap_strong).eq.2 .and. count_complete(events,n,overlap_weak).eq.2
    if(.not.complete) then
       do i=1,n
          write(*,'(i0,1x,f8.3,1x,f8.3,1x,a)') events(i)%terminal,events(i)%start,events(i)%frequency,trim(events(i)%text)
       enddo
    endif
    call expect(complete,'both stronger and weaker overlapping messages complete inside and across the boundary')
  end subroutine expect_overlap

  integer function count_complete(events,n,message)
    type(update_event), intent(in) :: events(:)
    integer, intent(in) :: n
    character(len=*), optional, intent(in) :: message
    integer :: i
    character(len=80) :: expected

    count_complete=0
    expected=wanted
    if(present(message)) expected=message
    do i=1,n
       if(trim(events(i)%text).eq.trim(expected) .and. events(i)%terminal.eq.UPDATE_COMPLETE) &
            count_complete=count_complete+1
    enddo
  end function count_complete

  integer function count_growth(events,n)
    type(update_event), intent(in) :: events(:)
    integer, intent(in) :: n

    count_growth=count(events(1:n)%terminal.eq.UPDATE_GROWING)
  end function count_growth

  subroutine check_terminal(samples,stop,reason)
    integer(c_int16_t), intent(in) :: samples(:)
    integer, intent(in) :: stop,reason
    integer(c_int) :: handle,processed
    type(update_event) :: events(max_events)
    integer :: n

    handle=jtty_rx_create()
    call jtty_rx_begin(handle,3_c_int64_t,0_c_int64_t,0_c_int64_t,nsps)
    processed=jtty_rx_process(handle,samples,stop,0_c_int64_t,int(stop,c_int64_t),1000,200,1499,1500.0,50.0)
    call expect(processed.gt.0,'partial-message fixture is decoded')
    if(reason.eq.UPDATE_RECEPTION_ENDED) call jtty_rx_end(handle,reason)
    n=0
    call collect(handle,events,n)
    call expect(n.gt.0,'partial messages remain deliverable')
    call expect(any(events(1:n)%terminal.eq.reason),'partial message reports its actual terminal reason')
    call expect(.not.any(events(1:n)%terminal.eq.UPDATE_COMPLETE),'interruption is not a completed transmission')
    call jtty_rx_destroy(handle)
  end subroutine check_terminal

  subroutine expect(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if(condition) return
    write(*,'(a)') message
    error stop 1
  end subroutine expect

end program test_jtty_continuous_decode
