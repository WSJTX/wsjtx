program test_decoder_engine_q65_host
  use iso_c_binding, only: c_int,c_int16_t,c_int64_t,c_float,c_int8_t,c_sizeof,c_loc,c_f_pointer
  use iso_fortran_env, only: error_unit
  use decoder_engine, only: engine_host_decode
  use ft8_engine_kernel, only: params_block
  use decode_completion_module, only: decode_completion_result
  use prog_args, only: data_dir,temp_dir
  use q65_callers, only: Q65_MAX_CALLERS,q65_load_callers,q65_save_callers
  use types, only: q3list
  implicit none
  integer, parameter :: sample_count=60*12000
  integer(c_int16_t), allocatable, target :: samples(:),before(:)
  type(params_block), target :: params
  type(decode_completion_result) :: completion
  integer(c_int8_t), pointer :: parameter_bytes(:)
  integer(c_int8_t), allocatable :: saved_parameters(:)
  type(q3list), allocatable :: callers(:)
  integer(c_int64_t) :: input_id=0,analysis_id=0
  integer :: attempt=0,status,count,now
  character(len=16) :: scenario

  interface
     subroutine gen_signal(period,submode,flag,seed,amplitude,noise,frequency,samples) &
          bind(C,name='decoder_engine_test_q65_signal')
       import c_int,c_int16_t,c_float
       integer(c_int), value :: period,submode,flag,seed
       real(c_float), value :: amplitude,noise,frequency
       integer(c_int16_t), intent(out) :: samples(*)
     end subroutine
  end interface

  allocate(samples(sample_count),before(sample_count),callers(Q65_MAX_CALLERS))
  call c_f_pointer(c_loc(params),parameter_bytes,[int(c_sizeof(params))])
  allocate(saved_parameters(size(parameter_bytes)))
  parameter_bytes=0
  data_dir='.'
  temp_dir='.'
  params%nmode=66
  params%ntr=60
  params%nfqso=1000
  params%nfa=850
  params%nfb=1150
  params%ntol=50
  params%ndepth=17
  params%nexp_decode=32
  params%emedelay=1
  params%ndiskdat=.true.
  params%mycall=' '
  params%hiscall=' '
  params%hisgrid=' '
  call remove_file('tsil.3q')

  call get_command_argument(1,scenario)
  select case(trim(scenario))
  case('')
     call check_averaging()
  case('--contest')
     call check_contest_history()
  case default
     error stop 'unknown host test scenario'
  end select

  deallocate(samples,before,callers,saved_parameters)

