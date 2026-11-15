program test_jtty_surface_cache
  use iso_fortran_env, only: int16,int64,real64
  use jtty_fec, only: PAYLOAD_BITS
  use jtty_mod, only: MAX_FRAMES,pack_jtty
  use jtty_mdec, only: message_update,MAX_ACTIVE_MESSAGES,MAX_SUBTRACTED, &
       jtty_mdecode,jtty_release_fft_resources,reset_decode_search_state, &
       discard_pending_updates,npending,pending_first,pending_updates, &
       ndecodes,next_message_id,nsubtracted,subtracted_f1,subtracted_tsync, &
       subtracted_payload,interferer_pending,interferer_f1,interferer_tsync, &
       interferer_payload,sample_origin,explicit_receive_context
  implicit none

  character(len=24), parameter :: scenarios(8)=[character(len=24) :: &
       'disjoint subtraction','full overlap','partial overlap','changed PCM', &
       'changed search bounds','changed sample origin','changed symbol rate', &
       'changed chunk size']
  type :: decode_result
     integer :: count=0,decodes=0,subtractions=0
     type(message_update) :: updates(MAX_ACTIVE_MESSAGES)
     real :: frequencies(MAX_SUBTRACTED)=0.0
     real(real64) :: times(MAX_SUBTRACTED)=0.0_real64
     integer :: payloads(PAYLOAD_BITS,MAX_SUBTRACTED)=0
  end type decode_result
  integer :: scenario

  do scenario=1,8
     call verify_cache(scenario)
  enddo
  call jtty_release_fft_resources()
  explicit_receive_context=.false.
  sample_origin=0_int64
  print *, 'JTTY surface cache tests passed'

