program test_jtty_alias_decode

  ! Regression test for misaligned "alias" decodes inside FTol.
  !
  ! A strong JTTY transmission decoded in channel 0 (signal within FTol of
  ! the Rx frequency) used to produce repeatable false frames such as
  ! "IG4QXF TU": a sync candidate landing a whole number of symbols inside an
  ! already-decoded frame of the same signal matched 7 of 13 sync tones by
  ! chance and still yielded a TBCC codeword. is_frame_alias() in
  ! jtty_mdecode.f90 rejects those.
  !
  ! Case 1: the fox message at -3 dB (2500 Hz bandwidth) in deterministic
  ! Gaussian noise, several seeds. Every decoded message must be made of
  ! fragments of the transmitted text, and the full message must still be
  ! decoded in (nearly) every trial.
  !
  ! Case 2: pins down the trade-off. A weaker independent station that
  ! overlaps the strong one at a fractional-symbol offset, within the alias
  ! frequency window, must still decode completely.

  use iso_fortran_env, only: int16, int64
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mdec, only: npending,pending_updates,discard_pending_updates
  use jtty_mod, only: MAX_FRAMES
  implicit none

  character(len=*), parameter :: fox='THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG'
  character(len=*), parameter :: weak='CQ DL3WDG JN68'
  integer, parameter :: nsps=384
  integer :: ntrials
  real :: snr_case1
  character(len=16) :: arg
  integer, parameter :: frame_symbols=size(is13)+TOTAL_K
  integer(int64) :: rng_state
  integer :: failures,itrial,nfull,nfalse
  logical :: full,clean,found_fox,found_weak

  failures=0
  ntrials=24
  snr_case1=-3.0
  call get_command_argument(1,arg)
  if(len_trim(arg).gt.0) read(arg,*) ntrials
  call get_command_argument(2,arg)
  if(len_trim(arg).gt.0) read(arg,*) snr_case1

! Case 1: no alias decodes of a strong signal inside FTol.
  nfull=0
  nfalse=0
  do itrial=1,ntrials
     call decode_fox_in_noise(snr_case1,int(1000+itrial,int64))
     call inspect_fox(full,clean)
     if(full) nfull=nfull+1
     if(.not.clean) nfalse=nfalse+1
  enddo
  write(*,'(a,i0,a,i0,a,i0)') 'trials=',ntrials,' full=',nfull,' false=',nfalse
  call expect(nfalse.eq.0,'no false (alias) messages decoded from a strong signal',failures)
  call expect(nfull.ge.ntrials-1,'the strong message still decodes completely',failures)

! Case 2: a weaker independent station at a fractional-symbol offset survives.
  ! 30 Hz (inside the alias window) and 18.5 symbols after the strong one.
  call decode_pair(found_fox,found_weak,1530.0,37,12000.0)
  call expect(found_fox,'strong station decodes alongside a weaker overlapping one',failures)
  call expect(found_weak,'weaker station at a fractional-symbol offset still decodes',failures)

  if(failures.ne.0) then
     write(*,'(a,i0)') 'test_jtty_alias_decode: failures=',failures
     stop 1
  endif
  write(*,'(a)') 'test_jtty_alias_decode: all checks passed'

