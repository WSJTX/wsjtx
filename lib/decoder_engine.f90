module decoder_engine
  use, intrinsic :: iso_c_binding
  use decoder_engine_types
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use packjt77, only: pack77_state,initialize_pack77_state,reset_pack77_state
  use ft8_engine_kernel, only: ft8_kernel_state,params_block,run_ft8_kernel,reset_ft8_kernel,release_ft8_input
  use decoder_callbacks, only: decoder_callback_context,counting_ft4_decoder,ft4_decoded, &
       counting_jt9_decoder,jt9_decoded,counting_jt65_decoder,jt65_decoded, &
       counting_jt4_decoder,jt4_decoded, &
       counting_q65_decoder,q65_decoded,counting_fst4_decoder,fst4_decoded
  use q65_decode, only: q65_options
  use fst4_decode, only: fst4_options
  use fst4_host_support, only: prepare_fst4_host,finish_fst4_host
  use q65_host_support, only: load_q65_history,save_q65_history,write_q65_curves,write_q65_diagnostic
  use q65_callers, only: Q65_MAX_CALLERS
  use types, only: q3list
  use jt4_decode, only: jt4_options,jt4_call,jt4_average_entry
  use jt4_host_support, only: convert_jt4_host_options,load_jt4_calls,write_jt4_averages, &
       prepare_jt4_host,write_jt4_diagnostic
  use jt65_decode, only: jt65_options,jt65_average_entry
  use jt65_host_support, only: load_jt65_calls,write_jt65_averages
  use decode_completion_module, only: decode_completion_result,set_decode_completion,write_decode_progress
  use prog_args, only: temp_dir
  use timer_module, only: timer
  use streaming_emit, only: streaming_emit_enabled
  implicit none
  private
  public :: engine_host_decode
  integer(c_int), parameter :: ok=0,invalid=1,busy=2,unsupported=3,capacity=4,internal_error=5
  integer, parameter :: evidence_capacity=1024

  type, bind(C) :: engine_options
     integer(c_int) :: abi_version
  end type
  type, bind(C) :: engine_capabilities
     integer(c_int) :: abi_version,supported_modes,cancellation,concurrent_sessions,evidence_capacity
  end type
  type, bind(C) :: ft8_options
     integer(c_int) :: half_symbol_stage=50
     integer(c_int) :: reuse_spectrum=0
     integer(c_int) :: utc=0
     integer(c_int) :: qso_progress=0
     integer(c_int) :: receive_frequency_hz=0
     integer(c_int) :: transmit_frequency_hz=0
     integer(c_int) :: search_low_hz=0
     integer(c_int) :: search_high_hz=0
     integer(c_int) :: tolerance_hz=0
     integer(c_int) :: depth=0
     integer(c_int) :: ap_width_hz=0
     integer(c_int) :: contest=0
     integer(c_int) :: candidate_thinning=0
     integer(c_int) :: time_center=0
     integer(c_int) :: cycles=0
     integer(c_int) :: trials=0
     integer(c_int) :: last_transmit=0
     integer(c_int) :: delay=0
     integer(c_int) :: threads=0
     integer(c_int) :: receive_sensitivity=0
     integer(c_int) :: discard_leading_seconds=0
     integer(c_int) :: date=0
     integer(c_int) :: ap_enabled=0
     integer(c_int) :: cq_only=0
     integer(c_int) :: even_sequence=0
     integer(c_int) :: superfox=0
     integer(c_int) :: filtered_retry=0
     integer(c_int) :: stop_hint=0
     integer(c_int) :: mtd=0
     integer(c_int) :: low_threshold=0
     integer(c_int) :: subtract_pass=0
     integer(c_int) :: transmitting=0
     integer(c_int) :: hide_duplicates=0
     integer(c_int) :: hound=0
     integer(c_int) :: standard_mycall=0
     integer(c_int) :: standard_hiscall=0
     integer(c_int) :: ap_mycall=0
     integer(c_int) :: mode_changed=0
     integer(c_int) :: band_changed=0
     integer(c_int) :: dx_search=0
     integer(c_int) :: wide_dx_search=0
     integer(c_int) :: multiple_instances=0
     integer(c_int) :: skip_first_message=0
     real(c_float) :: eme_delay_seconds=0
     character(c_char) :: mycall(12)=' '
     character(c_char) :: my_base_call(12)=' '
     character(c_char) :: hiscall(12)=' '
     character(c_char) :: his_base_call(12)=' '
     character(c_char) :: mygrid(6)=' '
     character(c_char) :: hisgrid(6)=' '
  end type
  type, bind(C) :: ft4_options
     integer(c_int) :: utc=0
     integer(c_int) :: qso_progress=0
     integer(c_int) :: receive_frequency_hz=0
     integer(c_int) :: search_low_hz=0
     integer(c_int) :: search_high_hz=0
     integer(c_int) :: depth=0
     integer(c_int) :: cq_only=0
     integer(c_int) :: contest=0
     character(c_char) :: mycall(12)=' '
     character(c_char) :: hiscall(12)=' '
  end type
  type, bind(C) :: jt9_options
     integer(c_int) :: utc=0
     integer(c_int) :: receive_frequency_hz=0
     integer(c_int) :: search_low_hz=0
     integer(c_int) :: search_high_hz=0
     integer(c_int) :: tolerance_hz=0
     integer(c_int) :: depth=0
     integer(c_int) :: submode=0
  end type
  type, bind(C) :: engine_jt65_options
     integer(c_int) :: utc=0,qso_progress=0,receive_frequency_hz=0,search_low_hz=0,search_high_hz=0
     integer(c_int) :: tolerance_hz=0,depth=0,submode=0,min_sync=0,passes=0,trials=0,aggressiveness=0
     integer(c_int) :: single_decode=0,vhf=0,averaging=0,auto_clear=0,deep_search=0,ap_enabled=0
     character(c_char) :: mycall(12)=' ',hiscall(12)=' ',hisgrid(6)=' '
  end type
  type, bind(C) :: engine_jt65_call
     character(c_char) :: call(12),grid(4)
  end type
  type, bind(C) :: engine_jt65_average_entry
     integer(c_int) :: utc,frequency_hz,polarity,used
     real(c_float) :: sync,dt_seconds
  end type
  type, bind(C) :: engine_q65_options
     integer(c_int) :: utc=0,period_seconds=60,submode=0,receive_frequency_hz=0,tolerance_hz=0
     integer(c_int) :: search_low_hz=0,search_high_hz=0,depth=0,max_drift_symbol_rates=0
     integer(c_int) :: qso_progress=0,contest=0,averaging=0,auto_clear=0,single_decode=0
     integer(c_int) :: extended_eme_search=0,pileup=0,ap_cq_only=0,now_seconds=0
     character(c_char) :: mycall(12)=' ',hiscall(12)=' ',hisgrid(6)=' '
  end type
  type, bind(C) :: engine_q65_caller
     character(c_char) :: call(12),grid(4)
     integer(c_int) :: last_seen,frequency_hz
  end type
  type, bind(C) :: engine_q65_snapshot
     integer(c_int) :: even_count=0,odd_count=0,curve_count=0,curve_average_count=0
     real(c_float) :: frequency_step_hz=0,dt_seconds=0
  end type
  type, bind(C) :: engine_fst4_options
     integer(c_int) :: utc=0,period_seconds=15,receive_frequency_hz=1500,search_low_hz=200,search_high_hz=4000
     integer(c_int) :: tolerance_hz=100,depth=1,qso_progress=0,single_decode=0,ap_cq_only=0
     integer(c_int) :: blanker_mode=0,blanker_percent=0,measure_doppler=0
     real(c_float) :: eme_delay_seconds=0
     character(c_char) :: mycall(12)=' ',hiscall(12)=' '
  end type
  type, bind(C) :: engine_fst4w_call
     character(c_char) :: call_grid(20)
  end type
  type, bind(C) :: engine_jt4_call
     character(c_char) :: call(12),grid(4)
  end type
  type, bind(C) :: engine_jt4_average_entry
     integer(c_int) :: utc,frequency_hz,polarity,used
     real(c_float) :: sync,dt_seconds
  end type
  type, bind(C) :: attempt_request
     integer(c_int64_t) :: input_id=0,analysis_id=0
     integer(c_int) :: attempt_no=0,mode=8,phase=2,source=0
     type(ft8_options) :: ft8
     type(ft4_options) :: ft4
     type(jt9_options) :: jt9
     type(engine_jt65_options) :: jt65
     type(engine_q65_options) :: q65
     type(engine_fst4_options) :: fst4
     type(engine_jt4_options) :: jt4
  end type
  type, bind(C) :: audio_view
     type(c_ptr) :: samples
     integer(c_int) :: sample_count,sample_rate_hz
  end type
  type, bind(C) :: attempt_outcome
     integer(c_int) :: status=ok,observation_count=0,retained_count=0,evidence_dropped=0
  end type
  abstract interface
     subroutine observation_callback(observation,user) bind(C)
       import :: engine_observation,c_ptr
       type(engine_observation), intent(in) :: observation
       type(c_ptr), value :: user
     end subroutine
  end interface
  type :: engine_session
     type(pack77_state) :: knowledge
     type(ft8_kernel_state) :: kernel
     type(counting_ft4_decoder) :: ft4
     ! GCC 13 can finalize an uninitialized temporary for an embedded finalizable extension.
     type(counting_jt9_decoder), allocatable :: jt9
     type(counting_jt4_decoder), allocatable :: jt4
     type(counting_jt65_decoder), allocatable :: jt65
     type(counting_q65_decoder), allocatable :: q65
     type(counting_fst4_decoder), allocatable :: fst4
     type(attempt_request) :: request
     type(engine_observation), allocatable :: evidence(:)
     integer(c_short), allocatable :: audio(:)
     integer(c_int64_t) :: input_id=0,analysis_id=0
     integer :: input_mode=0,input_sample_count=0,input_sample_rate=0
     integer :: last_attempt=0,retained=0,dropped=0,emitted=0
     logical :: running=.false.,render_legacy=.false.
     type(c_funptr) :: callback=c_null_funptr
     type(c_ptr) :: user=c_null_ptr
     type(decode_completion_result) :: completion
  end type
  type(engine_session), allocatable, target, save :: session
  integer(c_int), target, save :: token=1
  type(c_ptr), save :: host_handle=c_null_ptr
  integer(c_int64_t), save :: fallback_input=0,fallback_analysis=0
  integer, save :: fallback_utc=-1

