program test_jtty_sticky_retry

  ! decode_and_merge's sticky-sync retry rescues a continuation frame whose
  ! own sync symbols are too degraded for the blind search to find, by
  ! redecoding directly at the position remembered from the slot it
  ! continues -- bypassing the blind search's sync-quality gate entirely.
  ! Two bugs in that retry (found in PR #337 review by David Christle,
  ! fixed in 48b9e6d74) meant it could silently fail or read the wrong
  ! frequency's data. Reproducing the real stochastic conditions that
  ! trigger it (weak SNR, fading) isn't reliable for a regression test, so
  ! this instead forces it deterministically: frame 2's 13 sync tones are
  ! shifted by 2 (mod 4), guaranteeing zero matches against the true sync
  ! pattern -- so the blind search's nsync<=6 gate rejects it outright,
  ! regardless of what the coarse candidate search finds -- while frame
  ! 2's 46 payload tones are left untouched. If the retry works, it merges
  ! anyway; if not, frame 2's contribution is silently missing. A second,
  ! in-band signal is superimposed so the shared c1 work array actually
  ! gets shifted away from the target's own frequency before the retry
  ! runs -- see the comment at its construction below for why.

  use iso_fortran_env, only: int16
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mdec, only: npending,pending_updates,discard_pending_updates
  use jtty_mod, only: MAX_FRAMES
  implicit none

  integer :: failures

  failures=0
  call run_case('WB9XYZ 599 0123',3,failures)

  if(failures.ne.0) then
     write(*,'(a,i0)') 'test_jtty_sticky_retry: failures=',failures
     stop 1
  endif
  write(*,'(a)') 'test_jtty_sticky_retry: all checks passed'

contains

  subroutine run_case(message,nframes,count)
    character(len=*), intent(in) :: message
    integer, intent(in) :: nframes
    integer, intent(inout) :: count
    integer, parameter :: nsps=384
    integer, parameter :: frame_symbols=size(is13)+TOTAL_K
    integer :: tones(MAX_FRAMES*frame_symbols)
    integer :: qrm_tones(frame_symbols)
    integer :: nsym,qrm_nsym,nsamples,total_samples,ib,i0
    integer(int16), allocatable :: pcm(:)
    real, allocatable :: wave(:),qrm_wave(:),combined(:)
    complex, allocatable :: complex_wave(:)
    character(len=80) :: input,qrm_input

    call discard_pending_updates()
    input=message
    call genjtty(input,tones,nsym)
    call expect(nsym.eq.nframes*frame_symbols, &
         'the message has the expected frame count',count)

    ! Frame 2's sync tones start right after frame 1's 59 symbols.
    ib=frame_symbols+1
    tones(ib:ib+size(is13)-1)=mod(is13+2,4)

    ! rjtty_sub only processes a window once nchunk (1.25 frame periods)
    ! more data is available past its start; without a trailing pad past
    ! the message's own end, the internal quarter-step sweep runs out of
    ! data before ever reaching the window that would check frame 2's
    ! continuation, blind or sticky-retry alike (matches the same padding
    ! test_jtty_adjacent_decode.f90 uses for the same reason).
    nsamples=nsym*nsps
    total_samples=nsamples+frame_symbols*nsps
    allocate(wave(nsamples),complex_wave(total_samples),combined(total_samples))
    allocate(pcm(total_samples))
    call gen_jttywave(tones,nsym,nsps,2.0,12000.0,1500.0, &
         complex_wave,wave,0,nsamples)
    combined=0.
    combined(1:nsamples)=wave

    ! twkfreq unconditionally re-shifts the shared c1 work array for
    ! every candidate the blind search tries this call, whether or not
    ! it decodes -- so by the time the sticky retry runs, c1 reflects
    ! whichever candidate was tried last, not necessarily the target's
    ! own frequency. A single clean signal with no competing candidate
    ! doesn't exercise that (verified: reverting 48b9e6d74 didn't fail
    ! this test until the interferer below was added) -- an in-band
    ! interferer (within ftol=50 Hz of f0=1500, so channel 0's own
    ! search sees it) gives c1 something else to be shifted to. Tiled
    ! from one short message repeated across the buffer, at lower
    ! amplitude so it can't preempt the target's own frame 1 decode.
    qrm_input='N2PPI'
    call genjtty(qrm_input,qrm_tones,qrm_nsym)
    allocate(qrm_wave(qrm_nsym*nsps))
    call gen_jttywave(qrm_tones,qrm_nsym,nsps,2.0,12000.0,1540.0, &
         complex_wave(1:qrm_nsym*nsps),qrm_wave,0,qrm_nsym*nsps)
    i0=1
    do while(i0.le.total_samples)
       ib=min(total_samples,i0+qrm_nsym*nsps-1)
       combined(i0:ib)=combined(i0:ib)+0.3*qrm_wave(1:ib-i0+1)
       i0=i0+qrm_nsym*nsps
    enddo

    pcm=int(nint(30000.0*combined),int16)

    ! nfb=1499 (not the usual Wide Graph range) keeps channels 1 and 2 --
    ! whose hardwired bands, [1200,1500] and [1500,1800], meet exactly at
    ! the target's own f0 -- from independently retrying the same slot
    ! themselves and masking a channel-0-specific failure: the target's
    ! own estimated frequency lands a couple of Hz above 1500, just
    ! inside channel 2's band too, so without this its own sticky retry
    ! can coincidentally rescue the frame channel 0's retry failed to.
    call rjtty_sub(pcm,1,nsps,200,1499,1500.0,50.0)
    call rjtty_sub(pcm,total_samples,nsps,200,1499,1500.0,50.0)

    call expect(npending.eq.2,'the decoder keeps the target and interferer', &
         count)
    call expect(found_complete(message), &
         'the full message decodes despite the corrupted sync',count)

    deallocate(pcm,wave,complex_wave,combined,qrm_wave)
  end subroutine run_case

  logical function found_complete(message)
    character(len=*), intent(in) :: message
    integer :: i

    found_complete=.false.
    do i=1,npending
       if(trim(normalized(pending_updates(i)%decoded)).eq.trim(message) .and. &
            pending_updates(i)%complete) found_complete=.true.
    enddo
  end function found_complete

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

end program test_jtty_sticky_retry
