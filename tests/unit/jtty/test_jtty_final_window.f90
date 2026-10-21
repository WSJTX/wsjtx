! SPDX-License-Identifier: GPL-3.0-or-later
program test_jtty_final_window
  use iso_c_binding, only: c_int, c_int16_t, c_int64_t, c_float, c_double, c_char
  use jtty_mdec, only: UPDATE_COMPLETE, UPDATE_RECEPTION_ENDED
  use jtty_receive_context
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mod, only: MAX_FRAMES
  implicit none

  integer, parameter :: nsps=384, nframe=59*nsps, nchunk=nframe+nframe/4, step=nframe/4
  integer, parameter :: lead=6000, capacity=30
  integer, parameter :: extras(5)=[0,1,2000,4000,5327]
  character(len=*), parameter :: quick='THE QUICK BROWN FOX'
  integer(c_int16_t), allocatable :: pcm(:)
  character(len=80) :: text
  integer :: terminal,last,i

  call synthesize(quick,lead+1,pcm)
  last=lead+4*nframe
  call expect(size(pcm).eq.last+nframe,'the message takes four frames')

  ! A full window reaches the last frame only when 5328 more samples follow
  ! it; with fewer, only the final window decodes it.
  call receive(pcm(1:last+5328),.false.,text,terminal)
  call expect(terminal.eq.UPDATE_COMPLETE .and. trim(text).eq.quick, &
       'a full window decodes the last frame')
  call receive(pcm(1:last+5327),.false.,text,terminal)
  call expect(terminal.eq.UPDATE_RECEPTION_ENDED, &
       'without the final window the last frame is lost')

  do i=1,size(extras)
     call receive(pcm(1:last+extras(i)),.true.,text,terminal)
     call expect(terminal.eq.UPDATE_COMPLETE .and. trim(text).eq.quick, &
          'the final window decodes a frame that ends before the reception')
  enddo

  ! A last frame that starts on the search grid lies whole in the final
  ! window, so it decodes even with its end missing.
  call synthesize(quick,4*step+1,pcm)
  last=4*step+4*nframe-4000
  call receive(pcm(1:last),.false.,text,terminal)
  call expect(terminal.eq.UPDATE_RECEPTION_ENDED,'a full window cannot reach the cut frame')
  call receive(pcm(1:last),.true.,text,terminal)
  call expect(terminal.eq.UPDATE_COMPLETE .and. trim(text).eq.quick, &
       'the final window decodes a frame received only in part')

  ! Audio past stop_sample is not part of the reception.
  call receive(pcm(1:4*step+4*nframe),.true.,text,terminal,4*step+4*nframe-6000)
  call expect(terminal.eq.UPDATE_RECEPTION_ENDED,'audio past the end of the reception is not searched')

  call expect_guard()

  print *, 'test_jtty_final_window: all checks passed'