contains

  pure integer function analysis_extent(mode,period,sample_rate) result(samples)
    integer, intent(in) :: mode
    integer, optional, intent(in) :: period,sample_rate
    select case(mode)
    case(engine_mode_ft4)
       samples=72576
    case(engine_mode_ft8)
       samples=180000
    case(engine_mode_jt4)
       samples=720000
       if(present(sample_rate)) samples=60*sample_rate
    case(engine_mode_jt9,engine_mode_jt65)
       samples=720000
    case(engine_mode_q65)
       samples=0
       if(present(period)) then
          if(any(period==[15,30,60,120,300])) samples=period*12000
       endif
    case(engine_mode_fst4,engine_mode_fst4w)
       samples=0
       if(present(period)) then
          if(any(period==[15,30,60,120,300,900,1800])) samples=period*12000
       endif
    case default
       samples=0
    end select
  end function analysis_extent

  subroutine initialize_session(state)
    type(engine_session), intent(inout) :: state
    allocate(state%jt9)
    allocate(state%jt4)
    allocate(state%jt65)
    allocate(state%q65)
    allocate(state%fst4)
    allocate(state%audio(analysis_extent(engine_mode_ft8)),state%evidence(evidence_capacity))
    state%audio=0
    call initialize_pack77_state(state%knowledge)
    call reset_ft8_kernel(state%kernel)
  end subroutine initialize_session

  logical function valid_handle(handle)
    type(c_ptr), value :: handle
    valid_handle=allocated(session).and.c_associated(handle,c_loc(token))
  end function

  integer(c_int) function engine_create(options,handle) bind(C,name='decoder_engine_create') result(status)
    type(engine_options), intent(in) :: options
    type(c_ptr), intent(out) :: handle
    handle=c_null_ptr
    status=unsupported
    if(options%abi_version/=engine_abi) return
    status=busy
    if(allocated(session)) return
    allocate(session)
    call initialize_session(session)
    handle=c_loc(token)
    status=ok
  end function

  integer(c_int) function engine_get_capabilities(handle,caps) &
       bind(C,name='decoder_engine_get_capabilities') result(status)
    type(c_ptr), value :: handle
    type(engine_capabilities), intent(out) :: caps
    caps=engine_capabilities(engine_abi, &
         ior(ior(engine_support_ft8,engine_support_ft4), &
         ior(ior(ior(engine_support_jt9,engine_support_jt65),engine_support_q65), &
         ior(ior(engine_support_fst4,engine_support_fst4w),engine_support_jt4))), &
         0,0,evidence_capacity)
    status=invalid
    if(valid_handle(handle)) status=ok
  end function

  recursive integer(c_int) function engine_decode(handle,request,audio,callback,user,outcome) &
       bind(C,name='decoder_engine_decode') result(status)
    type(c_ptr), value :: handle,user
    type(attempt_request), intent(in) :: request
    type(audio_view), intent(in) :: audio
    type(c_funptr), value :: callback
    type(attempt_outcome), intent(out) :: outcome
    status=decode_attempt(handle,request,audio,callback,user,outcome,.false.,0)
  end function

  recursive integer(c_int) function decode_attempt(handle,request,audio,callback,user,outcome,render,progress) result(status)
    type(c_ptr), intent(in) :: handle,user
    type(attempt_request), intent(in) :: request
    type(audio_view), intent(in) :: audio
    type(c_funptr), intent(in) :: callback
    type(attempt_outcome), intent(out) :: outcome
    logical, intent(in) :: render
    integer, intent(in) :: progress
    integer(c_short), pointer :: samples(:)
    integer :: analysis_samples,allocation_status
    integer(c_short), allocatable :: grown_audio(:)
    type(params_block) :: params
    type(decoder_callback_context) :: context
    outcome=attempt_outcome()
    status=invalid
    if(.not.valid_handle(handle)) go to 900
    status=busy
    if(session%running) go to 900
    status=unsupported
    if(request%mode/=engine_mode_jt4.and.request%mode/=engine_mode_ft8.and.request%mode/=engine_mode_ft4.and. &
         request%mode/=engine_mode_jt9.and.request%mode/=engine_mode_jt65.and.request%mode/=engine_mode_q65.and. &
         request%mode/=engine_mode_fst4.and.request%mode/=engine_mode_fst4w) go to 900
    if(request%mode==engine_mode_jt4) then
       if(audio%sample_rate_hz/=11025.and.audio%sample_rate_hz/=12000) go to 900
    else
       if(audio%sample_rate_hz/=12000) go to 900
    endif
    status=invalid
    if(request%input_id<=0.or.request%analysis_id<=0.or.request%attempt_no<=0) go to 900
    if(request%phase<1.or.request%phase>3.or.request%source<0.or.request%source>1) go to 900
    if(request%mode==engine_mode_fst4.or.request%mode==engine_mode_fst4w) then
       analysis_samples=analysis_extent(request%mode,request%fst4%period_seconds)
    else
       analysis_samples=analysis_extent(request%mode,request%q65%period_seconds,audio%sample_rate_hz)
    endif
    if(audio%sample_count<1.or.audio%sample_count>analysis_samples) go to 900
    if(.not.c_associated(audio%samples)) go to 900
    if(request%mode==engine_mode_jt4) then
       if(request%phase==1) go to 900
       if(.not.valid_jt4_options(request%jt4)) go to 900
    else if(request%mode==engine_mode_ft4) then
       if(request%phase==1) go to 900
       if(request%ft4%search_low_hz<0.or.request%ft4%search_high_hz>6000.or. &
            request%ft4%search_low_hz>request%ft4%search_high_hz) go to 900
       if(request%ft4%depth<1.or.request%ft4%depth>3) go to 900
       if(request%ft4%qso_progress<0.or.request%ft4%qso_progress>5) go to 900
       if(request%ft4%contest<0.or.request%ft4%contest>7) go to 900
       if(request%ft4%cq_only<0.or.request%ft4%cq_only>1) go to 900
    else if(request%mode==engine_mode_jt9) then
       if(request%phase==1) go to 900
       if(request%jt9%search_low_hz<0.or.request%jt9%search_high_hz>5000.or. &
            request%jt9%search_low_hz>request%jt9%search_high_hz) go to 900
       if(request%jt9%receive_frequency_hz<0.or.request%jt9%receive_frequency_hz>5000) go to 900
       if(request%jt9%tolerance_hz<0.or.request%jt9%tolerance_hz>5000) go to 900
       if(request%jt9%depth<1.or.request%jt9%depth>3) go to 900
       if(request%jt9%submode<0.or.request%jt9%submode>7) go to 900
    else if(request%mode==engine_mode_jt65) then
       if(request%phase==1) go to 900
       if(.not.valid_jt65_options(request%jt65)) go to 900
    else if(request%mode==engine_mode_q65) then
       if(request%phase==1) go to 900
       if(.not.valid_q65_options(request%q65)) go to 900
    else if(request%mode==engine_mode_fst4.or.request%mode==engine_mode_fst4w) then
       if(request%phase==1) go to 900
       if(.not.valid_fst4_options(request%fst4)) go to 900
    else
       if(request%ft8%search_low_hz<0.or.request%ft8%search_high_hz>6000.or. &
            request%ft8%search_low_hz>request%ft8%search_high_hz) go to 900
       if(request%ft8%half_symbol_stage<41.or.request%ft8%half_symbol_stage>50) go to 900
       if(request%ft8%reuse_spectrum<0.or.request%ft8%reuse_spectrum>1) go to 900
       if(request%ft8%threads<0.or.request%ft8%threads>24) go to 900
    endif
    if(session%input_id/=0.and.session%input_id/=request%input_id) go to 900
    if(session%input_id/=0.and.session%input_mode/=request%mode) go to 900
    if(request%mode==engine_mode_jt4.and.session%input_id/=0) then
       if(audio%sample_count/=session%input_sample_count.or.audio%sample_rate_hz/=session%input_sample_rate) go to 900
    endif
    if(request%mode==engine_mode_q65.and.session%input_id/=0) then
       if(audio%sample_count/=session%input_sample_count) go to 900
    endif
    if((request%mode==engine_mode_fst4.or.request%mode==engine_mode_fst4w).and.session%input_id/=0) then
       if(audio%sample_count/=session%input_sample_count) go to 900
       if(request%fst4%period_seconds/=session%request%fst4%period_seconds) go to 900
    endif
    if(session%analysis_id==request%analysis_id.and.request%attempt_no<=session%last_attempt) go to 900

    if(size(session%audio)<analysis_samples) then
       allocate(grown_audio(analysis_samples),stat=allocation_status)
       if(allocation_status/=0) then
          status=internal_error
          go to 900
       endif
       call move_alloc(grown_audio,session%audio)
    endif
    session%running=.true.
    session%request=request
    session%input_id=request%input_id
    session%input_mode=request%mode
    session%input_sample_count=audio%sample_count
    session%input_sample_rate=audio%sample_rate_hz
    session%analysis_id=request%analysis_id
    session%last_attempt=request%attempt_no
    session%callback=callback
    session%user=user
    session%render_legacy=render
    session%emitted=0
    call c_f_pointer(audio%samples,samples,[audio%sample_count])
    session%audio(1:audio%sample_count)=samples
    session%audio(audio%sample_count+1:analysis_samples)=0
    status=ok
    context%sink=>collect_observation
    context%superfox_sink=>collect_superfox
    context%sink_user=handle
    context%render_legacy=render
    if(request%mode==engine_mode_jt4) then
       call run_jt4_attempt(request%jt4,request%phase,audio%sample_count,audio%sample_rate_hz,context,progress,status)
    else if(request%mode==engine_mode_ft4) then
       call run_ft4_attempt(request%ft4,request%phase,context,progress)
    else if(request%mode==engine_mode_jt9) then
       call run_jt9_attempt(request%jt9,request%phase,audio%sample_count,context,progress)
    else if(request%mode==engine_mode_jt65) then
       call run_jt65_attempt(request%jt65,request%phase,audio%sample_count,context,progress)
    else if(request%mode==engine_mode_q65) then
       call run_q65_attempt(request%q65,request%phase,audio%sample_count,context,progress,status)
    else if(request%mode==engine_mode_fst4.or.request%mode==engine_mode_fst4w) then
       call run_fst4_attempt(request%fst4,request%mode,request%phase,context,progress,status)
    else
       call request_to_params(request,audio%sample_count,params)
       call run_ft8_kernel(session%kernel,session%knowledge,session%audio(:180000), &
            params,session%completion,progress,context)
    endif
    outcome%observation_count=session%emitted
    outcome%retained_count=session%retained
    outcome%evidence_dropped=session%dropped
    session%callback=c_null_funptr
    session%user=c_null_ptr
    session%running=.false.
