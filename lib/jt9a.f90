subroutine jt9a()
  use, intrinsic :: iso_c_binding, only: c_f_pointer, c_null_char, c_bool, c_int, &
       c_ptr, c_null_ptr, c_size_t
  use decoder_ipc_layout_module, only: decoder_ipc_fortran_validate, &
       decoder_ipc_report_layout_error, DECODER_IPC_LAYOUT_VERSION, DECODER_IPC_LAYOUT_ATTACH
  use decoder_ipc_atomic, only: decoder_ipc_control_try_claim, &
       decoder_ipc_control_finish, decoder_ipc_progress_bind, &
       decoder_ipc_progress_unbind, DECODER_IPC_CLAIM_INVALID, &
       DECODER_IPC_CLAIM_INCOMPATIBLE, DECODER_IPC_CLAIM_NONE, &
       DECODER_IPC_CLAIMED, DECODER_IPC_CLAIM_SHUTDOWN
  use decode_completion_module, only: decode_completion_result,          &
       reset_decode_completion, set_decode_completion, write_decode_completion
  use prog_args
  use timer_module, only: timer
  use timer_impl, only: init_timer !, limtrace
  use shmem

  include 'jt9com.f90'

  integer*2 id2a(180000)
  save id2a                              !Keep this big array off the stack
! Multiple instances:
  type(shared_dec_data), pointer, volatile :: shared_memory
  type(params_block) :: local_params
  logical(c_bool) :: ok
  integer(c_int) :: active_generation, claim_result, pass_attempt_no, pass_valid_samples
  integer(c_int) :: layout_status
  integer(c_size_t) :: available_bytes
  type(c_ptr) :: shared_address
  type(decode_completion_result) :: completion

  layout_status=0
