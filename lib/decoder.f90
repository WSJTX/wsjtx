subroutine multimode_decoder_core(id2,params,nfsample,completion,progress_generation)
  use decode_completion_module, only: decode_completion_result, reset_decode_completion, write_decode_progress
  include 'jt9com.f90'
  integer, intent(in) :: progress_generation
  type(params_block) :: params
  type(decode_completion_result), intent(out) :: completion
  external :: run_decoder_engine
  integer*2 id2(NTMAX*12000)

  if(params%nmode.eq.4.or.params%nmode.eq.8.or.params%nmode.eq.5.or.params%nmode.eq.9.or. &
       params%nmode.eq.65.or. &
       params%nmode.eq.66.or.params%nmode.eq.240.or.params%nmode.eq.241.or.params%nmode.eq.242) then
     call run_decoder_engine(id2,params,nfsample,completion,progress_generation, &
          0_c_int64_t,0_c_int64_t,0_c_int, &
          merge(params%kin,0_c_int,params%nmode==4.or.params%nmode==5.or.params%nmode==9.or. &
               params%nmode==65.or.params%nmode==66.or. &
               params%nmode==240.or.params%nmode==241.or.params%nmode==242))
     return
  endif
  call reset_decode_completion(completion)
  call write_decode_progress(progress_generation)
end subroutine multimode_decoder_core

subroutine multimode_decoder(id2,params,nfsample)
  include 'jt9com.f90'

  integer*2 id2(NTMAX*12000)
  type(params_block) :: params

  call multimode_decoder_progress(id2,params,nfsample,0)
end subroutine multimode_decoder

! multimode_decoder, its decoders reporting their progress under generation
! (lib/decode_completion.f90; 0: none).
subroutine multimode_decoder_progress(id2,params,nfsample,generation)
  use prog_args, only: lquiet
  use streaming_emit, only: streaming_emit_enabled,                       &
       streaming_emit_decode_finished, streaming_emit_flush
  use decode_completion_module, only: decode_completion_result,          &
       write_decode_completion

  include 'jt9com.f90'

  integer*2 id2(NTMAX*12000)
  type(params_block) :: params
  integer, intent(in) :: generation
  type(decode_completion_result) :: completion

  call multimode_decoder_core(id2,params,nfsample,completion,generation)
  if (.not. completion%available) return

  if (streaming_emit_enabled()) then
     call streaming_emit_decode_finished(params%nutc,params%nmode==4.or.params%nmode==66.or. &
          params%nmode==240.or. &
          params%nmode==241.or.params%nmode==242, &
          [completion%synchronized,completion%decoded,completion%average])
  else if (.not. lquiet) then
     call write_decode_completion(completion)
  end if
  call streaming_emit_flush()
end subroutine multimode_decoder_progress