contains

  subroutine verify_cache(scenario)
    integer, intent(in) :: scenario
    integer, parameter :: nsps=384,nframe=59*nsps
    integer :: prime_rate,prime_chunk,chunk,payload(PAYLOAD_BITS)
    integer(int16), allocatable :: prime_pcm(:),pcm(:)
    integer(int64) :: origin
    real :: delay,center,width,prime_hz
    type(decode_result) :: primed,warm,cold

    prime_rate=nsps
    prime_chunk=5*nframe/4
    chunk=prime_chunk
    delay=0.65
    center=1500.0
    width=100.0
    prime_hz=1500.0
    origin=0_int64
    select case(scenario)
    case(1)
       delay=1.10
    case(2)
       delay=0.15
    case(4)
       prime_hz=1490.0
    case(5)
       center=1600.0
       width=300.0
    case(6)
       origin=120000_int64
    case(7)
       prime_rate=320
       prime_chunk=5*59*prime_rate/4
    case(8)
       chunk=3*nframe/2
    end select

    allocate(prime_pcm(prime_chunk),pcm(chunk))
    call make_audio(prime_pcm,prime_rate,prime_hz,delay,payload)
    call make_audio(pcm,nsps,1500.0,delay,payload)

    call jtty_release_fft_resources()
    call decode_window(prime_pcm,prime_rate,1500.0,100.0,0_int64, &
         .false.,delay,payload,primed)
    call decode_window(pcm,nsps,center,width,origin, &
         .true.,delay,payload,warm)
    call jtty_release_fft_resources()
    call decode_window(pcm,nsps,center,width,origin, &
         .true.,delay,payload,cold)

    call expect(cold%count.gt.0,'fixture must deliver a message',scenario)
    call compare_results(warm,cold,scenario)
  end subroutine verify_cache

  subroutine make_audio(pcm,nsps,target_hz,delay,payload)
    integer(int16), intent(out) :: pcm(:)
    integer, intent(in) :: nsps
    real, intent(in) :: target_hz,delay
    integer, intent(out) :: payload(PAYLOAD_BITS)
    integer :: tones(59*MAX_FRAMES),nsym,nframes,start,count
    real, allocatable :: wave(:),mixture(:)
    complex, allocatable :: complex_wave(:)
    character(len=80) :: message
    character(len=34) :: frames(MAX_FRAMES)

    allocate(mixture(size(pcm)))
    mixture=0.0
    message='N2PPI'
    call genjtty(message,tones,nsym)
    call expect(nsym.eq.59,'target must occupy one frame',0)
    allocate(wave(nsym*nsps),complex_wave(nsym*nsps))
    call gen_jttywave(tones,nsym,nsps,2.0,12000.0,target_hz, &
         complex_wave,wave,0,size(wave))
    start=1801
    count=min(size(wave),size(pcm)-start+1)
    mixture(start:start+count-1)=14000.0*wave(:count)

    message='W9XYZ'
    call pack_jtty(message,frames,nframes)
    call expect(nframes.eq.1,'interferer must occupy one frame',0)
    read(frames(1),'(34i1)') payload
    call genjtty_frames(frames,nframes,tones,nsym)
    call gen_jttywave(tones,nsym,nsps,2.0,12000.0,1530.0, &
         complex_wave,wave,0,size(wave))
    start=nint(delay*12000.0)+1
    count=min(size(wave),size(pcm)-start+1)
    mixture(start:start+count-1)=mixture(start:start+count-1)+9000.0*wave(:count)
    pcm=int(nint(mixture),int16)
  end subroutine make_audio

  subroutine decode_window(pcm,nsps,center,width,origin,retro,delay,payload,result)
    integer(int16), intent(in) :: pcm(:)
    integer, intent(in) :: nsps,payload(PAYLOAD_BITS)
    real, intent(in) :: center,width,delay
    integer(int64), intent(in) :: origin
    logical, intent(in) :: retro
    type(decode_result), intent(out) :: result

    call reset_decode_search_state()
    call discard_pending_updates()
    ndecodes=0
    next_message_id=1_int64
    explicit_receive_context=.true.
    sample_origin=origin
    interferer_pending=retro
    interferer_f1=1530.0
    interferer_tsync=real(origin,real64)/12000.0_real64+real(delay,real64)
    interferer_payload=payload
    call jtty_mdecode(1,1,pcm,size(pcm),nsps,-1,200,2800,center,width,4.6)
    result%count=npending
    result%decodes=ndecodes
    result%subtractions=nsubtracted
    call expect(npending.le.MAX_ACTIVE_MESSAGES,'result capacity',0)
    if(npending.gt.0) result%updates(:npending)= &
         pending_updates(pending_first:pending_first+npending-1)
    if(nsubtracted.gt.0) then
       result%frequencies(:nsubtracted)=subtracted_f1(:nsubtracted)
       result%times(:nsubtracted)=subtracted_tsync(:nsubtracted)
       result%payloads(:,:nsubtracted)=subtracted_payload(:,:nsubtracted)
    endif
  end subroutine decode_window

  subroutine compare_results(actual,expected,scenario)
    type(decode_result), intent(in) :: actual,expected
    integer, intent(in) :: scenario
    integer :: i,n

    call expect(actual%count.eq.expected%count,'message count',scenario)
    call expect(actual%decodes.eq.expected%decodes,'decode count',scenario)
    call expect(actual%subtractions.eq.expected%subtractions,'subtraction count',scenario)
    do i=1,expected%count
       call expect(actual%updates(i)%message_id.eq.expected%updates(i)%message_id, &
            'message identity',scenario)
       call expect(actual%updates(i)%decoded.eq.expected%updates(i)%decoded, &
            'message text',scenario)
       call expect(actual%updates(i)%complete.eqv.expected%updates(i)%complete, &
            'message completion',scenario)
       call expect(actual%updates(i)%terminal.eq.expected%updates(i)%terminal, &
            'terminal state',scenario)
       ! FFTW can choose a different rounding path after resource teardown.
       call expect(abs(actual%updates(i)%f1-expected%updates(i)%f1).le.0.001, &
            'message frequency',scenario)
       call expect(abs(actual%updates(i)%start_tsync-expected%updates(i)%start_tsync).le.1.e-5_real64, &
            'message start time',scenario)
       call expect(abs(actual%updates(i)%latest_tsync-expected%updates(i)%latest_tsync).le.1.e-5_real64, &
            'message continuation time',scenario)
       call expect(abs(actual%updates(i)%snr-expected%updates(i)%snr).le.0.01, &
            'message SNR',scenario)
    enddo
    n=expected%subtractions
    call expect(all(actual%payloads(:,:n).eq.expected%payloads(:,:n)), &
         'subtracted payloads',scenario)
    call expect(all(abs(actual%frequencies(:n)-expected%frequencies(:n)).le.0.001), &
         'subtracted frequencies',scenario)
    call expect(all(abs(actual%times(:n)-expected%times(:n)).le.1.e-5_real64), &
         'subtracted times',scenario)
  end subroutine compare_results

  subroutine expect(condition,description,scenario)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    integer, intent(in) :: scenario

    if(condition) return
    print *, 'Surface cache mismatch:',scenario,description
    if(scenario.ge.1 .and. scenario.le.size(scenarios)) print *, trim(scenarios(scenario))
    stop 1
  end subroutine expect
end program test_jtty_surface_cache
