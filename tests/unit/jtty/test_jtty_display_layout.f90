! SPDX-License-Identifier: GPL-3.0-or-later
program test_jtty_display_layout
  use iso_c_binding, only: c_int, c_int16_t, c_int64_t, c_float, c_double, c_char
  use jtty_mdec, only: MAX_DISPLAY_GAPS, UPDATE_COMPLETE, UPDATE_RECEPTION_ENDED, &
       display_message_layout, display_message_text
  use jtty_receive_context
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mod, only: MAX_FRAMES
  implicit none

  integer, parameter :: nsps=384, nframe=59*nsps, nchunk=nframe+nframe/4, capacity=30
  character(len=*), parameter :: gapped='HI ... THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG.'
  character(len=80) :: tildes
  integer :: i

  call expect_layout('CQ K1ABC FN20','CQ K1ABC FN20',[integer ::])
  call expect_layout('WAIT... OK ...','WAIT... OK ...',[integer ::])
  call expect_layout('HELLO~~~~~WORLD','HELLO ... WORLD',[6])
  call expect_layout('HI ... THE QUICK BRO~~~~~X JUMPED', &
       'HI ... THE QUICK BRO ... X JUMPED',[21])
  call expect_layout('~~~~~MORE TEXT','... MORE TEXT',[0])
  call expect_layout('END~~~~~','END ...',[4])
  call expect_layout('A~~~~~B~~~~~C','A ... B ... C',[2,8])
  call expect_layout('~~~~~~~~~~X','...  ... X',[0,5])
  call expect_layout('~~~~~~X','...  X',[0])
  call expect_layout('~599 TU','599 TU',[integer ::])

  tildes=repeat('~',80)
  call expect_layout(tildes,repeat('...  ',15)//'...',[(5*i,i=0,15)])
  call expect_capped_layout(tildes,3,[0,5,10])

  call expect_receive_gaps()

  print *, 'test_jtty_display_layout: all checks passed'

contains

  subroutine expect_layout(decoded,text,offsets)
    character(len=*), intent(in) :: decoded,text
    integer, intent(in) :: offsets(:)
    character(len=80) :: msg
    integer :: gaps(MAX_DISPLAY_GAPS),ngaps,i

    call display_message_layout(decoded,msg,gaps,ngaps)
    call expect(msg.eq.display_message_text(decoded), &
         'the layout renders exactly display_message_text: '//decoded)
    call expect(trim(msg).eq.text .and. len_trim(msg).eq.len(text), &
         'the rendering is unchanged: '//decoded)
    call expect(ngaps.eq.size(offsets),'every missed-frames marker is reported once: '//decoded)
    if(ngaps.ne.size(offsets)) return
    call expect(all(gaps(1:ngaps).eq.offsets),'a gap is the offset of its first dot: '//decoded)
    do i=1,ngaps
       call expect(msg(gaps(i)+1:gaps(i)+3).eq.'...','a gap offset points at three dots: '//decoded)
    enddo
  end subroutine expect_layout

  subroutine expect_capped_layout(decoded,cap,offsets)
    character(len=*), intent(in) :: decoded
    integer, intent(in) :: cap,offsets(:)
    character(len=80) :: msg
    integer :: gaps(cap),ngaps

    call display_message_layout(decoded,msg,gaps,ngaps)
    call expect(msg.eq.display_message_text(decoded), &
         'markers past the capacity are still rendered')
    call expect(ngaps.eq.cap .and. all(gaps.eq.offsets), &
         'only the first markers that fit are reported')
  end subroutine expect_capped_layout

  ! A frame lost from a received message: jtty_rx_take_updates_with_gaps
  ! returns the text jtty_rx_take_updates returns, and the marker's offset.
  subroutine expect_receive_gaps()
    integer(c_int16_t), allocatable :: pcm(:)
    character(len=80) :: plain(64),marked(64)
    integer :: plain_gaps(MAX_DISPLAY_GAPS,64),marked_gaps(MAX_DISPLAY_GAPS,64)
    integer :: plain_ngaps(64),marked_ngaps(64),plain_terminals(64),marked_terminals(64)
    integer :: nplain,nmarked,first,i

    first=2401
    call synthesize(gapped,first,pcm)
    call expect(size(pcm).eq.first-1+(11+2)*nframe,'the message takes eleven frames')
    pcm(first+4*nframe:first+5*nframe-1)=0
    call receive(pcm,.false.,plain,plain_gaps,plain_ngaps,plain_terminals,nplain)
    call receive(pcm,.true.,marked,marked_gaps,marked_ngaps,marked_terminals,nmarked)
    call expect(nmarked.ge.2 .and. nmarked.eq.nplain,'both takes see the same updates')
    if(nmarked.ne.nplain .or. nmarked.lt.2) return
    do i=1,nmarked
       call expect(marked(i).eq.plain(i) .and. marked_terminals(i).eq.plain_terminals(i), &
            'the update text is jtty_rx_take_updates''s text')
       if(len_trim(marked(i)).gt.21) then
          call expect(marked_ngaps(i).eq.1 .and. marked_gaps(1,i).eq.21, &
               'the lost frame is reported where its marker begins')
       else
          call expect(marked_ngaps(i).eq.0,'no marker precedes the lost frame')
       endif
    enddo
    call expect(marked_terminals(nmarked).eq.UPDATE_COMPLETE .and. &
         trim(marked(nmarked)).eq.gapped(1:20)//' ... '//gapped(26:), &
         'the message completes with the lost frame marked')
  end subroutine expect_receive_gaps

  subroutine synthesize(text,first,pcm)
    character(len=*), intent(in) :: text
    integer, intent(in) :: first
    integer(c_int16_t), allocatable, intent(out) :: pcm(:)
    integer :: tones(MAX_FRAMES*(size(is13)+TOTAL_K)),symbols,n,i
    real, allocatable :: wave(:)
    complex, allocatable :: analytic(:)
    character(len=80) :: message

    message=text
    call genjtty(message,tones,symbols)
    n=symbols*nsps
    allocate(wave(n),analytic(n),pcm(first-1+n+2*nframe))
    call gen_jttywave(tones,symbols,nsps,2.0,12000.0,1500.0,analytic,wave,0,n)
    pcm=0
    do i=1,n
       pcm(first+i-1)=int(max(-32767,min(32767,nint(20000.0*wave(i)))),c_int16_t)
    enddo
  end subroutine synthesize

  ! The GUI's receive loop: one step at a time, updates taken after each,
  ! while a search window fits.
  subroutine receive(pcm,with_gaps,texts,gaps,ngaps,terminals,count)
    integer(c_int16_t), intent(in) :: pcm(:)
    logical, intent(in) :: with_gaps
    character(len=80), intent(out) :: texts(:)
    integer, intent(out) :: gaps(:,:),ngaps(:),terminals(:),count
    integer(c_int) :: handle,processed
    integer(c_int64_t) :: required,search

    count=0
    handle=jtty_rx_create()
    call expect(handle.gt.0,'a receive context is available')
    call jtty_rx_begin(handle,1_c_int64_t,0_c_int64_t,0_c_int64_t,nsps)
    do
       search=jtty_rx_next_search_sample(handle)
       if(search+nchunk.gt.size(pcm)) exit
       required=jtty_rx_next_required_sample(handle)
       processed=jtty_rx_process(handle,pcm(required+1:),int(size(pcm)-required,c_int), &
            required,int(size(pcm),c_int64_t),1,200,3000,1500.0,20.0)
       call take(handle,with_gaps,texts,gaps,ngaps,terminals,count)
       if(processed.le.0) exit
    enddo
    call jtty_rx_end(handle,UPDATE_RECEPTION_ENDED)
    call take(handle,with_gaps,texts,gaps,ngaps,terminals,count)
    call jtty_rx_destroy(handle)
  end subroutine receive

  subroutine take(handle,with_gaps,texts,gaps,ngaps,terminals,count)
    integer(c_int), intent(in) :: handle
    logical, intent(in) :: with_gaps
    character(len=80), intent(inout) :: texts(:)
    integer, intent(inout) :: gaps(:,:),ngaps(:),terminals(:),count
    character(kind=c_char) :: text(80*capacity)
    integer(c_int64_t) :: ids(capacity)
    real(c_float) :: frequencies(capacity)
    real(c_double) :: starts(capacity),latest(capacity)
    integer(c_int) :: states(capacity),snrs(capacity),n
    integer :: batch_gaps(MAX_DISPLAY_GAPS,capacity),batch_ngaps(capacity),i,j

    do
       if(with_gaps) then
          n=jtty_rx_take_updates_with_gaps(handle,capacity,text,ids,frequencies,starts,latest, &
               states,snrs,batch_gaps,batch_ngaps)
       else
          n=jtty_rx_take_updates(handle,capacity,text,ids,frequencies,starts,latest,states,snrs)
          batch_gaps=0
          batch_ngaps=0
       endif
       do i=1,n
          call expect(count.lt.size(texts),'the fixture produces a bounded number of updates')
          count=count+1
          do j=1,80
             texts(count)(j:j)=text((i-1)*80+j)
          enddo
          terminals(count)=states(i)
          ngaps(count)=batch_ngaps(i)
          gaps(:,count)=batch_gaps(:,i)
       enddo
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

end program test_jtty_display_layout
