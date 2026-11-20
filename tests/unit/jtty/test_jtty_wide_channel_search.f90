program test_jtty_wide_channel_search

  ! The fixed search channels tile nfa..nfb. A signal well away from Rx, or
  ! one sitting on the boundary between two channels, decodes exactly once.

  use iso_fortran_env, only: int16
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mdec, only: npending,pending_updates,discard_pending_updates
  use jtty_mod, only: MAX_FRAMES
  implicit none

  integer, parameter :: nfa=200, nfb=2800
  ! Matches jtty_mdecode's tiling for this range: 9 channels.
  integer, parameter :: nchan=9
  real, parameter :: width=real(nfb-nfa)/real(nchan)
  real, parameter :: offsets(5)=[-6.0,-3.0,0.0,3.0,6.0]
  integer :: failures, k, i

  failures=0
  call run_case(600.0,'far below Rx',failures)
  call run_case(2400.0,'far above Rx',failures)
  do k=1,nchan-1
     do i=1,size(offsets)
        call run_case(real(nfa)+k*width+offsets(i),'on a channel boundary',failures)
     enddo
  enddo

  if(failures.ne.0) then
     write(*,'(a,i0)') 'test_jtty_wide_channel_search: failures=',failures
     stop 1
  endif
  write(*,'(a)') 'test_jtty_wide_channel_search: all checks passed'

contains

  subroutine run_case(f0,label,count)
    real, intent(in) :: f0
    character(len=*), intent(in) :: label
    integer, intent(inout) :: count
    character(len=*), parameter :: message='WB9XYZ TU CQ KA1ABC CQ'
    integer, parameter :: nsps=384
    integer, parameter :: frame_symbols=size(is13)+TOTAL_K
    integer :: tones(MAX_FRAMES*frame_symbols)
    integer :: nsym, nsamples, total_samples
    integer(int16), allocatable :: pcm(:)
    real, allocatable :: wave(:)
    complex, allocatable :: cwave(:)
    character(len=80) :: input
    character(len=96) :: what

    write(what,'(a,a,f7.1,a)') label,' (',f0,' Hz)'
    call discard_pending_updates()
    input=message
    call genjtty(input,tones,nsym)
    nsamples=nsym*nsps
    total_samples=nsamples+frame_symbols*nsps
    allocate(wave(nsamples),cwave(nsamples),pcm(total_samples))
    call gen_jttywave(tones,nsym,nsps,2.0,12000.0,f0,cwave,wave,0,nsamples)
    pcm=0_int16
    pcm(1:nsamples)=int(nint(30000.0*wave),int16)

    call rjtty_sub(pcm,1,nsps,nfa,nfb,1500.0,50.0)
    call rjtty_sub(pcm,total_samples,nsps,nfa,nfb,1500.0,50.0)

    call expect(npending.eq.1,trim(what)//': decoded exactly once',count)
    if(npending.ge.1) then
       call expect(trim(normalized(pending_updates(1)%decoded)).eq.message, &
            trim(what)//': text is intact',count)
       call expect(abs(pending_updates(1)%f1-f0).lt.5.0, &
            trim(what)//': reported at its own frequency',count)
    endif
    if(npending.ne.1) write(*,'(a,i0)') '  npending=',npending
    deallocate(pcm,wave,cwave)
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

end program test_jtty_wide_channel_search