contains

  subroutine decode_fox_in_noise(snrdb,seed)
    real, intent(in) :: snrdb
    integer(int64), intent(in) :: seed
    integer :: tones(MAX_FRAMES*frame_symbols),symbols,nsamples,start,total,i
    real, allocatable :: wave(:),noisy(:)
    complex, allocatable :: cwave(:)
    integer(int16), allocatable :: pcm(:)
    character(len=80) :: text
    real :: sig

    call discard_pending_updates()
    text=fox
    call genjtty(text,tones,symbols)
    nsamples=symbols*nsps
    start=6001                                   ! 0.5 s
    total=start+nsamples-1+2*frame_symbols*nsps
    allocate(wave(nsamples),cwave(nsamples),noisy(total),pcm(total))
    call gen_jttywave(tones,symbols,nsps,2.0,12000.0,1500.0,cwave,wave,0,nsamples)

    ! Same SNR convention as sjtty: unit-variance noise, 2500 Hz reference.
    sig=sqrt(2.0*2500.0/6000.0)*10.0**(0.05*snrdb)
    rng_state=seed
    do i=1,total
       noisy(i)=gaussian()
    enddo
    noisy(start:start+nsamples-1)=noisy(start:start+nsamples-1)+sig*wave
    do i=1,total
       pcm(i)=int(max(-32767,min(32767,nint(100.0*noisy(i)))),int16)
    enddo

    call rjtty_sub(pcm,1,nsps,200,2800,1500.0,50.0)
    call rjtty_sub(pcm,total,nsps,200,2800,1500.0,50.0)
  end subroutine decode_fox_in_noise

  subroutine inspect_fox(full,clean)
    logical, intent(out) :: full,clean
    character(len=80) :: body
    integer :: i

    full=.false.
    clean=.true.
    do i=1,npending
       body=normalized(pending_updates(i)%decoded)
       if(pending_updates(i)%complete .and. trim(body).eq.fox) full=.true.
       if(.not.fragments_of(pending_updates(i)%decoded,fox)) then
          clean=.false.
          write(*,'(a,i0,2a)') 'false message: seed=',itrial,' ',trim(body)
       endif
    enddo
  end subroutine inspect_fox

  subroutine decode_pair(found_fox,found_weak,weak_hz,offset_halves,weak_amp)
    logical, intent(out) :: found_fox,found_weak
    real, intent(in) :: weak_hz,weak_amp
    integer, intent(in) :: offset_halves
    integer :: fox_tones(MAX_FRAMES*frame_symbols),weak_tones(MAX_FRAMES*frame_symbols)
    integer :: fox_symbols,weak_symbols,fox_n,weak_n,fox_start,weak_start
    integer :: total,i,idx,v
    real, allocatable :: wave(:)
    complex, allocatable :: cwave(:)
    integer(int16), allocatable :: pcm(:)
    character(len=80) :: text,body

    call discard_pending_updates()
    text=fox
    call genjtty(text,fox_tones,fox_symbols)
    text=weak
    call genjtty(text,weak_tones,weak_symbols)
    fox_n=fox_symbols*nsps
    weak_n=weak_symbols*nsps
    fox_start=2401
    ! 0.3 frame later plus half a symbol: a fractional-symbol offset.
    weak_start=fox_start+offset_halves*nsps/2
    total=max(fox_start+fox_n,weak_start+weak_n)+2*frame_symbols*nsps
    allocate(pcm(total))
    pcm=0_int16

    allocate(wave(fox_n),cwave(fox_n))
    call gen_jttywave(fox_tones,fox_symbols,nsps,2.0,12000.0,1500.0,cwave,wave,0,fox_n)
    do i=1,fox_n
       idx=fox_start+i-1
       pcm(idx)=int(max(-32767,min(32767,nint(14000.0*wave(i)))),int16)
    enddo
    deallocate(wave,cwave)

    allocate(wave(weak_n),cwave(weak_n))
    call gen_jttywave(weak_tones,weak_symbols,nsps,2.0,12000.0,weak_hz,cwave,wave,0,weak_n)
    do i=1,weak_n
       idx=weak_start+i-1
       v=int(pcm(idx))+nint(weak_amp*wave(i))
       pcm(idx)=int(max(-32767,min(32767,v)),int16)
    enddo
    deallocate(wave,cwave)

    call rjtty_sub(pcm,1,nsps,200,2800,1500.0,50.0)
    call rjtty_sub(pcm,total,nsps,200,2800,1500.0,50.0)

    found_fox=.false.
    found_weak=.false.
    do i=1,npending
       body=normalized(pending_updates(i)%decoded)
       if(.not.pending_updates(i)%complete) cycle
       if(trim(body).eq.fox) found_fox=.true.
       if(trim(body).eq.weak) found_weak=.true.
    enddo
    if(.not.(found_fox .and. found_weak)) then
       do i=1,npending
          write(*,'(a,l2,1x,a)') 'decoded:',pending_updates(i)%complete, &
               trim(normalized(pending_updates(i)%decoded))
       enddo
    endif
  end subroutine decode_pair

  ! True when every piece of text between missed-frame markers ('~~~~~') is
  ! part of reference.
  logical function fragments_of(raw,reference)
    character(len=*), intent(in) :: raw,reference
    character(len=80) :: text
    integer :: i,p

    text=adjustl(raw)
    i=index(text,'~~~~~')
    do while(i.gt.0)
       text(i:i+4)='|    '
       i=index(text,'~~~~~')
    enddo
    do i=1,len_trim(text)
       if(text(i:i).eq.'~') text(i:i)=' '
    enddo
    fragments_of=.true.
    do while(len_trim(text).gt.0)
       p=index(text,'|')
       if(p.eq.0) p=len_trim(text)+1
       if(p.gt.1) then
          if(len_trim(text(1:p-1)).gt.0) then
             if(index(reference,trim(adjustl(text(1:p-1)))).eq.0) then
                fragments_of=.false.
                return
             endif
          endif
       endif
       if(p.ge.len_trim(text)) exit
       text=text(p+1:)
    enddo
  end function fragments_of

  ! Portable deterministic N(0,1) samples: Park-Miller LCG (no integer
  ! overflow) plus Box-Muller.
  real function gaussian()
    real :: u1,u2

    u1=uniform()
    u2=uniform()
    gaussian=sqrt(-2.0*log(u1))*cos(6.2831853*u2)
  end function gaussian

  real function uniform()
    rng_state=mod(16807_int64*rng_state,2147483647_int64)
    uniform=real(rng_state)/2147483647.0
  end function uniform

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

end program test_jtty_alias_decode
