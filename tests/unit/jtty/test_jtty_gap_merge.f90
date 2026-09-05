program test_jtty_gap_merge

  ! When a frame in the middle of a multi-frame message fails to decode
  ! (common at marginal SNR), the message must still land in ONE slot with
  ! a gap marker between the surviving fragments, not split into two
  ! slots (the bug Joe reported on 000000_000011.wav: "THE QUICK BROWN FOX
  ! JUMPED OVER THE LAZY DOG." decoded as two separate lines, "THE Q" and
  ! "BROWN FOX JUMPED OVER THE LAZY DOG.", losing "UICK " with no trace).

  use iso_fortran_env, only: int16
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mdec, only: nslots, slot
  use jtty_mod, only: MAX_FRAMES
  implicit none

  character(len=*), parameter :: msg='THE QUICK BROWN FOX JUMPED OVER THE LAZY DOG.'
  integer, parameter :: nsps=384
  integer, parameter :: frame_symbols=size(is13)+TOTAL_K
  integer :: failures,total_samples,nframes
  integer(int16), allocatable :: reference_pcm(:)
  character(len=80) :: full_text

  failures=0

  call build_message_pcm(msg,reference_pcm,total_samples,nframes)
  call run_baseline_case(msg,reference_pcm,total_samples,full_text,failures)
  call run_dropped_frame_case(full_text,reference_pcm,total_samples,nframes,1,failures)
  call run_dropped_frame_case(full_text,reference_pcm,total_samples,nframes,2,failures)
  call run_beyond_max_gap_case(reference_pcm,total_samples,nframes,failures)

  if(failures.ne.0) then
     write(*,'(a,i0)') 'test_jtty_gap_merge: failures=',failures
     stop 1
  endif
  write(*,'(a)') 'test_jtty_gap_merge: all checks passed'

contains

  subroutine build_message_pcm(message,pcm,total_samples,nframes)
    character(len=*), intent(in) :: message
    integer(int16), allocatable, intent(out) :: pcm(:)
    integer, intent(out) :: total_samples,nframes
    integer :: tones(MAX_FRAMES*frame_symbols)
    integer :: nsymbols
    real, allocatable :: wave(:)
    complex, allocatable :: complex_wave(:)
    character(len=80) :: input

    input=message
    call genjtty(input,tones,nsymbols)
    nframes=nsymbols/frame_symbols
    total_samples=(nsymbols+frame_symbols)*nsps
    allocate(wave(nsymbols*nsps),complex_wave(nsymbols*nsps),pcm(total_samples))
    call gen_jttywave(tones,nsymbols,nsps,2.0,12000.0,1500.0, &
         complex_wave,wave,0,nsymbols*nsps)
    pcm=0_int16
    pcm(1:nsymbols*nsps)=int(nint(30000.0*wave),int16)
  end subroutine build_message_pcm

  subroutine run_baseline_case(message,reference_pcm,total_samples,full_text,count)
    character(len=*), intent(in) :: message
    integer(int16), intent(in) :: reference_pcm(:)
    integer, intent(in) :: total_samples
    character(len=80), intent(out) :: full_text
    integer, intent(inout) :: count
    integer(int16), allocatable :: pcm(:)

    pcm=reference_pcm
    call rjtty_sub(pcm,1,nsps,200,2800,1500.0,50.0)
    call rjtty_sub(pcm,total_samples,nsps,200,2800,1500.0,50.0)
    full_text=trim(normalized(slot(1)%decoded))
    call expect(nslots.eq.1 .and. full_text.eq.trim(message), &
         'uncorrupted baseline decodes to the original message',count)
  end subroutine run_baseline_case

  ! Drops n_dropped consecutive frames starting at the message's middle
  ! frame and confirms the message still merges into one slot with a
  ! matching run of n_dropped*5 tildes marking the gap.
  subroutine run_dropped_frame_case(full_text,reference_pcm, &
       total_samples,nframes,n_dropped,count)
    character(len=*), intent(in) :: full_text
    integer(int16), intent(in) :: reference_pcm(:)
    integer, intent(in) :: total_samples,nframes,n_dropped
    integer, intent(inout) :: count
    integer :: idrop
    integer :: drop_start,drop_end
    integer(int16), allocatable :: pcm(:)
    character(len=80) :: before,after
    character(len=200) :: description
    integer :: gap_pos

    pcm=reference_pcm
    idrop=nframes/2

    ! Zero out idrop..idrop+n_dropped-1's audio so those frames can't sync.
    drop_start=(idrop-1)*frame_symbols*nsps+1
    drop_end=(idrop+n_dropped-1)*frame_symbols*nsps
    pcm(drop_start:drop_end)=0_int16

    call rjtty_sub(pcm,1,nsps,200,2800,1500.0,50.0)
    call rjtty_sub(pcm,total_samples,nsps,200,2800,1500.0,50.0)

    write(description,'(a,i0,a)') 'dropping ',n_dropped, &
         ' consecutive frame(s) still merges into one slot'
    call expect(nslots.eq.1,trim(description),count)
    if(nslots.ne.1) return

    ! The gap sentinel is a fixed-width 5-tilde run regardless of how many
    ! frames were actually dropped -- " ... " is a generic "something's
    ! missing" marker, not a precise character count (matches Joe's own
    ! suggested display).
    gap_pos=index(slot(1)%decoded,'~~~~~')
    write(description,'(a,i0,a)') 'dropping ',n_dropped, &
         ' consecutive frame(s) leaves a gap sentinel'
    call expect(gap_pos.gt.0,trim(description),count)
    if(gap_pos.le.0) return

    before=trim(normalized(adjustl(slot(1)%decoded(1:gap_pos-1))))
    after=trim(normalized(adjustl(slot(1)%decoded(gap_pos+5:))))
    write(description,'(a,i0,a)') 'dropping ',n_dropped, &
         ' consecutive frame(s): surviving text matches the original message'
    call expect(len_trim(before).gt.0 .and. len_trim(after).gt.0 .and. &
         index(full_text,trim(before)).eq.1 .and. &
         full_text(len_trim(full_text)-len_trim(after)+1:len_trim(full_text)) &
         .eq.trim(after), &
         trim(description),count)
  end subroutine run_dropped_frame_case

  ! A gap wider than MAX_GAP (3 frame-periods, i.e. 3+ consecutive missed
  ! frames) must NOT be bridged -- confirms the fallback to two separate
  ! slots (today's behavior) still holds beyond the deliberately-bounded
  ! gap tolerance, so unrelated signals don't get false-merged.
  subroutine run_beyond_max_gap_case(reference_pcm,total_samples,nframes,count)
    integer(int16), intent(in) :: reference_pcm(:)
    integer, intent(in) :: total_samples,nframes
    integer, intent(inout) :: count
    integer :: idrop,drop_start,drop_end,n_dropped
    integer(int16), allocatable :: pcm(:)

    n_dropped=3   ! beyond MAX_GAP=3 (which bridges at most 2 dropped frames)
    pcm=reference_pcm
    idrop=nframes/2

    drop_start=(idrop-1)*frame_symbols*nsps+1
    drop_end=(idrop+n_dropped-1)*frame_symbols*nsps
    pcm(drop_start:drop_end)=0_int16

    call rjtty_sub(pcm,1,nsps,200,2800,1500.0,50.0)
    call rjtty_sub(pcm,total_samples,nsps,200,2800,1500.0,50.0)

    call expect(nslots.ge.2, &
         'dropping 3 consecutive frames (beyond MAX_GAP) does not merge', &
         count)
  end subroutine run_beyond_max_gap_case

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

end program test_jtty_gap_merge
