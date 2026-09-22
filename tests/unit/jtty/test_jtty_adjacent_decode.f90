program test_jtty_adjacent_decode

  use iso_fortran_env, only: int16
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mdec, only: npending,pending_updates,discard_pending_updates
  use jtty_mod, only: MAX_FRAMES
  implicit none

  integer :: failures

  failures=0
  call run_case('CQ KA1ABC CQ','WB9XYZ TU',1,1,failures)
  call run_case('WB9XYZ 599 0123','WB9XYZ TU CQ KA1ABC CQ',3,2,failures)

  if(failures.ne.0) then
     write(*,'(a,i0)') 'test_jtty_adjacent_decode: failures=',failures
     stop 1
  endif
  write(*,'(a)') 'test_jtty_adjacent_decode: all checks passed'

contains

  subroutine run_case(first_message,second_message,first_frames,second_frames, &
       count)
    character(len=*), intent(in) :: first_message,second_message
    integer, intent(in) :: first_frames,second_frames
    integer, intent(inout) :: count
    integer, parameter :: nsps=384
    integer, parameter :: frame_symbols=size(is13)+TOTAL_K
    integer :: first_tones(MAX_FRAMES*frame_symbols)
    integer :: second_tones(MAX_FRAMES*frame_symbols)
    integer :: first_symbols,second_symbols,first_samples,second_samples
    integer :: total_samples
    integer(int16), allocatable :: pcm(:)
    real, allocatable :: first_wave(:),second_wave(:)
    complex, allocatable :: complex_wave(:)
    character(len=80) :: first_input,second_input

    call discard_pending_updates()
    first_input=first_message
    second_input=second_message
    call genjtty(first_input,first_tones,first_symbols)
    call genjtty(second_input,second_tones,second_symbols)
    call expect(first_symbols.eq.first_frames*frame_symbols, &
         'the first message has the expected frame count',count)
    call expect(second_symbols.eq.second_frames*frame_symbols, &
         'the second message has the expected frame count',count)

    first_samples=first_symbols*nsps
    second_samples=second_symbols*nsps
    total_samples=first_samples+second_samples+frame_symbols*nsps
    allocate(first_wave(first_samples),second_wave(second_samples))
    allocate(complex_wave(max(first_samples,second_samples)))
    allocate(pcm(total_samples))

    call gen_jttywave(first_tones,first_symbols,nsps,2.0,12000.0,1500.0, &
         complex_wave,first_wave,0,first_samples)
    call gen_jttywave(second_tones,second_symbols,nsps,2.0,12000.0,1500.0, &
         complex_wave,second_wave,0,second_samples)
    pcm=0_int16
    pcm(1:first_samples)=int(nint(30000.0*first_wave),int16)
    pcm(first_samples+1:first_samples+second_samples)= &
         int(nint(30000.0*second_wave),int16)

    call rjtty_sub(pcm,1,nsps,200,2800,1500.0,50.0)
    call rjtty_sub(pcm,total_samples,nsps,200,2800,1500.0,50.0)

    call expect(npending.eq.2,'the decoder keeps two adjacent messages',count)
    if(npending.eq.2) then
       call expect(trim(normalized(pending_updates(1)%decoded)).eq.trim(first_message), &
            'the first decoded text is retained in order',count)
       call expect(trim(normalized(pending_updates(2)%decoded)).eq.trim(second_message), &
            'the second decoded text is retained in order',count)
       call expect(pending_updates(1)%complete, &
            'the first message reaches end of message',count)
       call expect(pending_updates(2)%complete, &
            'the second message reaches end of message',count)
    endif

    deallocate(pcm,first_wave,second_wave,complex_wave)
  end subroutine run_case

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

end program test_jtty_adjacent_decode
