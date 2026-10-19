module decoder_engine_test_ft8_waveform
  implicit none
contains
  subroutine make_ft8_test_signal(message,carrier,samples,payload,expected_tones,start_sample)
    use iso_c_binding, only: c_int,c_int8_t,c_int16_t,c_int64_t
    use packjt77, only: pack77_state
    use ft8_codec_context, only: genft8_for_state
    implicit none
    integer, parameter :: symbol_samples=1920,wave_samples=79*symbol_samples
    integer(c_int16_t), intent(out) :: samples(180000)
    integer(c_int8_t), intent(out) :: payload(77)
    integer(c_int8_t), intent(out) :: expected_tones(79)
    integer(c_int), intent(in) :: start_sample
    real, intent(in) :: carrier
    character(len=37), intent(in) :: message
    type(pack77_state) :: knowledge
    real :: wave(wave_samples)
    complex :: complex_wave(1)
    integer :: tones(79),i3,n3,i,j
    integer(c_int64_t) :: random_state
    character(len=37) :: sent

    i3=-1
    n3=-1
    call genft8_for_state(knowledge,message,i3,n3,sent,payload,tones)
    expected_tones=int(tones,c_int8_t)
    if(sent.ne.message) error stop 'engine test waveform message did not encode'
    call gen_ft8wave(tones,79,symbol_samples,2.0,12000.0,carrier, &
         complex_wave,wave,0,wave_samples)
    random_state=1234567_c_int64_t
    do i=1,size(samples)
       random_state=mod(16807_c_int64_t*random_state,2147483647_c_int64_t)
       samples(i)=int(mod(random_state,21_c_int64_t)-10_c_int64_t,c_int16_t)
    enddo
    do i=1,wave_samples
       j=start_sample+i
       if(j>=1.and.j<=size(samples)) samples(j)=samples(j)+int(1500.0*wave(i),c_int16_t)
    enddo
  end subroutine make_ft8_test_signal
end module decoder_engine_test_ft8_waveform

subroutine decoder_engine_test_signal(samples,payload,expected_tones,start_sample) bind(C)
  use iso_c_binding, only: c_int,c_int8_t,c_int16_t
  use decoder_engine_test_ft8_waveform, only: make_ft8_test_signal
  implicit none
  integer(c_int16_t), intent(out) :: samples(180000)
  integer(c_int8_t), intent(out) :: payload(77),expected_tones(79)
  integer(c_int), value :: start_sample
  character(len=37), parameter :: message='CQ K1JT FN20'

  call make_ft8_test_signal(message,1500.0,samples,payload,expected_tones,start_sample)
end subroutine decoder_engine_test_signal

subroutine decoder_engine_test_ft8_message(text,carrier,samples,payload,expected_tones,start_sample) bind(C)
  use iso_c_binding, only: c_int,c_char,c_float,c_int8_t,c_int16_t
  use decoder_engine_test_ft8_waveform, only: make_ft8_test_signal
  implicit none
  character(c_char), intent(in) :: text(37)
  real(c_float), value :: carrier
  integer(c_int16_t), intent(out) :: samples(180000)
  integer(c_int8_t), intent(out) :: payload(77),expected_tones(79)
  integer(c_int), value :: start_sample
  character(len=37) :: message

  message=transfer(text,message)
  call make_ft8_test_signal(message,real(carrier),samples,payload,expected_tones,start_sample)
end subroutine decoder_engine_test_ft8_message

subroutine decoder_engine_test_records(records,record_size) bind(C)
  use iso_c_binding, only: c_size_t,c_sizeof
  use decoder_engine_types
  implicit none
  type(engine_observation), intent(out) :: records(2)
  integer(c_size_t), intent(out) :: record_size

  record_size=c_sizeof(records(1))
  records=engine_observation()
  records(1)%mode=engine_mode_ft4
  records(1)%dt_seconds=0.25
  records(1)%ft4%payload77(77)=1
  records(1)%ft4%tones(103)=3
  records(1)%ft4%payload_origin=engine_payload_decoded
  records(1)%ft4%has_tones=1
  records(1)%ft4%has_waveform_start=1
  records(1)%ft4%waveform_start_seconds=-0.125
  records(2)%variant=2
  records(2)%superfox%symbols(50)=127
  records(2)%superfox%kind=sf_kind_verification
  records(2)%superfox%child_index=4
end subroutine decoder_engine_test_records

subroutine decoder_engine_test_message(mode,text,samples,payload,expected_tones,start_sample) bind(C)
  use iso_c_binding, only: c_int,c_char,c_int8_t,c_int16_t,c_int64_t
  use packjt77, only: pack77_state
  use ft4_codec, only: genft4_for_state
  use ft8_codec_context, only: genft8_for_state
  implicit none
  integer(c_int), value :: mode,start_sample
  character(c_char), intent(in) :: text(37)
  integer(c_int16_t), intent(out) :: samples(180000)
  integer(c_int8_t), intent(out) :: payload(77),expected_tones(103)
  type(pack77_state) :: knowledge
  real :: wave(151680)
  complex :: complex_wave(1)
  integer :: tones(103),i3,n3,i,j,nwave
  integer(c_int64_t) :: random_state
  character(len=37) :: message,sent

  message=transfer(text,message)
  if(mode==5) then
     call genft4_for_state(knowledge,message,0,sent,payload,tones)
     nwave=60480
     call gen_ft4wave(tones,103,576,12000.0,1500.0,complex_wave,wave,0,nwave)
  else
     tones=0
     i3=-1
     n3=-1
     call genft8_for_state(knowledge,message,i3,n3,sent,payload,tones(:79))
     nwave=151680
     call gen_ft8wave(tones,79,1920,2.0,12000.0,1500.0,complex_wave,wave,0,nwave)
  endif
  if(sent/=message) error stop 'mixed-mode waveform message did not encode'
  expected_tones=int(tones,c_int8_t)
  random_state=1234567_c_int64_t
  do i=1,size(samples)
     random_state=mod(16807_c_int64_t*random_state,2147483647_c_int64_t)
     samples(i)=int(mod(random_state,21_c_int64_t)-10_c_int64_t,c_int16_t)
  enddo
  do i=1,nwave
     j=start_sample+i
     if(j>=1.and.j<=size(samples)) samples(j)=samples(j)+int(1500.0*wave(i),c_int16_t)
  enddo