900 outcome%status=status
  end function

  subroutine open_legacy_output(phase,ios)
    integer, intent(in) :: phase
    integer, intent(out) :: ios
    integer :: tries

    do tries=1,4
       if(phase==3) then
          open(13,file=trim(temp_dir)//'/decoded.txt',status='unknown',position='append',iostat=ios)
       else
          open(13,file=trim(temp_dir)//'/decoded.txt',status='unknown',iostat=ios)
       endif
       if(ios==0) exit
       if(tries<4) call sleep_msec(10)
    enddo
  end subroutine open_legacy_output

  subroutine run_ft4_attempt(options,phase,callback_context,progress)
    type(ft4_options), intent(in) :: options
    integer, intent(in) :: phase,progress
    type(decoder_callback_context), intent(in) :: callback_context
    type(decoder_callback_context) :: context
    character(len=12) :: mycall,hiscall

    context=callback_context
    context%nutc=options%utc
    context%nfqso=options%receive_frequency_hz
    context%ncontest=options%contest
    context%ios13=-1
    if(context%render_legacy) call open_legacy_output(phase,context%ios13)
    session%ft4%context=context
    session%ft4%decoded=0
    mycall=transfer(options%mycall,mycall)
    hiscall=transfer(options%hiscall,hiscall)
    if(any(session%audio(:72576)/=0)) then
       call timer('decft4  ',0)
       call session%ft4%decode(ft4_decoded,session%audio(:72576),options%qso_progress, &
            options%receive_frequency_hz,options%search_low_hz,options%search_high_hz, &
            options%depth,options%cq_only/=0,options%contest,mycall,hiscall,session%knowledge)
       call timer('decft4  ',1)
    endif
    call set_decode_completion(session%completion,0,session%ft4%decoded,0)
    call write_decode_progress(progress)
    if(context%ios13==0) close(13)
  end subroutine

  subroutine run_jt9_attempt(options,phase,sample_count,callback_context,progress)
    type(jt9_options), intent(in) :: options
    integer, intent(in) :: phase,sample_count,progress
    type(decoder_callback_context), intent(in) :: callback_context
    type(decoder_callback_context) :: context

    context=callback_context
    context%nutc=options%utc
    context%nfqso=options%receive_frequency_hz
    context%submode=options%submode
    context%ios13=-1
    session%jt9%decoded=0
    if(sqrt(sum(real(session%audio(:sample_count))**2)/real(sample_count))<0.5) go to 100
    if(context%render_legacy) call open_legacy_output(phase,context%ios13)
    session%jt9%context=context
    call timer('decjt9  ',0)
    call session%jt9%decode_pcm(jt9_decoded,session%audio,sample_count,options%receive_frequency_hz, &
         options%search_low_hz,options%search_high_hz,options%tolerance_hz,options%depth, &
         options%submode,phase==3)
    call timer('decjt9  ',1)
    if(context%ios13==0) close(13)
100 call set_decode_completion(session%completion,0,session%jt9%decoded,0)
    call write_decode_progress(progress)
  end subroutine

  pure logical function valid_hhmmss(utc) result(valid)
    integer, intent(in) :: utc
    valid=utc>=0.and.utc<=235959.and.mod(utc/100,100)<=59.and.mod(utc,100)<=59
  end function

  logical function valid_jt4_options(options) result(valid)
    type(engine_jt4_options), intent(in) :: options
    valid=.false.
    if(.not.valid_hhmmss(options%utc)) return
    if(options%submode<0.or.options%submode>6) return
    if(options%minimum_width_profile<0.or.options%minimum_width_profile>options%submode) return
    if(options%depth<1.or.options%depth>3) return
    if(options%receive_frequency_hz<0.or.options%receive_frequency_hz>5000) return
    if(options%tolerance_hz<0.or.options%tolerance_hz>5000) return
    if(any([options%averaging,options%deep_search]<0).or.any([options%averaging,options%deep_search]>1)) return
    valid=.true.
  end function

  subroutine run_jt4_attempt(options,phase,sample_count,sample_rate,callback_context,progress,status)
    type(engine_jt4_options), intent(in) :: options
    integer, intent(in) :: phase,sample_count,sample_rate,progress
    type(decoder_callback_context), intent(in) :: callback_context
    integer(c_int), intent(out) :: status
    type(decoder_callback_context) :: context
    type(jt4_options) :: kernel_options
    integer :: kernel_status

    context=callback_context
    context%nutc=options%utc
    context%utc_is_hhmmss=.true.
    context%nfqso=options%receive_frequency_hz
    context%submode=options%submode
    context%ios13=-1
    context%ios14=-1
    if(context%render_legacy) then
       call open_legacy_output(phase,context%ios13)
       open(14,file=trim(temp_dir)//'/avemsg.txt',status='unknown',iostat=context%ios14)
       call prepare_jt4_host(session%jt4)
    endif
    session%jt4%context=context
    session%jt4%decoded=0
    kernel_options%utc=options%utc
    kernel_options%submode=options%submode
    kernel_options%receive_frequency_hz=options%receive_frequency_hz
    kernel_options%tolerance_hz=options%tolerance_hz
    kernel_options%depth=options%depth
    kernel_options%min_sync=options%min_sync
    kernel_options%minimum_width_profile=options%minimum_width_profile
    kernel_options%averaging=options%averaging/=0
    kernel_options%deep_search=options%deep_search/=0
    kernel_options%mycall=transfer(options%mycall,kernel_options%mycall)
    kernel_options%hiscall=transfer(options%hiscall,kernel_options%hiscall)
    kernel_options%hisgrid=transfer(options%hisgrid,kernel_options%hisgrid)
    call timer('decjt4  ',0)
    if(context%render_legacy) then
       call session%jt4%decode_pcm(kernel_options,session%audio(:sample_count),sample_rate,jt4_decoded, &
            kernel_status,write_jt4_diagnostic)
    else
       call session%jt4%decode_pcm(kernel_options,session%audio(:sample_count),sample_rate,jt4_decoded,kernel_status)
    endif
    call timer('decjt4  ',1)
    if(context%ios14==0) then
       call write_jt4_averages(session%jt4,14)
       close(14)
    endif
    if(context%ios13==0) close(13)
    call set_decode_completion(session%completion,0,session%jt4%decoded,0)
    call write_decode_progress(progress)
    status=ok
    if(kernel_status/=0) status=internal_error
  end subroutine

  integer(c_int) function engine_set_jt4_calls(handle,entries,count) &
       bind(C,name='decoder_engine_set_jt4_calls') result(status)
    type(c_ptr), value :: handle,entries
    integer(c_int), value :: count
    type(engine_jt4_call), pointer :: records(:)
    type(jt4_call), allocatable :: calls(:)
    integer :: i,allocation_status,kernel_status
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    status=invalid
    if(count<0.or.count>6999) return
    if(count>0.and..not.c_associated(entries)) return
    allocate(calls(count),stat=allocation_status)
    status=internal_error
    if(allocation_status/=0) return
    if(count>0) then
       call c_f_pointer(entries,records,[count])
       do i=1,count
          calls(i)%call=transfer(records(i)%call,calls(i)%call)
          calls(i)%grid=transfer(records(i)%grid,calls(i)%grid)
       enddo
    endif
    call session%jt4%set_calls(calls,kernel_status)
    if(kernel_status/=0) return
    status=ok
  end function

  integer(c_int) function engine_get_jt4_averages(handle,entries,entry_capacity,count) &
       bind(C,name='decoder_engine_get_jt4_averages') result(status)
    type(c_ptr), value :: handle,entries
    integer(c_int), value :: entry_capacity
    integer(c_int), intent(out) :: count
    type(engine_jt4_average_entry), pointer :: records(:)
    type(jt4_average_entry) :: averages(64)
    integer :: i,n
    count=0
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    status=invalid
    if(entry_capacity<0) return
    if(entry_capacity>0.and..not.c_associated(entries)) return
    call session%jt4%get_averages(averages,n)
    count=n
    if(entry_capacity>0) then
       call c_f_pointer(entries,records,[entry_capacity])
       do i=1,min(entry_capacity,n)
          records(i)%utc=averages(i)%utc
          records(i)%frequency_hz=averages(i)%frequency_hz
          records(i)%polarity=averages(i)%polarity
          records(i)%used=merge(1,0,averages(i)%used)
          records(i)%sync=averages(i)%sync
          records(i)%dt_seconds=averages(i)%dt_seconds
       enddo
    endif
    status=ok
    if(entry_capacity>0.and.entry_capacity<n) status=capacity
  end function

  integer(c_int) function engine_clear_jt4_averages(handle) &
       bind(C,name='decoder_engine_clear_jt4_averages') result(status)
    type(c_ptr), value :: handle
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    call session%jt4%clear_averages()
    status=ok
  end function

  logical function valid_jt65_options(options) result(valid)
    type(engine_jt65_options), intent(in) :: options
    valid=.false.
    if(options%search_low_hz<0.or.options%search_high_hz>5000.or. &
         options%search_low_hz>options%search_high_hz) return
    if(options%receive_frequency_hz<0.or.options%receive_frequency_hz>5000) return
    if(options%tolerance_hz<0.or.options%tolerance_hz>5000) return
    if(options%depth<1.or.options%depth>3.or.options%submode<0.or.options%submode>2) return
    if(options%passes<1.or.options%passes>2) return
    if(options%trials<0.or.options%trials>1000000) return
    if(options%aggressiveness<0.or.options%aggressiveness>11) return
    if(options%qso_progress<0.or.options%qso_progress>5) return
    if(any([options%single_decode,options%vhf,options%averaging,options%auto_clear, &
         options%deep_search,options%ap_enabled]<0)) return
    if(any([options%single_decode,options%vhf,options%averaging,options%auto_clear, &
         options%deep_search,options%ap_enabled]>1)) return
    valid=.true.
  end function

  subroutine run_jt65_attempt(options,phase,sample_count,callback_context,progress)
    type(engine_jt65_options), intent(in) :: options
    integer, intent(in) :: phase,sample_count,progress
    type(decoder_callback_context), intent(in) :: callback_context
    type(decoder_callback_context) :: context
    type(jt65_options) :: kernel_options
    logical, external :: baddata

    context=callback_context
    context%nutc=options%utc
    context%nfqso=options%receive_frequency_hz
    context%submode=options%submode
    context%bVHF=options%vhf/=0
    context%ios13=-1
    context%ios14=-1
    if(context%render_legacy) then
       call open_legacy_output(phase,context%ios13)
       open(14,file=trim(temp_dir)//'/avemsg.txt',status='unknown',iostat=context%ios14)
    endif
    session%jt65%context=context
    session%jt65%decoded=0
    kernel_options%utc=options%utc
    kernel_options%low_frequency=options%search_low_hz
    kernel_options%high_frequency=options%search_high_hz
    kernel_options%receive_frequency=options%receive_frequency_hz
    kernel_options%tolerance=options%tolerance_hz
    kernel_options%submode=options%submode
    kernel_options%min_sync=options%min_sync
    kernel_options%passes=options%passes
    kernel_options%trials=options%trials
    kernel_options%aggressiveness=options%aggressiveness
    kernel_options%effort=options%depth
    kernel_options%qso_progress=options%qso_progress
    kernel_options%repeat=phase==3
    kernel_options%single_decode=options%single_decode/=0
    kernel_options%vhf=options%vhf/=0
    kernel_options%average=options%averaging/=0
    kernel_options%auto_clear=options%auto_clear/=0
    kernel_options%deep_search=options%deep_search/=0
    kernel_options%ap=options%ap_enabled/=0
    kernel_options%mycall=transfer(options%mycall,kernel_options%mycall)
    kernel_options%hiscall=transfer(options%hiscall,kernel_options%hiscall)
    kernel_options%hisgrid=transfer(options%hisgrid,kernel_options%hisgrid)
    call timer('decjt65 ',0)
    if(.not.baddata(session%audio,52*12000)) then
      call session%jt65%decode_pcm(jt65_decoded,session%audio,sample_count,kernel_options,session%input_id)
    endif
    call timer('decjt65 ',1)
    if(context%ios14==0) then
       call write_jt65_averages(session%jt65,14)
       close(14)
    endif
    if(context%ios13==0) close(13)
    call set_decode_completion(session%completion,0,session%jt65%decoded,0)
    call write_decode_progress(progress)
  end subroutine

  integer(c_int) function engine_set_jt65_calls(handle,entries,count) &
       bind(C,name='decoder_engine_set_jt65_calls') result(status)
    type(c_ptr), value :: handle,entries
    integer(c_int), value :: count
    type(engine_jt65_call), pointer :: records(:)
    character(len=12), allocatable :: calls(:)
    character(len=4), allocatable :: grids(:)
    integer :: i
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    status=invalid
    if(count<0.or.count>10000) return
    if(count>0.and..not.c_associated(entries)) return
    allocate(calls(count),grids(count))
    if(count>0) then
       call c_f_pointer(entries,records,[count])
       do i=1,count
          calls(i)=transfer(records(i)%call,calls(i))
          grids(i)=transfer(records(i)%grid,grids(i))
       enddo
    endif
    call session%jt65%set_calls(calls,grids)
    status=ok
  end function

  integer(c_int) function engine_get_jt65_averages(handle,entries,entry_capacity,count) &
       bind(C,name='decoder_engine_get_jt65_averages') result(status)
    type(c_ptr), value :: handle,entries
    integer(c_int), value :: entry_capacity
    integer(c_int), intent(out) :: count
    type(engine_jt65_average_entry), pointer :: records(:)
    type(jt65_average_entry) :: averages(64)
    integer :: i,n
    count=0
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    status=invalid
    if(entry_capacity<0) return
    if(entry_capacity>0.and..not.c_associated(entries)) return
    call session%jt65%get_averages(averages,n)
    count=n
    if(entry_capacity>0) then
       call c_f_pointer(entries,records,[entry_capacity])
       do i=1,min(entry_capacity,n)
          records(i)%utc=averages(i)%utc
          records(i)%frequency_hz=averages(i)%frequency
          records(i)%polarity=averages(i)%polarity
          records(i)%used=merge(1,0,averages(i)%used)
          records(i)%sync=averages(i)%sync
          records(i)%dt_seconds=averages(i)%dt
       enddo
    endif
    status=ok
    if(entry_capacity>0.and.entry_capacity<n) status=capacity
  end function

  integer(c_int) function engine_clear_jt65_averages(handle) &
       bind(C,name='decoder_engine_clear_jt65_averages') result(status)
    type(c_ptr), value :: handle
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    call session%jt65%clear_averages()
    status=ok
  end function

  logical function valid_fst4_options(options) result(valid)
    type(engine_fst4_options), intent(in) :: options
    valid=.false.
    if(analysis_extent(engine_mode_fst4,options%period_seconds)==0) return
    if(options%depth<1.or.options%depth>3) return
    if(options%qso_progress<0.or.options%qso_progress>5) return
    if(options%search_low_hz<0.or.options%search_high_hz>5000.or. &
         options%search_low_hz>=options%search_high_hz) return
    if(options%receive_frequency_hz<0.or.options%receive_frequency_hz>5000) return
    if(options%tolerance_hz<0.or.options%tolerance_hz>5000) return
    if(.not.any(options%blanker_mode==[0,1,2,5])) return
    if(options%blanker_percent<0.or.options%blanker_percent>25) return
    if(.not.ieee_is_finite(options%eme_delay_seconds)) return
    if(options%eme_delay_seconds<0.or.options%eme_delay_seconds>real(options%period_seconds)) return
    if(any([options%single_decode,options%ap_cq_only,options%measure_doppler]<0)) return
    if(any([options%single_decode,options%ap_cq_only,options%measure_doppler]>1)) return
    if(.not.valid_hhmmss(options%utc)) return
    valid=.true.
  end function

  subroutine run_fst4_attempt(options,mode,phase,callback_context,progress,status)
    type(engine_fst4_options), intent(in) :: options
    integer, intent(in) :: mode,phase,progress
    type(decoder_callback_context), intent(in) :: callback_context
    integer(c_int), intent(out) :: status
    type(decoder_callback_context) :: context
    type(fst4_options) :: kernel_options
    integer :: kernel_status

    kernel_options=fst4_options()
    context=callback_context
    context%nutc=options%utc
    context%utc_is_hhmmss=.true.
    context%mode=mode
    context%nfqso=options%receive_frequency_hz
    context%ios13=-1
    if(context%render_legacy) call open_legacy_output(phase,context%ios13)
    session%fst4%context=context
    session%fst4%decoded=0
    session%fst4%knowledge=>session%knowledge
    kernel_options%utc=options%utc
    kernel_options%period=options%period_seconds
    kernel_options%receive_frequency=options%receive_frequency_hz
    kernel_options%low_frequency=options%search_low_hz
    kernel_options%high_frequency=options%search_high_hz
    kernel_options%tolerance=options%tolerance_hz
    kernel_options%effort=options%depth
    kernel_options%qso_progress=options%qso_progress
    kernel_options%blanker_percent=options%blanker_percent
    kernel_options%blanker_step=options%blanker_mode
    kernel_options%eme_delay=options%eme_delay_seconds
    kernel_options%wspr=mode==engine_mode_fst4w
    kernel_options%single_decode=options%single_decode/=0
    kernel_options%ap_cq_only=options%ap_cq_only/=0
    kernel_options%measure_doppler=options%measure_doppler/=0
    kernel_options%mycall=transfer(options%mycall,kernel_options%mycall)
    kernel_options%hiscall=transfer(options%hiscall,kernel_options%hiscall)
    if(context%render_legacy) call prepare_fst4_host(session%fst4,kernel_options)
    call timer('dec_fst4',0)
    call session%fst4%decode_pcm(fst4_decoded,session%audio,options%period_seconds*12000,kernel_options, &
         kernel_status)
    call timer('dec_fst4',1)
    if(context%render_legacy) call finish_fst4_host(session%fst4)
    if(context%ios13==0) close(13)
    call set_decode_completion(session%completion,0,session%fst4%decoded,0)
    call write_decode_progress(progress)
    status=ok
    if(kernel_status/=0) status=internal_error
  end subroutine

  integer(c_int) function engine_set_fst4w_calls(handle,entries,count) &
       bind(C,name='decoder_engine_set_fst4w_calls') result(status)
    type(c_ptr), value :: handle,entries
    integer(c_int), value :: count
    type(engine_fst4w_call), pointer :: records(:)
    character(len=20), allocatable :: calls(:)
    integer :: i,kernel_status
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    status=invalid
    if(count<0.or.count>100) return
    if(count>0.and..not.c_associated(entries)) return
    allocate(calls(count))
    if(count>0) then
       call c_f_pointer(entries,records,[count])
       do i=1,count
          calls(i)=transfer(records(i)%call_grid,calls(i))
       enddo
    endif
    call session%fst4%set_known_calls(calls,kernel_status)
    if(kernel_status/=0) return
    status=ok
  end function

  integer(c_int) function engine_get_fst4w_calls(handle,entries,entry_capacity,count) &
       bind(C,name='decoder_engine_get_fst4w_calls') result(status)
    type(c_ptr), value :: handle,entries
    integer(c_int), value :: entry_capacity
    integer(c_int), intent(out) :: count
    type(engine_fst4w_call), pointer :: records(:)
    character(len=20) :: calls(100)
    integer :: i,n
    count=0
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    status=invalid
    if(entry_capacity<0) return
    if(entry_capacity>0.and..not.c_associated(entries)) return
    call session%fst4%get_known_calls(calls,n)
    count=n
    if(entry_capacity>0) then
       call c_f_pointer(entries,records,[entry_capacity])
       do i=1,min(entry_capacity,n)
          records(i)%call_grid=transfer(calls(i),records(i)%call_grid)
       enddo
    endif
    status=ok
    if(entry_capacity>0.and.entry_capacity<n) status=capacity
  end function


  logical function valid_q65_options(options) result(valid)
    type(engine_q65_options), intent(in) :: options
    valid=.false.
    if(.not.any(options%period_seconds==[15,30,60,120,300])) return
    if(options%submode<0.or.options%submode>5) return
    if(options%depth<1.or.options%depth>3) return
    if(options%max_drift_symbol_rates<0.or.options%max_drift_symbol_rates>50) return
    if(options%search_low_hz<0.or.options%search_high_hz>5000.or. &
         options%search_low_hz>=options%search_high_hz) return
    if(options%receive_frequency_hz<0.or.options%receive_frequency_hz>5000) return
    if(options%tolerance_hz<0.or.options%tolerance_hz>5000) return
    if(options%qso_progress<0.or.options%qso_progress>5) return
    if(options%contest<0.or.options%contest>7) return
    if(any([options%averaging,options%auto_clear,options%single_decode,options%extended_eme_search, &
         options%pileup,options%ap_cq_only]<0)) return
    if(any([options%averaging,options%auto_clear,options%single_decode,options%extended_eme_search, &
         options%pileup,options%ap_cq_only]>1)) return
    if(.not.valid_hhmmss(options%utc)) return
    valid=.true.
  end function

  subroutine run_q65_attempt(options,phase,sample_count,callback_context,progress,status)
    type(engine_q65_options), intent(in) :: options
    integer, intent(in) :: phase,sample_count,progress
    type(decoder_callback_context), intent(in) :: callback_context
    integer(c_int), intent(out) :: status
    type(decoder_callback_context) :: context
    type(q65_options) :: kernel_options
    integer :: kernel_status,even_count,odd_count,count
    real :: df,dt,empty_current(0),empty_average(0)

    context=callback_context
    context%nutc=options%utc
    context%submode=options%submode
    context%utc_is_hhmmss=.true.
    context%ios13=-1
    if(context%render_legacy) call open_legacy_output(phase,context%ios13)
    session%q65%context=context
    session%q65%decoded=0
    nullify(session%q65%diagnostic)
    if(context%render_legacy) session%q65%diagnostic=>write_q65_diagnostic
    kernel_options%utc=options%utc
    kernel_options%period=options%period_seconds
    kernel_options%submode=options%submode
    kernel_options%receive_frequency=options%receive_frequency_hz
    kernel_options%tolerance=options%tolerance_hz
    kernel_options%low_frequency=options%search_low_hz
    kernel_options%high_frequency=options%search_high_hz
    kernel_options%effort=options%depth
    kernel_options%max_drift=options%max_drift_symbol_rates
    kernel_options%qso_progress=options%qso_progress
    kernel_options%contest=options%contest
    kernel_options%now=options%now_seconds
    kernel_options%repeat=phase==3
    kernel_options%single_decode=options%single_decode/=0
    kernel_options%average=options%averaging/=0
    kernel_options%auto_clear=options%auto_clear/=0
    kernel_options%eme=options%extended_eme_search/=0
    kernel_options%pileup=options%pileup/=0
    kernel_options%ap_cq_only=options%ap_cq_only/=0
    kernel_options%mycall=transfer(options%mycall,kernel_options%mycall)
    kernel_options%hiscall=transfer(options%hiscall,kernel_options%hiscall)
    kernel_options%hisgrid=transfer(options%hisgrid,kernel_options%hisgrid)
    call timer('dec_q65 ',0)
    call session%q65%decode_pcm(q65_decoded,session%audio,sample_count,kernel_options, &
         session%input_id,kernel_status)
    call timer('dec_q65 ',1)
    if(context%render_legacy) call write_q65_curves(session%q65,kernel_options)
    if(context%ios13==0) close(13)
    nullify(session%q65%diagnostic)
    call session%q65%get_curves(even_count,odd_count,df,dt,empty_current,empty_average,count)
    call set_decode_completion(session%completion,0,session%q65%decoded,1000*even_count+odd_count)
    call write_decode_progress(progress)
    status=ok
    if(kernel_status/=0) status=internal_error
  end subroutine

  integer(c_int) function engine_set_q65_callers(handle,entries,count) &
       bind(C,name='decoder_engine_set_q65_callers') result(status)
    type(c_ptr), value :: handle,entries
    integer(c_int), value :: count
    type(engine_q65_caller), pointer :: records(:)
    type(q3list) :: callers(Q65_MAX_CALLERS)
    character(len=12) :: call
    integer :: i
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    status=invalid
    if(count<0.or.count>Q65_MAX_CALLERS) return
    if(count>0.and..not.c_associated(entries)) return
    callers=q3list('','',0,0,0)
    if(count>0) then
       call c_f_pointer(entries,records,[count])
       do i=1,count
          call=transfer(records(i)%call,call)
          if(len_trim(call)>6) return
          callers(i)%call=call
          callers(i)%grid=transfer(records(i)%grid,callers(i)%grid)
          callers(i)%nsec=records(i)%last_seen
          callers(i)%nfreq=records(i)%frequency_hz
          if(callers(i)%nfreq<0.or.callers(i)%nfreq>5000) return
       enddo
    endif
    call session%q65%set_callers(callers,count)
    status=ok
  end function

  integer(c_int) function engine_get_q65_callers(handle,entries,entry_capacity,count) &
       bind(C,name='decoder_engine_get_q65_callers') result(status)
    type(c_ptr), value :: handle,entries
    integer(c_int), value :: entry_capacity
    integer(c_int), intent(out) :: count
    type(engine_q65_caller), pointer :: records(:)
    type(q3list) :: callers(Q65_MAX_CALLERS)
    character(len=12) :: call
    integer :: i,n
    count=0
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    status=invalid
    if(entry_capacity<0) return
    if(entry_capacity>0.and..not.c_associated(entries)) return
    call session%q65%get_callers(callers,n)
    count=n
    if(entry_capacity>0) then
       call c_f_pointer(entries,records,[entry_capacity])
       do i=1,min(entry_capacity,n)
          call=callers(i)%call
          records(i)%call=transfer(call,records(i)%call)
          records(i)%grid=transfer(callers(i)%grid,records(i)%grid)
          records(i)%last_seen=callers(i)%nsec
          records(i)%frequency_hz=callers(i)%nfreq
       enddo
    endif
    status=ok
    if(entry_capacity>0.and.entry_capacity<n) status=capacity
  end function

  integer(c_int) function engine_get_q65_snapshot(handle,snapshot,instant,averaged,curve_capacity) &
       bind(C,name='decoder_engine_get_q65_snapshot') result(status)
    type(c_ptr), value :: handle,instant,averaged
    type(engine_q65_snapshot), intent(out) :: snapshot
    integer(c_int), value :: curve_capacity
    real(c_float), pointer :: current(:),average(:)
    real :: empty_current(0),empty_average(0)
    snapshot=engine_q65_snapshot()
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    status=invalid
    if(curve_capacity<0) return
    if(curve_capacity>0) then
       if(.not.c_associated(instant).or..not.c_associated(averaged)) return
       call c_f_pointer(instant,current,[curve_capacity])
       call c_f_pointer(averaged,average,[curve_capacity])
       call session%q65%get_curves(snapshot%even_count,snapshot%odd_count,snapshot%frequency_step_hz, &
            snapshot%dt_seconds,current,average,snapshot%curve_count,snapshot%curve_average_count)
    else
       call session%q65%get_curves(snapshot%even_count,snapshot%odd_count,snapshot%frequency_step_hz, &
            snapshot%dt_seconds,empty_current,empty_average,snapshot%curve_count,snapshot%curve_average_count)
    endif
    status=ok
    if(curve_capacity>0.and.curve_capacity<snapshot%curve_count) status=capacity
  end function

  integer(c_int) function engine_clear_q65_averages(handle) &
       bind(C,name='decoder_engine_clear_q65_averages') result(status)
    type(c_ptr), value :: handle
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    call session%q65%clear_averages()
    status=ok
  end function

  subroutine collect_observation(user,observation)
    type(c_ptr), intent(in) :: user
    type(engine_observation), intent(in) :: observation
    type(engine_observation) :: record
    procedure(observation_callback), pointer :: callback
    if(.not.valid_handle(user)) return
    record=observation
    record%input_id=session%request%input_id
    record%analysis_id=session%request%analysis_id
    record%attempt_no=session%request%attempt_no
    session%emitted=session%emitted+1
    if(session%retained<evidence_capacity) then
       session%retained=session%retained+1
       session%evidence(session%retained)=record
    else
       session%dropped=session%dropped+1
    endif
    if(c_associated(session%callback)) then
       call c_f_procpointer(session%callback,callback)
       call callback(record,session%user)
    endif
  end subroutine

  subroutine collect_superfox(user,observation)
    type(c_ptr), intent(in) :: user
    type(superfox_observation), intent(in) :: observation
    type(engine_observation) :: record
    integer :: i
    record%variant=2
    record%superfox%kind=observation%kind
    record%superfox%child_index=observation%child_index
    record%snr_db=observation%snr_db
    record%frequency_hz=observation%frequency_hz
    record%dt_seconds=observation%dt_seconds
    record%superfox%symbols=observation%symbols
    do i=1,min(37,len_trim(observation%message))
       record%message(i)=observation%message(i:i)
    enddo
    call collect_observation(user,record)
    if(session%render_legacy.and.observation%legacy_line_length>0) then
       write(*,'(a)') observation%legacy_line(:observation%legacy_line_length)
       flush(6)
    endif
  end subroutine

  integer(c_int) function engine_release_input(handle,input_id) &
       bind(C,name='decoder_engine_release_input') result(status)
    type(c_ptr), value :: handle
    integer(c_int64_t), value :: input_id
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    status=invalid
    if(input_id<=0.or.session%input_id/=input_id) return
    call release_ft8_input(session%kernel)
    call session%jt9%release_input()
    call session%jt4%release_input()
    call session%jt65%release_input()
    call session%q65%release_input()
    session%input_id=0
    session%input_mode=0
    session%input_sample_count=0
    session%input_sample_rate=0
    session%analysis_id=0
    session%last_attempt=0
    session%retained=0
    session%dropped=0
    status=ok
  end function

  integer(c_int) function engine_reset_session(handle) bind(C,name='decoder_engine_reset_session') result(status)
    type(c_ptr), value :: handle
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    call reset_ft8_kernel(session%kernel)
    call session%ft4%reset()
    call session%jt9%reset()
    call session%jt4%reset()
    call session%jt65%reset()
    call session%q65%reset()
    call session%fst4%reset()
    call reset_pack77_state(session%knowledge)
    session%input_id=0
    session%input_mode=0
    session%input_sample_count=0
    session%input_sample_rate=0
    session%analysis_id=0
    session%last_attempt=0
    session%retained=0
    session%dropped=0
    status=ok
  end function

  integer(c_int) function engine_destroy(handle) bind(C,name='decoder_engine_destroy') result(status)
    type(c_ptr), value :: handle
    status=invalid
    if(.not.valid_handle(handle)) return
    status=busy
    if(session%running) return
    call reset_ft8_kernel(session%kernel)
    call session%fst4%destroy()
    deallocate(session)
    host_handle=c_null_ptr
    status=ok
  end function

  subroutine request_to_params(request,valid_samples,params)
    type(attempt_request), intent(in) :: request
    integer, intent(in) :: valid_samples
    type(params_block), intent(out) :: params
    integer(c_int8_t) :: zeros(c_sizeof(params))
    zeros=0
    params=transfer(zeros,params)
    params%nmode=8
    params%ntr=15
    params%kin=valid_samples
    params%ndiskdat=request%source==1
    params%nagain=request%phase==3
    params%newdat=request%ft8%reuse_spectrum==0
    params%nzhsym=request%ft8%half_symbol_stage
    params%nutc=request%ft8%utc
    params%nQSOProgress=request%ft8%qso_progress
    params%nfqso=request%ft8%receive_frequency_hz
    params%nftx=request%ft8%transmit_frequency_hz
    params%nfa=request%ft8%search_low_hz
    params%nfb=request%ft8%search_high_hz
    params%ntol=request%ft8%tolerance_hz
    params%ndepth=request%ft8%depth
    params%napwid=request%ft8%ap_width_hz
    params%nexp_decode=request%ft8%contest
    params%ncandthin=request%ft8%candidate_thinning
    params%ndtcenter=request%ft8%time_center
    params%nft8cycles=request%ft8%cycles
    params%nranera=request%ft8%trials
    params%nlasttx=request%ft8%last_transmit
    params%ndelay=request%ft8%delay
    params%nmt=request%ft8%threads
    params%nft8rxfsens=request%ft8%receive_sensitivity
    params%nsecbandchanged=request%ft8%discard_leading_seconds
    params%yymmdd=request%ft8%date
    params%lft8apon=request%ft8%ap_enabled/=0
    params%lapcqonly=request%ft8%cq_only/=0
    params%b_even_seq=request%ft8%even_sequence/=0
    params%b_superfox=request%ft8%superfox/=0
    params%nagainfil=request%ft8%filtered_retry/=0
    params%nstophint=request%ft8%stop_hint/=0
    params%lmultift8=request%ft8%mtd/=0
    params%lft8lowth=request%ft8%low_threshold/=0
    params%lft8subpass=request%ft8%subtract_pass/=0
    params%ltxing=request%ft8%transmitting/=0
    params%lhideft8dupes=request%ft8%hide_duplicates/=0
    params%lhound=request%ft8%hound/=0
    params%lmycallstd=request%ft8%standard_mycall/=0
    params%lhiscallstd=request%ft8%standard_hiscall/=0
    params%lapmyc=request%ft8%ap_mycall/=0
    params%lmodechanged=request%ft8%mode_changed/=0
    params%lbandchanged=request%ft8%band_changed/=0
    params%lenabledxcsearch=request%ft8%dx_search/=0
    params%lwidedxcsearch=request%ft8%wide_dx_search/=0
    params%lmultinst=request%ft8%multiple_instances/=0
    params%lskiptx1=request%ft8%skip_first_message/=0
    params%emedelay=request%ft8%eme_delay_seconds
    params%mycall=request%ft8%mycall
    params%mybcall=request%ft8%my_base_call
    params%hiscall=request%ft8%hiscall
    params%hisbcall=request%ft8%his_base_call
    params%mygrid=request%ft8%mygrid
    params%hisgrid=request%ft8%hisgrid
  end subroutine

  subroutine engine_host_decode(id2,params,nfsample,completion,progress,input_id,analysis_id,attempt_no,valid_samples)
    integer(c_short), target, intent(in) :: id2(*)
    type(params_block), intent(in) :: params
    integer, intent(in) :: nfsample,progress,attempt_no,valid_samples
    integer(c_int64_t), intent(in) :: input_id,analysis_id
    type(decode_completion_result), intent(out) :: completion
    type(attempt_request) :: request
    type(audio_view) :: audio
    type(attempt_outcome) :: outcome
    type(engine_options) :: options
    integer(c_int) :: status
    integer :: time,history_status,blanker,period_samples,gate_samples
    character(len=12), allocatable :: calls(:)
    character(len=4), allocatable :: grids(:)
    options%abi_version=engine_abi
    completion=decode_completion_result(.true.,0,0,0)
    if(params%nmode==engine_mode_jt4) then
       if(params%nclearave.and.valid_handle(host_handle)) call session%jt4%clear_averages()
       gate_samples=min(180000,max(0,valid_samples))
       if(gate_samples>0) then
          if(sqrt(sum(real(id2(1:gate_samples))**2)/180000.0)<0.5) return
       endif
    endif
    if(params%nmode==engine_mode_ft4) then
       ! The legacy host's low-signal gate includes its zero-padded 15-second window.
       if(sqrt(sum(real(id2(1:180000))**2)/180000.0)<0.5) return
    endif
    request%input_id=input_id
    request%mode=params%nmode
    if(request%mode==242) request%mode=engine_mode_fst4w
    request%analysis_id=analysis_id
    request%attempt_no=attempt_no
    if(.not.valid_handle(host_handle)) then
       status=engine_create(options,host_handle)
       if(status/=ok) then
          call report_host_rejection(status,request,'create')
          return
       endif
    endif
    if(input_id<=0.or.analysis_id<=0) then
       if(session%input_id==0.or.params%nutc/=fallback_utc.or.session%input_mode/=request%mode.or. &
            ((params%nmode==engine_mode_jt4.or.params%nmode==engine_mode_ft4.or.params%nmode==engine_mode_jt9.or. &
            params%nmode==engine_mode_jt65.or.params%nmode==engine_mode_q65.or. &
            request%mode==engine_mode_fst4.or.request%mode==engine_mode_fst4w).and..not.params%nagain).or. &
            (params%nzhsym==41.and..not.params%nagain)) then
          fallback_input=fallback_input+1
          fallback_analysis=fallback_analysis+1
       else if(params%nagain) then
          fallback_analysis=fallback_analysis+1
       endif
       request%input_id=max(1_c_int64_t,fallback_input)
       request%analysis_id=max(1_c_int64_t,fallback_analysis)
       request%attempt_no=session%last_attempt+1
       fallback_utc=params%nutc
    endif
    if(session%input_id/=0.and.(session%input_id/=request%input_id.or.session%input_mode/=request%mode)) then
       status=engine_release_input(host_handle,session%input_id)
       if(status/=ok) then
          call report_host_rejection(status,request,'release-input')
          return
       endif
    endif
    request%phase=2
    if(params%nmode==engine_mode_ft8.and.params%nzhsym<50) request%phase=1
    if(params%nagain) request%phase=3
    request%source=merge(1,0,params%ndiskdat)
    if(request%mode==engine_mode_jt4) then
       call convert_jt4_host_options(params,request%jt4,streaming_emit_enabled())
       if(request%jt4%deep_search/=0) call load_jt4_calls(session%jt4)
    else if(request%mode==engine_mode_ft4) then
       request%ft4%utc=params%nutc
       request%ft4%qso_progress=params%nQSOProgress
       request%ft4%receive_frequency_hz=params%nfqso
       request%ft4%search_low_hz=params%nfa
       request%ft4%search_high_hz=params%nfb
       request%ft4%depth=iand(params%ndepth,7)
       request%ft4%cq_only=merge(1,0,params%lapcqonly)
       request%ft4%contest=iand(params%nexp_decode,7)
       request%ft4%mycall=params%mycall
       request%ft4%hiscall=params%hiscall
    else if(request%mode==engine_mode_jt9) then
       request%jt9%utc=params%nutc
       request%jt9%receive_frequency_hz=params%nfqso
       request%jt9%search_low_hz=params%nfa
       request%jt9%search_high_hz=params%nfb
       request%jt9%tolerance_hz=params%ntol
       request%jt9%depth=iand(params%ndepth,7)
       request%jt9%submode=params%nsubmode
    else if(request%mode==engine_mode_jt65) then
       request%jt65%utc=params%nutc
       request%jt65%qso_progress=params%nQSOProgress
       request%jt65%receive_frequency_hz=params%nfqso
       request%jt65%search_low_hz=params%nfa
       request%jt65%search_high_hz=params%nfb
       request%jt65%tolerance_hz=params%ntol
       request%jt65%depth=iand(params%ndepth,7)
       request%jt65%submode=params%nsubmode
       request%jt65%min_sync=params%minsync
       request%jt65%passes=max(1,min(2,params%n2pass))
       request%jt65%trials=10**(params%nranera/2)
       if(mod(params%nranera,2)==1) request%jt65%trials=3*request%jt65%trials
       if(params%nranera==0) request%jt65%trials=0
       request%jt65%aggressiveness=params%naggressive
       request%jt65%single_decode=merge(1,0,iand(params%nexp_decode,32)/=0)
       request%jt65%vhf=merge(1,0,iand(params%nexp_decode,64)/=0)
       request%jt65%averaging=merge(1,0,iand(params%ndepth,16)/=0)
       request%jt65%auto_clear=merge(1,0,iand(params%ndepth,128)/=0)
       request%jt65%deep_search=merge(1,0,iand(params%ndepth,32)/=0)
       request%jt65%ap_enabled=merge(1,0,params%ljt65apon)
       request%jt65%mycall=params%mycall
       request%jt65%hiscall=params%hiscall
       request%jt65%hisgrid=params%hisgrid
       if(params%nclearave) call session%jt65%clear_averages()
       if(request%jt65%deep_search/=0) then
          call load_jt65_calls(calls,grids)
          call session%jt65%set_calls(calls,grids)
       endif
    else if(request%mode==engine_mode_fst4.or.request%mode==engine_mode_fst4w) then
       request%fst4%utc=params%nutc
       request%fst4%period_seconds=params%ntr
       request%fst4%receive_frequency_hz=params%nfqso
       request%fst4%search_low_hz=max(0,params%nfa)
       request%fst4%search_high_hz=min(params%nfb,5000)
       request%fst4%tolerance_hz=params%ntol
       request%fst4%depth=iand(params%ndepth,3)
       request%fst4%qso_progress=params%nQSOProgress
       request%fst4%single_decode=merge(1,0,iand(params%nexp_decode,32)/=0)
       request%fst4%ap_cq_only=merge(1,0,params%lapcqonly)
       blanker=params%nexp_decode/256-3
       if(blanker>=0) then
          request%fst4%blanker_percent=blanker
       else
          select case(blanker)
          case(-1); request%fst4%blanker_mode=5
          case(-2); request%fst4%blanker_mode=2
          case(-3); request%fst4%blanker_mode=1
          end select
       endif
       session%fst4%print_hash22=params%nmode==242
       request%fst4%eme_delay_seconds=params%emedelay
       request%fst4%mycall=params%mycall
       request%fst4%hiscall=params%hiscall
    else if(request%mode==engine_mode_q65) then
       request%q65%utc=params%nutc
       request%q65%period_seconds=params%ntr
       request%q65%submode=params%nsubmode
       request%q65%receive_frequency_hz=params%nfqso
       request%q65%tolerance_hz=params%ntol
       request%q65%search_low_hz=params%nfa
       request%q65%search_high_hz=min(params%nfb,5000)
       request%q65%depth=iand(params%ndepth,3)
       request%q65%max_drift_symbol_rates=params%max_drift
       request%q65%qso_progress=params%nQSOProgress
       request%q65%contest=iand(params%nexp_decode,7)
       request%q65%averaging=merge(1,0,iand(params%ndepth,16)/=0)
       request%q65%auto_clear=merge(1,0,iand(params%ndepth,128)/=0)
       request%q65%single_decode=merge(1,0,iand(params%nexp_decode,32)/=0)
       request%q65%extended_eme_search=merge(1,0,params%emedelay>0)
       request%q65%pileup=merge(1,0,iand(params%nexp_decode,128)/=0)
       request%q65%ap_cq_only=merge(1,0,params%lapcqonly)
       request%q65%now_seconds=time()
       request%q65%mycall=params%mycall
       request%q65%hiscall=params%hiscall
       request%q65%hisgrid=params%hisgrid
       if(params%nclearave) call session%q65%clear_averages()
       history_status=0
       if(request%q65%contest==1) call load_q65_history(session%q65,history_status)
    else
       request%ft8%reuse_spectrum=merge(0,1,params%newdat)
       request%ft8%half_symbol_stage=params%nzhsym
       request%ft8%utc=params%nutc
       request%ft8%qso_progress=params%nQSOProgress
       request%ft8%receive_frequency_hz=params%nfqso
       request%ft8%transmit_frequency_hz=params%nftx
       request%ft8%search_low_hz=params%nfa
       request%ft8%search_high_hz=params%nfb
       request%ft8%tolerance_hz=params%ntol
       request%ft8%depth=params%ndepth
       request%ft8%ap_width_hz=params%napwid
       request%ft8%contest=params%nexp_decode
       request%ft8%candidate_thinning=params%ncandthin
       request%ft8%time_center=params%ndtcenter
       request%ft8%cycles=params%nft8cycles
       request%ft8%trials=params%nranera
       request%ft8%last_transmit=params%nlasttx
       request%ft8%delay=params%ndelay
       request%ft8%threads=params%nmt
       request%ft8%receive_sensitivity=params%nft8rxfsens
       request%ft8%discard_leading_seconds=params%nsecbandchanged
       request%ft8%date=params%yymmdd
       request%ft8%ap_enabled=merge(1,0,params%lft8apon)
       request%ft8%cq_only=merge(1,0,params%lapcqonly)
       request%ft8%even_sequence=merge(1,0,params%b_even_seq)
       request%ft8%superfox=merge(1,0,params%b_superfox)
       request%ft8%filtered_retry=merge(1,0,params%nagainfil)
       request%ft8%stop_hint=merge(1,0,params%nstophint)
       request%ft8%mtd=merge(1,0,params%lmultift8)
       request%ft8%low_threshold=merge(1,0,params%lft8lowth)
       request%ft8%subtract_pass=merge(1,0,params%lft8subpass)
       request%ft8%transmitting=merge(1,0,params%ltxing)
       request%ft8%hide_duplicates=merge(1,0,params%lhideft8dupes)
       request%ft8%hound=merge(1,0,params%lhound)
       request%ft8%standard_mycall=merge(1,0,params%lmycallstd)
       request%ft8%standard_hiscall=merge(1,0,params%lhiscallstd)
       request%ft8%ap_mycall=merge(1,0,params%lapmyc)
       request%ft8%mode_changed=merge(1,0,params%lmodechanged)
       request%ft8%band_changed=merge(1,0,params%lbandchanged)
       request%ft8%dx_search=merge(1,0,params%lenabledxcsearch)
       request%ft8%wide_dx_search=merge(1,0,params%lwidedxcsearch)
       request%ft8%multiple_instances=merge(1,0,params%lmultinst)
       request%ft8%skip_first_message=merge(1,0,params%lskiptx1)
       request%ft8%eme_delay_seconds=params%emedelay
       request%ft8%mycall=params%mycall
       request%ft8%my_base_call=params%mybcall
       request%ft8%hiscall=params%hiscall
       request%ft8%his_base_call=params%hisbcall
       request%ft8%mygrid=params%mygrid
       request%ft8%hisgrid=params%hisgrid
    endif
    audio%samples=c_loc(id2(1))
    period_samples=analysis_extent(request%mode,request%q65%period_seconds,nfsample)
    if(request%mode==engine_mode_fst4.or.request%mode==engine_mode_fst4w) &
         period_samples=analysis_extent(request%mode,request%fst4%period_seconds)
    audio%sample_count=min(period_samples,valid_samples)
    if(valid_samples<=0.and.input_id<=0) then
       audio%sample_count=period_samples
       if(request%mode==engine_mode_jt9.or.request%mode==engine_mode_jt65) audio%sample_count=624000
       if(request%mode==engine_mode_jt4) audio%sample_count=52*nfsample
    endif
    if(request%mode==engine_mode_ft8) then
       if(params%lmultift8.and..not.(iand(params%nexp_decode,7)==7.and. &
            params%b_superfox.and.params%b_even_seq)) &
            audio%sample_count=min(audio%sample_count,params%nzhsym*3456)
    endif
    audio%sample_rate_hz=nfsample
    status=decode_attempt(host_handle,request,audio,c_null_funptr,c_null_ptr,outcome,.true.,progress)
    if(status==ok) then
       if(request%mode==engine_mode_q65.and.request%q65%contest==1) &
            call save_q65_history(session%q65,history_status)
       completion=session%completion
    else
       call report_host_rejection(status,request,'decode')
    endif
  end subroutine

  subroutine report_host_rejection(status,request,operation)
    integer(c_int), intent(in) :: status
    type(attempt_request), intent(in) :: request
    character(len=*), intent(in) :: operation
    character(len=12) :: status_name
    select case(status)
    case(invalid)
       status_name='invalid'
    case(busy)
       status_name='busy'
    case(unsupported)
       status_name='unsupported'
    case default
       write(status_name,'(i0)') status
    end select
    write(*,'(a,a,a,a,4(a,i0))') '<DecodeRejected> status=',trim(status_name), &
         ' operation=',operation,' mode=',request%mode,' input=',request%input_id, &
         ' analysis=',request%analysis_id,' attempt=',request%attempt_no
    flush(6)
  end subroutine report_host_rejection
end module decoder_engine

subroutine run_decoder_engine(id2,params,nfsample,completion,progress_generation, &
     input_id,analysis_id,attempt_no,valid_samples)
  use, intrinsic :: iso_c_binding, only: c_short,c_int,c_int64_t
  use decoder_engine, only: engine_host_decode
  use ft8_engine_kernel, only: params_block
  use decode_completion_module, only: decode_completion_result
  implicit none
  integer(c_short), target, intent(in) :: id2(*)
  type(params_block), intent(in) :: params
  integer(c_int), intent(in) :: nfsample,progress_generation,attempt_no,valid_samples
  integer(c_int64_t), intent(in) :: input_id,analysis_id
  type(decode_completion_result), intent(out) :: completion
  call engine_host_decode(id2,params,nfsample,completion,progress_generation,input_id,analysis_id,attempt_no,valid_samples)
end subroutine