!  open(23,file=trim(data_dir)//'/CALL3.TXT',status='unknown')

!  limtrace=-1                            !Disable all calls to timer()

  ok=shmem_lock_worker(trim(ipc_lock_path)//c_null_char)
  if(.not.ok) then
     write(*,'(a)') '<DecoderError> status=worker-lock'
     call flush(6)
     stop 1
  endif
  ok=shmem_attach(trim(shm_key)//c_null_char)
  if(.not.ok) then
     layout_status=DECODER_IPC_LAYOUT_ATTACH
     call decoder_ipc_report_layout_error(layout_status,c_null_ptr,0_c_size_t)
     go to 999
  endif
  msdelay=10
  shared_address=shmem_address()
  available_bytes=shmem_size()
  layout_status=decoder_ipc_fortran_validate(shared_address,available_bytes)
  if(layout_status.ne.0) then
     call decoder_ipc_report_layout_error(layout_status,shared_address,available_bytes)
     go to 999
  endif
  call init_timer (trim(data_dir)//'/timer.out')
  call c_f_pointer(shared_address,shared_memory)

  call decoder_ipc_progress_bind(shared_memory%control%generation, &
       shared_memory%control%state, shared_memory%control%version, &
       shared_memory%control%progress)

  call reset_decode_completion(completion)
  write(*,'(a,i0)') '<DecoderReady> version=',DECODER_IPC_VERSION
  call flush(6)

10 layout_status=decoder_ipc_fortran_validate(shared_address,available_bytes)
  if(layout_status.ne.0) then
     call decoder_ipc_report_layout_error(layout_status,shared_address,available_bytes)
     go to 999
  endif
  claim_result=decoder_ipc_control_try_claim( &
       shared_memory%control%generation, shared_memory%control%state, &
       shared_memory%control%version, active_generation)
  if(claim_result.eq.DECODER_IPC_CLAIM_SHUTDOWN) then
     go to 999
  endif
  if(claim_result.eq.DECODER_IPC_CLAIM_INCOMPATIBLE) then
     layout_status=DECODER_IPC_LAYOUT_VERSION
     call decoder_ipc_report_layout_error(layout_status,shared_address,available_bytes)
     go to 999
  endif
  if(claim_result.eq.DECODER_IPC_CLAIM_INVALID) then
     layout_status=DECODER_IPC_CLAIM_INVALID
     write(*,'(a)') '<DecoderError> status=invalid-generation'
     call flush(6)
     go to 999
  endif
  if(claim_result.eq.DECODER_IPC_CLAIM_NONE) then
     call sleep_msec(msdelay)
     go to 10
  endif
  if(claim_result.ne.DECODER_IPC_CLAIMED) call abort
  write(*,'(a,i0)') '<DecodeStarted> gen=',active_generation
  call flush(6)
  local_params=shared_memory%payload%params
  pass_attempt_no=shared_memory%metadata%attempt_no
  call timer('decoder ',0)
  if(local_params%nmode.eq.8 .and. local_params%ndiskdat .and.    &
       .not. local_params%nagain .and.  .not. local_params%lmultift8) then
! Early decoding pass, FT8 only, when wsjtx reads from disk
     nearly=41
     local_params%nzhsym=nearly
     id2a(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
     id2a(nearly*3456+1:)=0
     call run_decoder_engine(id2a,local_params, &
          12000,completion,active_generation,shared_memory%metadata%input_id, &
          shared_memory%metadata%analysis_id,pass_attempt_no, &
          min(shared_memory%metadata%valid_samples,nearly*3456))
     pass_attempt_no=pass_attempt_no+1
     nearly=47
     local_params%nzhsym=nearly
     id2a(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
     id2a(nearly*3456+1:)=0
     call run_decoder_engine(id2a,local_params, &
          12000,completion,active_generation,shared_memory%metadata%input_id, &
          shared_memory%metadata%analysis_id,pass_attempt_no, &
          min(shared_memory%metadata%valid_samples,nearly*3456))
     pass_attempt_no=pass_attempt_no+1
     local_params%nzhsym=50
  endif
  
  !ft8md
  if(local_params%nmode.eq.8 .and. local_params%lmultift8 .and.   &
       .not. local_params%nagain .and. local_params%ndiskdat) then 
     if(local_params%ndecoderstart.lt.2) then
        nearly=41
        local_params%lmultift8=.false.
        local_params%nzhsym=nearly
        id2a(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
        id2a(nearly*3456+1:)=0
        call run_decoder_engine(id2a,local_params, &
             12000,completion,active_generation,shared_memory%metadata%input_id, &
             shared_memory%metadata%analysis_id,pass_attempt_no, &
             min(shared_memory%metadata%valid_samples,nearly*3456))
        pass_attempt_no=pass_attempt_no+1
        if(local_params%ndecoderstart.lt.2) then
           nearly=46
           local_params%lmultift8=.false.
           local_params%nzhsym=nearly
           id2a(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
           id2a(nearly*3456+1:)=0
           call run_decoder_engine(id2a,local_params, &
                12000,completion,active_generation,shared_memory%metadata%input_id, &
                shared_memory%metadata%analysis_id,pass_attempt_no, &
                min(shared_memory%metadata%valid_samples,nearly*3456))
           pass_attempt_no=pass_attempt_no+1
        endif
        if(local_params%ndecoderstart.eq.0) nearly=49
        if(local_params%ndecoderstart.eq.1) nearly=50
        local_params%lmultift8=.true.
        shared_memory%payload%params%nzhsym=nearly
        if(local_params%ndecoderstart.eq.0) shared_memory%payload%params%nzhsym=49
        if(local_params%ndecoderstart.eq.1) shared_memory%payload%params%nzhsym=50
     else
        nearly=50
        if(local_params%ndecoderstart.eq.2) nearly=48
        if(local_params%ndecoderstart.eq.3) nearly=49
        if(local_params%ndecoderstart.eq.4) nearly=50
        shared_memory%payload%params%nzhsym=nearly
        if(local_params%ndecoderstart.eq.2) shared_memory%payload%params%nzhsym=48
        if(local_params%ndecoderstart.eq.3) shared_memory%payload%params%nzhsym=49
        if(local_params%ndecoderstart.eq.4) shared_memory%payload%params%nzhsym=50
     endif
     local_params%nzhsym=nearly
  endif
  !end ft8md

  if(local_params%nmode .eq. 144) then
    ! MSK144
     call decode_msk144_core(shared_memory%payload%id2, local_params, data_dir, &
          completion)
  elseif(local_params%nmode.eq.4.or.local_params%nmode.eq.8.or.local_params%nmode.eq.5.or. &
       local_params%nmode.eq.9.or. &
       local_params%nmode.eq.65.or.local_params%nmode.eq.66.or. &
       local_params%nmode.eq.240.or.local_params%nmode.eq.241.or.local_params%nmode.eq.242) then
     pass_valid_samples=min(shared_memory%metadata%valid_samples,180000)
     if(local_params%nmode.eq.4.or.local_params%nmode.eq.9.or.local_params%nmode.eq.65) &
          pass_valid_samples=min(shared_memory%metadata%valid_samples,720000)
     if(local_params%nmode.eq.66.or.local_params%nmode.eq.240.or. &
          local_params%nmode.eq.241.or.local_params%nmode.eq.242) &
          pass_valid_samples=min(shared_memory%metadata%valid_samples,local_params%ntr*12000)
     call run_decoder_engine(shared_memory%payload%id2, &
          local_params,12000,completion,active_generation, &
          shared_memory%metadata%input_id,shared_memory%metadata%analysis_id, &
          pass_attempt_no,pass_valid_samples)
  else
    ! Normal decoding pass
     call multimode_decoder_core(shared_memory%payload%ss, &
          shared_memory%payload%id2,local_params,12000,completion,active_generation)
  endif

  call timer('decoder ',1)

  if(.not.completion%available) then
     call set_decode_completion(completion,0,0,0)
  endif
  claim_result=decoder_ipc_control_finish(shared_memory%control%generation, &
       shared_memory%control%state, shared_memory%control%version, &
       active_generation)
  if(claim_result.ne.0) then
     call write_decode_completion(completion,active_generation)
     call flush(6)
  endif
  go to 10
  
999 call decoder_ipc_progress_unbind()
  call timer('decoder ',101)
  if(layout_status.ne.0) stop 1

  return
end subroutine jt9a
