program test_decoder_engine_jt4_host
  use iso_c_binding, only: c_int,c_int8_t,c_int16_t,c_int64_t,c_float,c_sizeof
  use decoder_engine, only: engine_host_decode, engine_host_decode_request, &
       attempt_request, host_decode_context, params_to_request
  use decoder_engine_types, only: engine_jt4_options
  use ft8_engine_kernel, only: params_block
  use jt4_decode, only: jt4_decoder
  use jt4_host_support, only: convert_jt4_host_options,load_jt4_calls,write_jt4_averages
  use decode_completion_module, only: decode_completion_result
  use prog_args, only: data_dir,temp_dir
  implicit none
  type(params_block) :: params,before_params
  type(engine_jt4_options) :: options
  type(jt4_decoder), allocatable :: decoder
  type(decode_completion_result) :: completion
  integer(c_int8_t) :: zeros(c_sizeof(params))
  integer(c_int16_t), allocatable, target :: samples(:)
  integer(c_int16_t), allocatable :: before(:)
  integer(c_int64_t) :: input_id=700,analysis_id=700
  integer :: npatience,nthreads,unit,ios,attempt=1
  character(len=100) :: line
  logical :: exists
  common/patience/npatience,nthreads
  interface
     subroutine signal(rate,submode,seed,amplitude,noise,frequency,samples) bind(C,name='decoder_engine_test_jt4_signal')
       import c_int,c_int16_t,c_float
       integer(c_int), value :: rate,submode,seed
       real(c_float), value :: amplitude,noise,frequency
       integer(c_int16_t), intent(out) :: samples(*)
     end subroutine
  end interface

  data_dir='.'
  temp_dir='.'
  zeros=0
  params=transfer(zeros,params)
  params%nmode=4
  params%ntr=60
  params%nutc=1234
  params%nfqso=1000
  params%ntol=50
  params%ndepth=3+16+32+128
  params%mycall=transfer('K1ABC       ',params%mycall)
  params%hiscall=transfer('W9XYZ       ',params%hiscall)
  params%hisgrid=transfer('FN42  ',params%hisgrid)
  before_params=params
  call convert_jt4_host_options(params,options)
  call require(options%utc==123400,'legacy HHMM converts to HHMMSS')
  call require(options%depth==3.and.options%averaging==1.and.options%deep_search==1,'packed effort separates feature flags')
  call require(all(transfer(params,zeros)==transfer(before_params,zeros)),'conversion does not mutate legacy parameters')

  allocate(decoder)
  npatience=0
  call remove_file('CALL3.TXT')
  call load_jt4_calls(decoder)
  inquire(file='CALL3.TXT',exist=exists)
  call require(.not.exists,'missing call list is not created by loading')
  call require(size(decoder%calls)==0,'missing call list clears template knowledge')
  open(newunit=unit,file='CALL3.TXT',status='replace')
  write(unit,'(a)') '// ignored comment','invalid','W9XYZ,FN42,,','K9XYZ,EN50,,','ZZZZ,AA00,,','N0CALL,EM00,,'
  close(unit)
  call load_jt4_calls(decoder)
  call require(size(decoder%calls)==2,'CALL3 terminator stops parsing')
  call require(decoder%calls(1)%call=='W9XYZ'.and.decoder%calls(2)%call=='K9XYZ','CALL3 ordering is preserved')
  call require(decoder%calls(1)%grid=='FN42','CALL3 grid survives loading')
  open(newunit=unit,file='CALL3.TXT',status='replace')
  write(unit,'(a)') 'N0CALL,EM00,,'
  close(unit)
  call load_jt4_calls(decoder)
  call require(size(decoder%calls)==1.and.decoder%calls(1)%call=='N0CALL','changed call list replaces host knowledge')

  call decoder%save_average(123400,5.0,0.1,1.0,1000,50)
  call decoder%save_average(123600,6.0,0.1,-1.0,1000,50)
  decoder%average_used(1)=.true.
  open(newunit=unit,file='avemsg.txt',status='replace')
  call write_jt4_averages(decoder,unit)
  rewind(unit)
  read(unit,'(a)') line
  call require(line(1:1)=='$'.and.line(3:6)=='1234'.and.index(line,'*')>0,'legacy average row UTC and selection')
  read(unit,'(a)') line
  call require(line(1:1)=='.'.and.index(line(2:),'$')>0,'legacy inverted-polarity average marker')
  call decoder%clear_averages()
  call write_jt4_averages(decoder,unit)
  rewind(unit)
  read(unit,'(a)',iostat=ios) line
  call require(ios<0,'clear truncates old average output')
  close(unit)
  deallocate(decoder)

  allocate(samples(60*11025),before(60*11025))
  params%ndepth=1
  params%newdat=.true.
  params%ndiskdat=.true.
  params%nclearave=.true.
  samples=0
  before=samples
  call run(0)
  params%nclearave=.false.
  call signal(11025,0,1,300.0,100.0,1000.0,samples)
  before=samples
  call run(1)
  call require(output_contains('K1ABC W9XYZ FN42'),'legacy host prints recovered message')
  call require(output_contains('1234'),'legacy host retains HHMM output')
  params%newdat=.false.
  params%nagain=.true.
  attempt=2
  analysis_id=analysis_id+1
  call run(1)
  call require(output_contains('K1ABC W9XYZ FN42'),'manual repeat decodes retained input')
  input_id=input_id+1
  analysis_id=analysis_id+1
  attempt=1
  params%newdat=.true.
  params%nagain=.false.
  samples=0
  before=0
  call run(0)
  call require(.not.output_contains('K1ABC'),'no-sync output cannot reuse prior message')
  call new_period(1234)
  params%ndepth=3+16
  call signal(11025,0,5,5.0,100.0,1000.0,samples)
  before=samples
  call run(0)
  call require(average_rows()==1,'weak synchronized reception enters average history')
  call require(.not.output_contains('K1ABC'),'sync-only observation is not a recovered message')

  params%minsync=100
  params%nagain=.true.
  params%newdat=.false.
  attempt=2
  analysis_id=analysis_id+1
  call run(0)
  call require(output_contains('1234'),'no-sync observation remains visible without counting as decoded')
  params%minsync=0

  call new_period(1235)
  params%nclearave=.true.
  samples=0
  before=samples
  call run(0)
  params%nclearave=.false.

  call new_period(1236)
  call signal(11025,0,6,5.0,100.0,1000.0,samples)
  before=samples
  call run(0)
  call require(average_rows()==1,'quiet clear prevents reuse of the preceding weak reception')
  call compare_gate(4)
  call compare_gate(5)

  call new_period(1237)
  params%nmode=4
  params%ndepth=1
  params%nclearave=.true.
  call signal(11025,0,1,300.0,100.0,1000.0,samples)
  before=samples
  call compare_typed_decode()
  deallocate(samples,before)