contains

  subroutine synthesize(message,first,samples)
    character(len=*), intent(in) :: message
    integer, intent(in) :: first
    integer(c_int16_t), allocatable, intent(out) :: samples(:)
    integer :: tones(MAX_FRAMES*(size(is13)+TOTAL_K)),symbols,n,i
    real, allocatable :: wave(:)
    complex, allocatable :: analytic(:)
    character(len=80) :: padded

    padded=message
    call genjtty(padded,tones,symbols)
    n=symbols*nsps
    allocate(wave(n),analytic(n),samples(first-1+n+nframe))
    call gen_jttywave(tones,symbols,nsps,2.0,12000.0,1500.0,analytic,wave,0,n)
    samples=0
    do i=1,n
       samples(first+i-1)=int(max(-32767,min(32767,nint(20000.0*wave(i)))),c_int16_t)
    enddo
  end subroutine synthesize

  ! The reception is samples; the last update's text and state.
  subroutine receive(samples,final,text,terminal,stop)
    integer(c_int16_t), intent(in) :: samples(:)
    logical, intent(in) :: final
    character(len=80), intent(out) :: text
    integer, intent(out) :: terminal
    integer, intent(in), optional :: stop
    integer(c_int) :: handle,processed
    integer(c_int64_t) :: required,search,length,total

    text=''
    terminal=-1
    total=size(samples)
    length=total
    if(present(stop)) length=stop
    handle=jtty_rx_create()
    call expect(handle.gt.0,'a receive context is available')
    call jtty_rx_begin(handle,1_c_int64_t,0_c_int64_t,0_c_int64_t,nsps)
    do
       search=jtty_rx_next_search_sample(handle)
       if(search+nchunk.gt.length) exit
       required=jtty_rx_next_required_sample(handle)
       processed=jtty_rx_process(handle,samples(required+1:),int(length-required,c_int), &
            required,length,1,200,3000,1500.0,20.0)
       call take(handle,text,terminal)
       call expect(processed.ge.0,'the receive audio is available')
       if(processed.eq.0) exit
    enddo
    if(final) then
       do
          required=jtty_rx_next_required_sample(handle)
          processed=jtty_rx_process_final(handle,samples(required+1:),int(total-required,c_int), &
               required,length,1,200,3000,1500.0,20.0)
          call take(handle,text,terminal)
          call expect(processed.ge.0,'the final window''s audio is available')
          if(processed.eq.0) exit
       enddo
       call expect(jtty_rx_next_search_sample(handle).eq.step*((max(length-nchunk+1,0_c_int64_t)+step-1)/step)+step, &
            'only the first window past the reception is searched')
       search=jtty_rx_next_search_sample(handle)
       required=jtty_rx_next_required_sample(handle)
       call expect(jtty_rx_process_final(handle,samples(required+1:),int(length-required,c_int), &
            required,length,8,200,3000,1500.0,20.0).eq.0 .and. &
            jtty_rx_next_search_sample(handle).eq.search, &
            'the final window is searched once')
    endif
    call jtty_rx_end(handle,UPDATE_RECEPTION_ENDED)
    call take(handle,text,terminal)
    call jtty_rx_destroy(handle)
  end subroutine receive

  ! Less than half a frame in the final window: nothing is searched.
  subroutine expect_guard()
    integer(c_int16_t) :: silence(nframe/2)
    integer(c_int) :: handle,processed

    silence=0
    handle=jtty_rx_create()
    call expect(handle.gt.0,'a receive context is available')
    call jtty_rx_begin(handle,1_c_int64_t,0_c_int64_t,0_c_int64_t,nsps)
    processed=jtty_rx_process_final(handle,silence,int(nframe/2-1,c_int),0_c_int64_t, &
         int(nframe/2-1,c_int64_t),1,200,3000,1500.0,20.0)
    call expect(processed.eq.0 .and. jtty_rx_next_search_sample(handle).eq.0, &
         'a final window with less than half a frame is not searched')
    processed=jtty_rx_process_final(handle,silence,int(nframe/2,c_int),0_c_int64_t, &
         int(nframe/2,c_int64_t),1,200,3000,1500.0,20.0)
    call expect(processed.eq.1 .and. jtty_rx_next_search_sample(handle).eq.step, &
         'a final window with half a frame is searched')
    call jtty_rx_end(handle,UPDATE_RECEPTION_ENDED)
    call jtty_rx_destroy(handle)
  end subroutine expect_guard

  subroutine take(handle,text,terminal)
    integer(c_int), intent(in) :: handle
    character(len=80), intent(inout) :: text
    integer, intent(inout) :: terminal
    character(kind=c_char) :: block(80*capacity)
    integer(c_int64_t) :: ids(capacity)
    real(c_float) :: frequencies(capacity)
    real(c_double) :: starts(capacity),latest(capacity)
    integer(c_int) :: states(capacity),snrs(capacity),n
    integer :: j

    do
       n=jtty_rx_take_updates(handle,capacity,block,ids,frequencies,starts,latest,states,snrs)
       if(n.gt.0) then
          do j=1,80
             text(j:j)=block((n-1)*80+j)
          enddo
          terminal=states(n)
       endif
       if(n.lt.capacity) return
    enddo
  end subroutine take

  subroutine expect(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if(.not.condition) then
       write(*,'(a)') message
       error stop 1
    endif
  end subroutine expect

end program test_jtty_final_window
