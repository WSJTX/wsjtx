program test_jtty_overlap_decode

  ! Overlap two generated JTTY transmissions and decode them through
  ! rjtty_sub. This is a stand-in for the private W2PU on-air corpus.
  !
  ! The 4 Hz case reproduces leftover extra-slot fragments: a real next
  ! frame lands just outside the 3 Hz continuation gate and opens a new
  ! slot instead of staying on its message. Isolated and 2 Hz cases are
  ! controls that already pass.

  use iso_fortran_env, only: int16
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mdec, only: nslots, slot
  use jtty_mod, only: MAX_FRAMES
  implicit none

  character(len=*), parameter :: msg_a='MAYBE YOU SHOULD HELP'
  character(len=*), parameter :: msg_b='CLAUDE CO 3 MIN TRANSITION'
  integer :: failures
  integer :: failures_before_overlap

  failures=0

  call expect_complete_pair('isolated first message',msg_a,1500.0,0.2, &
       '',1500.0,0.6,1,failures)
  call expect_complete_pair('isolated second message','',1500.0,0.2, &
       msg_b,1504.0,0.6,1,failures)
  call expect_complete_pair('2 Hz overlap stays two complete slots', &
       msg_a,1500.0,0.2,msg_b,1502.0,0.6,2,failures)

  failures_before_overlap=failures
  call expect_complete_pair('4 Hz overlap stays two complete slots', &
       msg_a,1500.0,0.2,msg_b,1504.0,0.6,2,failures)
  if(failures.gt.failures_before_overlap) then
     write(*,'(a)') '4 Hz overlap slots: '//trim(slot_summary())
  endif

  if(failures.ne.0) then
     write(*,'(a,i0)') 'test_jtty_overlap_decode: failures=',failures
     stop 1
  endif
  write(*,'(a)') 'test_jtty_overlap_decode: all checks passed'

contains

  subroutine expect_complete_pair(description,first_message,first_hz, &
       first_dt,second_message,second_hz,second_dt,expected_slots,count)
    character(len=*), intent(in) :: description,first_message,second_message
    real, intent(in) :: first_hz,first_dt,second_hz,second_dt
    integer, intent(in) :: expected_slots
    integer, intent(inout) :: count
    logical :: found_a,found_b

    call decode_overlap(first_message,first_hz,first_dt,second_message, &
         second_hz,second_dt,50.0)
    call slot_coverage(first_message,second_message,found_a,found_b)
    call expect(nslots.eq.expected_slots,description//': slot count',count)
    if(len_trim(first_message).gt.0) then
       call expect(found_a,description//': first message complete',count)
    endif
    if(len_trim(second_message).gt.0) then
       call expect(found_b,description//': second message complete',count)
    endif
  end subroutine expect_complete_pair

  subroutine decode_overlap(first_message,first_hz,first_dt,second_message, &
       second_hz,second_dt,ftol)
    character(len=*), intent(in) :: first_message,second_message
    real, intent(in) :: first_hz,first_dt,second_hz,second_dt,ftol
    integer, parameter :: nsps=384
    integer, parameter :: frame_symbols=size(is13)+TOTAL_K
    integer :: first_tones(MAX_FRAMES*frame_symbols)
    integer :: second_tones(MAX_FRAMES*frame_symbols)
    integer :: first_symbols,second_symbols
    integer :: first_samples,second_samples,first_start,second_start
    integer :: total_samples,i,idx
    integer :: first_pcm,second_pcm,mixed
    integer(int16), allocatable :: pcm(:)
    real, allocatable :: first_wave(:),second_wave(:)
    complex, allocatable :: complex_wave(:)
    character(len=80) :: first_input,second_input

    first_symbols=0
    second_symbols=0
    first_samples=0
    second_samples=0
    first_start=1
    second_start=1

    if(len_trim(first_message).gt.0) then
       first_input=first_message
       call genjtty(first_input,first_tones,first_symbols)
       first_samples=first_symbols*nsps
       first_start=max(1,nint(first_dt*12000.0)+1)
    endif
    if(len_trim(second_message).gt.0) then
       second_input=second_message
       call genjtty(second_input,second_tones,second_symbols)
       second_samples=second_symbols*nsps
       second_start=max(1,nint(second_dt*12000.0)+1)
    endif

    total_samples=max(first_start+first_samples-1, &
         second_start+second_samples-1)+frame_symbols*nsps
    allocate(pcm(total_samples))
    pcm=0_int16

    if(first_samples.gt.0) then
       allocate(first_wave(first_samples),complex_wave(first_samples))
       call gen_jttywave(first_tones,first_symbols,nsps,2.0,12000.0,first_hz, &
            complex_wave,first_wave,0,first_samples)
       do i=1,first_samples
          idx=first_start+i-1
          first_pcm=nint(14000.0*first_wave(i))
          pcm(idx)=int(max(-32767,min(32767,first_pcm)),int16)
       enddo
       deallocate(first_wave,complex_wave)
    endif

    if(second_samples.gt.0) then
       allocate(second_wave(second_samples),complex_wave(second_samples))
       call gen_jttywave(second_tones,second_symbols,nsps,2.0,12000.0, &
            second_hz,complex_wave,second_wave,0,second_samples)
       do i=1,second_samples
          idx=second_start+i-1
          second_pcm=nint(14000.0*second_wave(i))
          mixed=int(pcm(idx))+second_pcm
          pcm(idx)=int(max(-32767,min(32767,mixed)),int16)
       enddo
       deallocate(second_wave,complex_wave)
    endif

    call rjtty_sub(pcm,1,nsps,200,2800,1500.0,ftol)
    call rjtty_sub(pcm,total_samples,nsps,200,2800,1500.0,ftol)
    deallocate(pcm)
  end subroutine decode_overlap

  subroutine slot_coverage(first_message,second_message,found_a,found_b)
    character(len=*), intent(in) :: first_message,second_message
    logical, intent(out) :: found_a,found_b
    integer :: i

    found_a=len_trim(first_message).eq.0
    found_b=len_trim(second_message).eq.0
    do i=1,nslots
       if(len_trim(first_message).gt.0) then
          if(trim(normalized(slot(i)%decoded)).eq.trim(first_message)) &
               found_a=.true.
       endif
       if(len_trim(second_message).gt.0) then
          if(trim(normalized(slot(i)%decoded)).eq.trim(second_message)) &
               found_b=.true.
       endif
    enddo
  end subroutine slot_coverage

  function slot_summary() result(text)
    character(len=240) :: text
    character(len=80) :: body
    integer :: i,pos

    text=''
    pos=1
    do i=1,nslots
       body=normalized(slot(i)%decoded)
       if(pos.gt.1) then
          text(pos:pos)='|'
          pos=pos+1
       endif
       text(pos:pos+len_trim(body)-1)=trim(body)
       pos=pos+len_trim(body)
       if(pos.ge.len(text)-1) exit
    enddo
  end function slot_summary

  function normalized(value) result(result_value)
    character(len=*), intent(in) :: value
    character(len=80) :: result_value
    integer :: i

    result_value=adjustl(value)
    do i=1,len_trim(result_value)
       if(result_value(i:i).eq.'~') result_value(i:i)=' '
    enddo
  end function normalized

  subroutine expect(condition,description,count)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    integer, intent(inout) :: count

    if(.not.condition) then
       count=count+1
       write(*,'(a)') 'FAIL: '//description
    endif
  end subroutine expect

end program test_jtty_overlap_decode