contains
  subroutine require(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message
    if(.not.condition) then
       print *, 'JT4 host: ',message
       error stop 1
    endif
  end subroutine

  subroutine run(expected)
    integer, intent(in) :: expected
    before_params=params
    open(unit=6,file='host-output.txt',status='replace')
    call engine_host_decode(samples,params,11025,completion,0,input_id,analysis_id,attempt,size(samples))
    close(6)
    call require(completion%available.and.completion%decoded==expected,'host completion counts only recovered messages')
    call require(all(before==samples),'host leaves supplied PCM unchanged')
    call require(all(transfer(params,zeros)==transfer(before_params,zeros)),'host leaves packed parameters unchanged')
  end subroutine

  subroutine make_request(request,context)
    type(attempt_request), intent(out) :: request
    type(host_decode_context), intent(out) :: context
    request=attempt_request()
    context=host_decode_context()
    request%mode=params%nmode
    request%input_id=input_id
    request%analysis_id=analysis_id
    request%attempt_no=attempt
    request%source=1
    call params_to_request(params,request)
    context%legacy_mode=params%nmode
    context%utc=params%nutc
    context%half_symbol_stage=params%nzhsym
    context%again=params%nagain
    context%clear_average=params%nclearave
  end subroutine

  subroutine compare_gate(mode)
    integer, intent(in) :: mode
    type(attempt_request) :: request
    type(host_decode_context) :: context
    type(decode_completion_result) :: legacy
    params%nmode=mode
    params%nclearave=.false.
    samples=0
    before=samples
    call engine_host_decode(samples,params,11025,legacy,0,input_id,analysis_id,attempt,size(samples))
    call make_request(request,context)
    call engine_host_decode_request(samples,request,context,11025,completion,0,size(samples))
    call require(legacy%available.and.completion%available,'both gated completions available')
    call require(legacy%decoded==0.and.completion%decoded==legacy%decoded,'typed and legacy quiet gates agree')
    call require(all(samples==before),'quiet gate leaves PCM unchanged')
  end subroutine

  subroutine compare_typed_decode()
    type(attempt_request) :: request
    type(host_decode_context) :: context
    type(decode_completion_result) :: legacy
    character(len=256) :: expected(100),row
    integer :: count,status,file_unit,i
    call run(1)
    legacy=completion
    count=0
    open(newunit=file_unit,file='host-output.txt',status='old')
    do
       read(file_unit,'(a)',iostat=status) row
       if(status/=0) exit
       count=count+1
       call require(count<=size(expected),'legacy output fits test buffer')
       expected(count)=row
    enddo
    close(file_unit)
    call require(count>0,'legacy decode emitted output')
    input_id=input_id+1
    analysis_id=analysis_id+1
    call make_request(request,context)
    open(unit=6,file='host-output.txt',status='replace')
    call engine_host_decode_request(samples,request,context,11025,completion,0,size(samples))
    close(6)
    call require(completion%available.eqv.legacy%available,'typed completion availability matches')
    call require(completion%decoded==legacy%decoded,'typed completion count matches')
    call require(all(samples==before),'typed host leaves PCM unchanged')
    open(newunit=file_unit,file='host-output.txt',status='old')
    do i=1,count
       read(file_unit,'(a)',iostat=status) row
       call require(status==0,'typed output has all legacy lines')
       call require(row==expected(i),'typed output matches legacy line')
    enddo
    read(file_unit,'(a)',iostat=status) row
    call require(status<0,'typed output has no extra lines')
    close(file_unit)
  end subroutine

  subroutine new_period(utc)
    integer, intent(in) :: utc
    params%nutc=utc
    params%newdat=.true.
    params%nagain=.false.
    input_id=input_id+1
    analysis_id=analysis_id+1
    attempt=1
  end subroutine

  integer function average_rows() result(count)
    integer :: file_unit,status
    character(len=100) :: row
    count=0
    open(newunit=file_unit,file='avemsg.txt',status='old')
    do
       read(file_unit,'(a)',iostat=status) row
       if(status/=0) exit
       count=count+1
    enddo
    close(file_unit)
  end function

  logical function output_contains(text) result(found)
    character(len=*), intent(in) :: text
    integer :: file_unit,status
    character(len=256) :: row
    found=.false.
    open(newunit=file_unit,file='host-output.txt',status='old')
    do
       read(file_unit,'(a)',iostat=status) row
       if(status/=0) exit
       if(index(row,text)>0) found=.true.
    enddo
    close(file_unit)
  end function

  subroutine remove_file(path)
    character(len=*), intent(in) :: path
    integer :: file_unit,status
    open(newunit=file_unit,file=path,status='old',iostat=status)
    if(status==0) close(file_unit,status='delete')
  end subroutine
end program
