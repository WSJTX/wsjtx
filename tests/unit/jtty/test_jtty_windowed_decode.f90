program test_jtty_windowed_decode

  ! rjtty_sub_windowed bounds the scan to [istart0,istop] instead of the
  ! whole buffer -- used by MainWindow::jttyDecodeAgainAt() for a WideGraph
  ! click-driven re-decode. Two messages placed far apart in time: a window
  ! covering only one message must find only that one; a window covering
  ! both (or the unwindowed rjtty_sub) must find both; and decoded content
  ! for a message fully inside its window must match the unwindowed result
  ! exactly (windowing must not corrupt what it does decode).

  use iso_fortran_env, only: int16
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mdec, only: npending,pending_updates,discard_pending_updates
  use jtty_mod, only: MAX_FRAMES
  implicit none

  character(len=*), parameter :: msg_a='MAYBE YOU SHOULD HELP'
  character(len=*), parameter :: msg_b='CLAUDE CO 3 MIN TRANSITION'
  integer, parameter :: nsps=384
  integer, parameter :: frame_symbols=size(is13)+TOTAL_K
  real, parameter :: dt_a=0.2, dt_b=40.0
  integer :: failures
  integer :: first_start,second_start,first_samples,second_samples,total_samples
  integer(int16), allocatable :: pcm(:)
  character(len=80) :: decoded_a_full,decoded_a_windowed

  failures=0

  call build_pair(pcm,total_samples,first_start,first_samples,second_start,second_samples)

  ! Baseline: unwindowed scan finds both messages.
  call rjtty_sub(pcm,1,nsps,200,2800,1500.0,50.0)
  call rjtty_sub(pcm,total_samples,nsps,200,2800,1500.0,50.0)
  call expect(found(msg_a),'unwindowed: first message found',failures)
  call expect(found(msg_b),'unwindowed: second message found',failures)
  decoded_a_full=decoded_text(msg_a)

  ! Window covering only the first message: second must not appear.
  call discard_pending_updates()
  call rjtty_sub_windowed(pcm,1,nsps,200,2800,1500.0,50.0,1,1)
  call rjtty_sub_windowed(pcm,first_start+first_samples+2*chunk(), &
       nsps,200,2800,1500.0,50.0,1,first_start+first_samples+2*chunk())
  call expect(found(msg_a),'window on first message: first message found',failures)
  call expect(.not.found(msg_b),'window on first message: second message absent',failures)
  decoded_a_windowed=decoded_text(msg_a)
  call expect(trim(decoded_a_windowed).eq.trim(decoded_a_full), &
       'windowed decode content matches unwindowed content',failures)

  ! Window covering only the second message: first must not appear.
  call discard_pending_updates()
  call rjtty_sub_windowed(pcm,1,nsps,200,2800,1500.0,50.0, &
       max(1,second_start-2*chunk()),total_samples)
  call rjtty_sub_windowed(pcm,total_samples,nsps,200,2800,1500.0,50.0, &
       max(1,second_start-2*chunk()),total_samples)
  call expect(.not.found(msg_a),'window on second message: first message absent',failures)
  call expect(found(msg_b),'window on second message: second message found',failures)

  if(failures.ne.0) then
     write(*,'(a,i0)') 'test_jtty_windowed_decode: failures=',failures
     stop 1
  endif
  write(*,'(a)') 'test_jtty_windowed_decode: all checks passed'

contains

  integer function chunk()
    integer :: nframe
    nframe=59*nsps
    chunk=nframe+nframe/4
  end function chunk

  subroutine build_pair(pcm,total_samples,first_start,first_samples, &
       second_start,second_samples)
    integer(int16), allocatable, intent(out) :: pcm(:)
    integer, intent(out) :: total_samples,first_start,first_samples
    integer, intent(out) :: second_start,second_samples
    integer :: first_tones(MAX_FRAMES*frame_symbols)
    integer :: second_tones(MAX_FRAMES*frame_symbols)
    integer :: first_symbols,second_symbols,i,idx,pcm_val
    real, allocatable :: first_wave(:),second_wave(:)
    complex, allocatable :: complex_wave(:)
    character(len=80) :: first_input,second_input

    first_input=msg_a
    call genjtty(first_input,first_tones,first_symbols)
    first_samples=first_symbols*nsps
    first_start=max(1,nint(dt_a*12000.0)+1)

    second_input=msg_b
    call genjtty(second_input,second_tones,second_symbols)
    second_samples=second_symbols*nsps
    second_start=max(1,nint(dt_b*12000.0)+1)

    total_samples=max(first_start+first_samples-1, &
         second_start+second_samples-1)+frame_symbols*nsps
    allocate(pcm(total_samples))
    pcm=0_int16

    allocate(first_wave(first_samples),complex_wave(first_samples))
    call gen_jttywave(first_tones,first_symbols,nsps,2.0,12000.0,1500.0, &
         complex_wave,first_wave,0,first_samples)
    do i=1,first_samples
       idx=first_start+i-1
       pcm_val=nint(14000.0*first_wave(i))
       pcm(idx)=int(max(-32767,min(32767,pcm_val)),int16)
    enddo
    deallocate(first_wave,complex_wave)

    allocate(second_wave(second_samples),complex_wave(second_samples))
    call gen_jttywave(second_tones,second_symbols,nsps,2.0,12000.0,1504.0, &
         complex_wave,second_wave,0,second_samples)
    do i=1,second_samples
       idx=second_start+i-1
       pcm_val=nint(14000.0*second_wave(i))
       pcm(idx)=int(max(-32767,min(32767,pcm_val)),int16)
    enddo
    deallocate(second_wave,complex_wave)
  end subroutine build_pair

  logical function found(message)
    character(len=*), intent(in) :: message
    integer :: i
    found=.false.
    do i=1,npending
       if(trim(normalized(pending_updates(i)%decoded)).eq.trim(message)) found=.true.
    enddo
  end function found

  function decoded_text(message) result(text)
    character(len=*), intent(in) :: message
    character(len=80) :: text
    integer :: i
    text=''
    do i=1,npending
       if(trim(normalized(pending_updates(i)%decoded)).eq.trim(message)) &
            text=normalized(pending_updates(i)%decoded)
    enddo
  end function decoded_text

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

end program test_jtty_windowed_decode