end subroutine

subroutine decoder_engine_test_residual(samples) bind(C)
  use iso_c_binding, only: c_float
  use ft8_mod1, only: dd8
  implicit none
  real(c_float), intent(out) :: samples(180000)
  samples=dd8
end subroutine

subroutine decoder_engine_test_qso_snapshot(valid,suppressed,message,dt,partner) bind(C)
  use iso_c_binding, only: c_int,c_float,c_char,c_null_char
  use ft8_mod1, only: ft8_qso_snapshot,get_ft8_qso_hint_state
  implicit none
  integer(c_int), intent(out) :: valid,suppressed
  character(c_char), intent(out) :: message(38),partner(13)
  real(c_float), intent(out) :: dt
  type(ft8_qso_snapshot) :: snapshot
  integer :: i

  call get_ft8_qso_hint_state(snapshot)
  valid=merge(1,0,snapshot%valid)
  suppressed=merge(1,0,snapshot%suppressed)
  message=c_null_char
  partner=c_null_char
  do i=1,len_trim(snapshot%message)
     message(i)=snapshot%message(i:i)
  enddo
  do i=1,len_trim(snapshot%partner)
     partner(i)=snapshot%partner(i:i)
  enddo
  dt=snapshot%dt
end subroutine decoder_engine_test_qso_snapshot

subroutine decoder_engine_test_host_windows() bind(C)
  use iso_c_binding, only: c_short,c_int8_t,c_int64_t,c_sizeof
  use decoder_engine, only: engine_host_decode
  use ft8_engine_kernel, only: params_block
  use ft8_mod1, only: dd8
  use decode_completion_module, only: decode_completion_result
  implicit none
  type(params_block) :: params
  type(decode_completion_result) :: completion
  integer(c_int8_t) :: zeros(c_sizeof(params))
  integer(c_short), target :: samples(180000)
  integer :: extent,case_no

  zeros=0
  params=transfer(zeros,params)
  params%nmode=8
  params%ntr=15
  params%nutc=120000
  params%nzhsym=49
  params%newdat=.true.
  params%lmultift8=.true.
  params%nmt=1
  params%nfqso=3000
  params%nfa=1000
  params%nfb=2000
  params%nagainfil=.true.
  params%mycall=' '
  params%mybcall=' '
  params%hiscall=' '
  params%hisbcall=' '
  params%mygrid=' '
  params%hisgrid=' '
  samples=2
  do case_no=1,2
     extent=180000
     if(case_no==2) extent=100000
     call engine_host_decode(samples,params,12000,completion,0, &
          int(900+case_no,c_int64_t),int(900+case_no,c_int64_t),1,extent)
     extent=min(extent,49*3456)
     if(any(dd8(:extent)/=2.0)) error stop 'host discarded available MTD audio'
     if(any(dd8(extent+1:)/=0.0)) error stop 'host retained audio outside the MTD window'
     if(.not.completion%available) error stop 'host window probe did not complete'
  enddo
end subroutine

subroutine decoder_engine_test_legacy_isolation() bind(C)
  use packjt77, only: nzhash,recent_calls
  implicit none
  if(nzhash/=0.or.any(recent_calls/='')) error stop 'engine mutated legacy codec knowledge'
end subroutine

subroutine decoder_engine_test_gui_codec() bind(C)
  use packjt77, only: pack77_state,pack77_for_state,unpack77_for_state,unpack77
  use decoder_codec_context, only: get_decoder_codec_state
  use ft4_codec, only: ft4_tones_from_77bits,ft4_scramble
  implicit none
  type(pack77_state), pointer :: state
  type(pack77_state) :: encoder
  character(len=37) :: message,sent,resolved
  character(len=77) :: bits
  integer(kind=1) :: payload(77),canonical(77)
  integer :: tones(103),expected(103),i3,n3
  logical :: ok

  message='CQ K1JT FN20'
  call genft4(message,0,sent,payload,tones)
  canonical=payload
  call ft4_scramble(canonical)
  call ft4_tones_from_77bits(canonical,expected)
  if(any(expected/=tones)) error stop 'legacy FT4 encoder bit convention changed'
  state=>get_decoder_codec_state()
  message='PJ2/W1AW <K1JT> RR73'
  i3=-1
  n3=-1
  call pack77_for_state(encoder,message,i3,n3,bits)
  call unpack77_for_state(state,bits,1,resolved,ok)
  if(.not.ok.or.resolved/=message) error stop 'GUI codec did not retain FT4 knowledge'
  call unpack77(bits,1,resolved,ok)
  if(.not.ok.or.resolved/='PJ2/W1AW <...> RR73') error stop 'GUI codec leaked into legacy store'
end subroutine
