subroutine jt9a()
  use, intrinsic :: iso_c_binding, only: c_f_pointer, c_null_char, c_bool, c_sizeof, c_int
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
  use ft8_mod1, only : dd8
  use jt65_mod6, only : dd

  include 'jt9com.f90'

  integer*2 id2a(180000)
  save id2a                              !Keep this big array off the stack
! Multiple instances:
  type(shared_dec_data), pointer, volatile :: shared_memory
  type(params_block) :: local_params
  logical(c_bool) :: ok
  integer(c_int) :: active_generation, claim_result
  type(decode_completion_result) :: completion

  call init_timer (trim(data_dir)//'/timer.out')
!  open(23,file=trim(data_dir)//'/CALL3.TXT',status='unknown')

!  limtrace=-1                            !Disable all calls to timer()

! Multiple instances: set the shared memory key before attaching
  call shmem_setkey(trim(shm_key)//c_null_char)
  ok=shmem_attach()
  if(.not.ok) call abort
  msdelay=10
  call c_f_pointer(shmem_address(),shared_memory)
  nbytes=shmem_size()
  if(nbytes.lt.c_sizeof(shared_memory)) then
     ok=shmem_detach()
     print*,'jt9a: Incompatible shared-memory layout.'
     go to 999
  endif

  call decoder_ipc_progress_bind(shared_memory%control%generation, &
       shared_memory%control%state, shared_memory%control%version, &
       shared_memory%control%progress)

  call reset_decode_completion(completion)

10 claim_result=decoder_ipc_control_try_claim( &
       shared_memory%control%generation, shared_memory%control%state, &
       shared_memory%control%version, active_generation)
  if(claim_result.eq.DECODER_IPC_CLAIM_SHUTDOWN) then
     ok=shmem_detach()
     go to 999
  endif
  if(claim_result.eq.DECODER_IPC_CLAIM_INCOMPATIBLE) then
     ok=shmem_detach()
     print*,'jt9a: Incompatible shared-memory protocol version.'
     go to 999
  endif
  if(claim_result.eq.DECODER_IPC_CLAIM_INVALID) then
     ok=shmem_detach()
     print*,'jt9a: Invalid decoder request generation.'
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
  call timer('decoder ',0)
  if(local_params%nmode.eq.8 .and. local_params%ndiskdat .and.    &
       .not. local_params%nagain .and.  .not. local_params%lmultift8) then
! Early decoding pass, FT8 only, when wsjtx reads from disk
     nearly=41
     local_params%nzhsym=nearly
     id2a(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
     id2a(nearly*3456+1:)=0
     call multimode_decoder_core(shared_memory%payload%ss,id2a,local_params, &
          12000,completion,active_generation)
     nearly=47
     local_params%nzhsym=nearly
     id2a(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
     id2a(nearly*3456+1:)=0
     call multimode_decoder_core(shared_memory%payload%ss,id2a,local_params, &
          12000,completion,active_generation)
     local_params%nzhsym=50
  endif
  
  !ft8md
  if(local_params%nmode.eq.8 .and. local_params%lmultift8 .and.   &
       .not. local_params%nagain .and. local_params%ndiskdat) then 
     npts1=180000
     if(local_params%ndecoderstart.lt.2) then
        nearly=41
        local_params%lmultift8=.false.
        local_params%nzhsym=nearly
        id2a(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
        id2a(nearly*3456+1:)=0
        call multimode_decoder_core(shared_memory%payload%ss,id2a,local_params, &
             12000,completion,active_generation)
        if(local_params%ndecoderstart.lt.2) then
           nearly=46
           local_params%lmultift8=.false.
           local_params%nzhsym=nearly
           id2a(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
           id2a(nearly*3456+1:)=0
           call multimode_decoder_core(shared_memory%payload%ss,id2a,local_params, &
                12000,completion,active_generation)
        endif
        if(local_params%ndecoderstart.eq.0) nearly=49
        if(local_params%ndecoderstart.eq.1) nearly=50
        local_params%lmultift8=.true.
        shared_memory%payload%params%nzhsym=nearly
        id2a(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
        id2a(nearly*3456+1:)=0
        dd(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
        dd(nearly*3456+1:)=0
        dd8(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
        dd8(nearly*3456+1:)=0
        if(local_params%ndecoderstart.eq.0) shared_memory%payload%params%nzhsym=49
        if(local_params%ndecoderstart.eq.1) shared_memory%payload%params%nzhsym=50
     else
        nearly=50
        if(local_params%ndecoderstart.eq.2) nearly=48
        if(local_params%ndecoderstart.eq.3) nearly=49
        if(local_params%ndecoderstart.eq.4) nearly=50
        shared_memory%payload%params%nzhsym=nearly
        id2a(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
        id2a(nearly*3456+1:)=0
        dd(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
        dd(nearly*3456+1:)=0
        dd8(1:nearly*3456)=shared_memory%payload%id2(1:nearly*3456)
        dd8(nearly*3456+1:)=0
        if(local_params%ndecoderstart.eq.2) shared_memory%payload%params%nzhsym=48
        if(local_params%ndecoderstart.eq.3) shared_memory%payload%params%nzhsym=49
        if(local_params%ndecoderstart.eq.4) shared_memory%payload%params%nzhsym=50
     endif
  elseif (local_params%nmode.eq.8 .and. local_params%lmultift8 .and. .not. &
       local_params%ndiskdat) then
     npts1=180000
     dd(1:npts1)=shared_memory%payload%id2(1:npts1)
     rms=sum(abs(dd(1:10))) + sum(abs(dd(76001:76010))) + sum(abs(dd(151670:151680)))
     dd8(1:npts1)=dd(1:npts1)

!### WHY WAS THIS STUFF HERE ??? ###
!     if(rms.gt.0.001) then
!        dd(1:npts1)=shared_memory%payload%dd2(1:npts1)
!        dd8(1:npts1)=dd(1:npts1)
!print *,'win7',rms
!     else ! workaround for zero data values of dd2 array under WinXP
!        dd(1:npts1)=shared_memory%payload%id2(1:npts1)
!        dd8(1:npts1)=dd(1:npts1)
!print *, 'winxp',rms
!     endif
!###
  endif
  !end ft8md

  if(local_params%nmode .eq. 144) then
    ! MSK144
     call decode_msk144_core(shared_memory%payload%id2, local_params, data_dir, &
          completion)
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

  return
end subroutine jt9a
