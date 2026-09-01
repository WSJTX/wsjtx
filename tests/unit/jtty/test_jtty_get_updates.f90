program test_jtty_get_updates
  use iso_fortran_env, only: int64
  use jtty_mdec, only: message_assembly,npending,discard_pending_updates, &
       queue_message_update
  implicit none

  integer, parameter :: BATCH_SIZE=30,TEXT_WIDTH=80
  character(len=BATCH_SIZE*TEXT_WIDTH) :: text_blocks
  integer(int64) :: message_ids(BATCH_SIZE)
  real :: frequencies(BATCH_SIZE),start_tsync(BATCH_SIZE)
  logical*1 :: eom(BATCH_SIZE)
  type(message_assembly) :: message
  integer :: i,count

  call discard_pending_updates()

  message=make_message(41_int64,1500.4,1.25,'CQ K1ABC')
  call queue_message_update(message,.false.)
  message%decoded='CQ K1ABC FN20'
  call queue_message_update(message,.true.)
  call expect(npending.eq.1, &
       'updates for one logical message coalesce before delivery')

  call jtty_get_updates(text_blocks,message_ids,frequencies,start_tsync,eom,count)
  call expect(count.eq.1 .and. npending.eq.0, &
       'retrieving an update drains it exactly once')
  call expect(message_ids(1).eq.41_int64 .and. &
       trim(block_text(text_blocks,1)).eq.'CQ K1ABC FN20', &
       'the batch contains the newest state and stable identity')
  call expect(abs(frequencies(1)-1500.4).lt.0.001 .and. &
       abs(start_tsync(1)-1.25).lt.0.001 .and. eom(1), &
       'the batch preserves structured message metadata')
  call jtty_get_updates(text_blocks,message_ids,frequencies,start_tsync,eom,count)
  call expect(count.eq.0, 'a drained update is not returned again')

  message=make_message(42_int64,1501.0,2.5,'UNCHANGED TEXT')
  call queue_message_update(message,.false.)
  call queue_message_update(message,.true.)
  call jtty_get_updates(text_blocks,message_ids,frequencies,start_tsync,eom,count)
  call expect(count.eq.1 .and. eom(1) .and. &
       trim(block_text(text_blocks,1)).eq.'UNCHANGED TEXT', &
       'completion metadata coalesces when message text is unchanged')

  do i=1,35
     message=make_message(int(100+i,int64),1400.0+real(i), &
          10.0+real(i),'MESSAGE')
     write(message%decoded,'(a,i0)') 'MESSAGE ',i
     message%k=len_trim(message%decoded)
     call queue_message_update(message,mod(i,2).eq.0)
  enddo
  call expect(npending.eq.35, &
       'the pending store retains more than one ABI batch')

  call jtty_get_updates(text_blocks,message_ids,frequencies,start_tsync,eom,count)
  call expect(count.eq.BATCH_SIZE .and. npending.eq.5, &
       'the first batch drains the oldest thirty updates')
  do i=1,BATCH_SIZE
     call expect(message_ids(i).eq.int(100+i,int64), &
          'the first batch preserves update order')
     call expect(abs(frequencies(i)-(1400.0+real(i))).lt.0.001 .and. &
          abs(start_tsync(i)-(10.0+real(i))).lt.0.001, &
          'the first batch preserves numeric metadata')
     call expect(eom(i) .eqv. (mod(i,2).eq.0), &
          'the first batch preserves completion state')
  enddo

  call jtty_get_updates(text_blocks,message_ids,frequencies,start_tsync,eom,count)
  call expect(count.eq.5 .and. npending.eq.0, &
       'a second batch drains every remaining update')
  do i=1,5
     call expect(message_ids(i).eq.int(130+i,int64), &
          'the second batch continues in update order')
  enddo

  print *, 'test_jtty_get_updates: all checks passed'

contains

  function make_message(id,frequency,tsync,text) result(value)
    integer(int64), intent(in) :: id
    real, intent(in) :: frequency,tsync
    character(len=*), intent(in) :: text
    type(message_assembly) :: value

    value%message_id=id
    value%f1=frequency
    value%tsync=tsync
    value%start_tsync=tsync
    value%decoded=text
    value%k=len_trim(value%decoded)
  end function make_message

  function block_text(blocks,index) result(text)
    character(len=*), intent(in) :: blocks
    integer, intent(in) :: index
    character(len=TEXT_WIDTH) :: text
    integer :: first

    first=(index-1)*TEXT_WIDTH+1
    text=blocks(first:first+TEXT_WIDTH-1)
  end function block_text

  subroutine expect(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if(.not.condition) then
       write(*,'(a)') message
       error stop 1
    endif
  end subroutine expect

end program test_jtty_get_updates