contains

  subroutine check_averaging()
    integer(c_int16_t), allocatable :: fixtures(:,:)
    integer :: seed

    allocate(fixtures(sample_count,4))
    do seed=1,3
       call gen_signal(60,0,0,seed,3.8,100.0,1000.0,fixtures(:,seed))
    enddo
    call gen_signal(60,0,0,4,300.0,100.0,1000.0,fixtures(:,4))
    call fresh(10000,fixtures(:,1),.true.)
    call decode(0,1000)
    call repeat()
    call decode(0,1000)
    call fresh(10200,fixtures(:,2),.false.)
    params%lbandchanged=.true.
    call decode(1,2000,.true.)
    params%lbandchanged=.false.
    call fresh(10300,fixtures(:,3),.false.)
    call decode(0,2001)
    call repeat()
    params%nclearave=.true.
    call decode(0,0)
    params%nclearave=.false.

    params%ndepth=145
    call fresh(10400,fixtures(:,4),.false.)
    call decode(1,0)
    call repeat()
    call decode(1,0)
    call fresh(10600,fixtures(:,1),.true.)
    call decode(0,1000)
    params%lmodechanged=.true.
    call fresh(10800,fixtures(:,2),.false.)
    call decode(1,0)
    params%lmodechanged=.false.

    params%ndepth=1
    call fresh(11000,fixtures(:,1),.true.)
    call decode(0,1000)
    call fresh(11200,fixtures(:,2),.false.)
    call decode(0,2000)
  end subroutine

  subroutine check_contest_history()
    integer(c_int16_t), allocatable :: fixture(:)

    allocate(fixture(sample_count))
    call gen_signal(60,0,0,1,3.0,100.0,1000.0,fixture)
    params%ndepth=1
    params%nexp_decode=1
    params%nQSOProgress=5
    params%nfqso=1500
    params%mycall=transfer('K1ABC       ',params%mycall)
    call fresh(11400,fixture,.true.)
    call decode(0,1000)
    callers=q3list('','',0,0,0)
    now=time()
    callers(1)=q3list('W9XYZ','FN42',now-3600,1000,0)
    call q65_save_callers('tsil.3q',callers,1,status)
    call expect(status==0,'seed contest caller history')
    call fresh(11600,fixture,.true.)
    call decode(1,1000)
    call expect(file_contains('host-output.txt','q3'),'contest history enables list-decode retry')
    call q65_load_callers('tsil.3q',callers,count,status)
    call expect(status==0.and.count==1,'contest history persists one caller')
    call expect(callers(1)%call=='W9XYZ'.and.callers(1)%grid=='FN42','contest caller survives host round trip')
    call expect(callers(1)%nsec>=now.and.abs(callers(1)%nfreq-1000)<=10,'successful decode refreshes caller history')
  end subroutine

  subroutine expect(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message
    if(condition) return
    write(error_unit,'(a)') message
    error stop 1
  end subroutine

  subroutine fresh(utc,fixture,clear_averages)
    integer, intent(in) :: utc
    integer(c_int16_t), intent(in) :: fixture(:)
    logical, intent(in) :: clear_averages
    input_id=input_id+1
    analysis_id=analysis_id+1
    attempt=1
    params%nutc=utc
    params%nagain=.false.
    params%newdat=.true.
    params%nclearave=clear_averages
    samples=fixture
    before=fixture
  end subroutine

  subroutine repeat()
    analysis_id=analysis_id+1
    attempt=attempt+1
    params%nagain=.true.
    params%newdat=.false.
    params%nclearave=.false.
  end subroutine

  subroutine decode(messages,averages,require_average)
    integer, intent(in) :: messages,averages
    logical, intent(in), optional :: require_average
    logical :: check_average

    check_average=.false.
    if(present(require_average)) check_average=require_average
    saved_parameters=parameter_bytes
    call remove_file('decoded.txt')
    call remove_file('red.dat')
    open(unit=6,file='host-output.txt',status='replace')
    call engine_host_decode(samples,params,12000,completion,0,input_id,analysis_id,attempt,sample_count)
    close(6)
    call expect(all(before==samples),'host leaves supplied PCM unchanged')
    call expect(all(saved_parameters==parameter_bytes),'host leaves packed parameters unchanged')
    call expect(completion%available,'host reports completion')
    if(completion%decoded/=messages.or.completion%average/=averages) then
       write(error_unit,'(a,i6.6,4(a,i0))') 'UTC ',params%nutc,': decoded ',completion%decoded, &
            ' expected ',messages,' averages ',completion%average,' expected ',averages
       error stop 1
    endif
    call expect(file_contains('decoded.txt','K1ABC W9XYZ FN42').eqv.(messages>0),'current legacy decoded output')
    call expect(file_contains('host-output.txt','K1ABC W9XYZ FN42').eqv.(messages>0),'current GUI decode line')
    call check_curves(check_average)
  end subroutine

  subroutine check_curves(require_average)
    logical, intent(in) :: require_average
    integer :: unit,ios,rows
    real :: x,average,current,maximum
    open(newunit=unit,file='red.dat',status='old',iostat=ios)
    call expect(ios==0,'host writes synchronization curves')
    read(unit,*,iostat=ios) x,average,current
    call expect(ios==0,'curve header is readable')
    rows=0
    maximum=0
    do
       read(unit,*,iostat=ios) x,average,current
       if(ios/=0) exit
       rows=rows+1
       maximum=max(maximum,average)
       call expect(x>=850.and.x<=1155,'curve frequency range follows request')
    enddo
    close(unit)
    call expect(rows>0,'host writes nonempty curve data')
    if(require_average) call expect(maximum>0,'two-reception average is visible in red curve')
  end subroutine

  logical function file_contains(filename,text) result(found)
    character(len=*), intent(in) :: filename,text
    integer :: unit,ios
    character(len=512) :: line
    found=.false.
    open(newunit=unit,file=filename,status='old',iostat=ios)
    if(ios/=0) return
    do
       read(unit,'(a)',iostat=ios) line
       if(ios/=0) exit
       if(index(line,text)>0) found=.true.
    enddo
    close(unit)
  end function

  subroutine remove_file(filename)
    character(len=*), intent(in) :: filename
    integer :: unit,ios
    open(newunit=unit,file=filename,status='old',iostat=ios)
    if(ios==0) close(unit,status='delete')
  end subroutine
end program
