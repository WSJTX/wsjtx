program test_decoder_engine_fst4_host
  use iso_c_binding, only: c_int,c_int16_t,c_int64_t,c_float,c_int8_t,c_sizeof,c_loc,c_f_pointer
  use iso_fortran_env, only: error_unit
  use decoder_engine, only: engine_host_decode
  use ft8_engine_kernel, only: params_block
  use decode_completion_module, only: decode_completion_result
  use prog_args, only: data_dir,temp_dir
  use ieee_arithmetic, only: ieee_is_finite
  implicit none
  integer(c_int16_t), allocatable, target :: samples(:)
  integer(c_int16_t), allocatable :: before(:)
  type(params_block), target :: params
  type(decode_completion_result) :: completion
  integer(c_int8_t), pointer :: parameter_bytes(:)
  integer(c_int8_t), allocatable :: saved_parameters(:)
  integer(c_int64_t) :: input_id=100,analysis_id=100
  integer, parameter :: modes(4)=[240,241,240,240]
  integer, parameter :: receive_frequencies(4)=[1500,1500,1500,200]
  integer :: mode,mode_index,period,attempt,unit,ios,matches,bins,first_fst4_bins=0
  real :: frequency,power,last_frequency
  character(len=256) :: line
  character(len=37) :: expected
  interface
     subroutine gen_signal(mode,period,frequency,samples) bind(C,name='decoder_engine_test_fst4_signal')
       import c_int,c_int16_t,c_float
       integer(c_int), value :: mode,period
       real(c_float), value :: frequency
       integer(c_int16_t), intent(out) :: samples(*)
     end subroutine
     subroutine hash_signals(count,samples) bind(C,name='decoder_engine_test_fst4_hash_signals')
       import c_int,c_int16_t
       integer(c_int), value :: count
       integer(c_int16_t), intent(out) :: samples(*)
     end subroutine
  end interface
  data_dir='.'
  temp_dir='.'
  call check_patience_mapping()
  open(newunit=unit,file='plotspec',status='replace')
  close(unit)
  call c_f_pointer(c_loc(params),parameter_bytes,[int(c_sizeof(params))])
  allocate(saved_parameters(size(parameter_bytes)))
  do mode_index=1,size(modes)
     mode=modes(mode_index)
     period=15
     expected='K1ABC W9XYZ FN42'
     if(mode==241) then
        period=120
        expected='K1ABC FN42 37'
     endif
     allocate(samples(period*12000),before(period*12000))
     call gen_signal(mode,period,real(receive_frequencies(mode_index),c_float),samples)
     before=samples
     parameter_bytes=0
     params%nmode=mode
     params%ntr=period
     params%nfqso=receive_frequencies(mode_index)
     params%nfa=1400
     params%nfb=1600
     params%ntol=50
     if(params%nfqso==200) then
        params%ntol=500
        params%nfa=params%nfqso-params%ntol
        params%nfb=params%nfqso+params%ntol
     endif
     params%ndepth=1
     params%nexp_decode=32+3*256
     params%ndiskdat=.true.
     params%mycall=' '
     params%hiscall=' '
     input_id=input_id+1
     do attempt=1,2
        params%nagain=attempt==2
        params%newdat=attempt==1
        analysis_id=analysis_id+1
        open(newunit=unit,file='decoded.txt',status='replace')
        close(unit)
        open(52,file='fst4-spectrum.txt',status='replace')
        saved_parameters=parameter_bytes
        call engine_host_decode(samples,params,12000,completion,0,input_id,analysis_id,attempt,size(samples))
        close(52)
        bins=0
        last_frequency=-huge(0.0)
        open(newunit=unit,file='fst4-spectrum.txt',status='old',action='read')
        do
           read(unit,*,iostat=ios) frequency,power
           if(ios<0) exit
           call require(ios==0,'readable Doppler spectrum')
           call require(ieee_is_finite(frequency).and.ieee_is_finite(power),'finite Doppler spectrum')
           call require(frequency>last_frequency.and.power>=0,'ordered bins with nonnegative power')
           last_frequency=frequency
           bins=bins+1
        enddo
        close(unit)
        call require(bins>0,'host emits Doppler spectrum')
        if(mode==240) then
           if(first_fst4_bins==0) first_fst4_bins=bins
           call require(bins==first_fst4_bins,'shorter period emits only its active spectrum bins')
        endif
        call require(completion%available,'completion available')
        call require(completion%decoded==1,'one decoded message')
        call require(all(samples==before),'immutable PCM')
        call require(all(parameter_bytes==saved_parameters),'immutable parameters')
        matches=0
        open(newunit=unit,file='decoded.txt',status='old',action='read')
        do
           read(unit,'(a)',iostat=ios) line
           if(ios/=0) exit
           if(period<60) then
              if(line(37:73)==expected) matches=matches+1
           else
              if(line(35:71)==expected) matches=matches+1
           endif
        enddo
        close(unit)
        call require(matches==1,'one legacy result in current attempt')
     enddo
     deallocate(samples,before)
  enddo
  open(newunit=unit,file='plotspec',status='old')
  close(unit,status='delete')
  allocate(samples(15*12000),before(15*12000))
  call hash_signals(2,samples)
  before=samples
  do mode=241,242
     parameter_bytes=0
     params%nmode=mode
     params%ntr=15
     params%nfqso=1500
     params%nfa=1400
     params%nfb=1600
     params%ntol=100
     params%ndepth=1
     params%nexp_decode=512
     params%ndiskdat=.true.
     params%mycall=' '
     params%hiscall=' '
     input_id=input_id+1
     do attempt=1,2
        params%nagain=attempt==2
        params%newdat=attempt==1
        analysis_id=analysis_id+1
        open(newunit=unit,file='decoded.txt',status='replace')
        close(unit)
        open(6,file='fst4-hash-output.txt',status='replace')
        saved_parameters=parameter_bytes
        call engine_host_decode(samples,params,12000,completion,0,input_id,analysis_id,attempt,size(samples))
        close(6)
        call require(completion%available.and.completion%decoded==2,'two distinct unresolved hash results')
        call require(all(samples==before),'immutable hash PCM')
        call require(all(parameter_bytes==saved_parameters),'immutable hash parameters')
        call verify_hash_output('decoded.txt',mode)
        call verify_hash_output('fst4-hash-output.txt',mode)
     enddo
  enddo
  deallocate(samples,before)
contains
  subroutine check_patience_mapping()
    use fst4_decode, only: fst4_decoder,fst4_options
    use fst4_host_support, only: prepare_fst4_host
    use fftw3, only: FFTW_ESTIMATE,FFTW_ESTIMATE_PATIENT,FFTW_MEASURE,FFTW_PATIENT,FFTW_EXHAUSTIVE
    type(fst4_decoder) :: decoder
    type(fst4_options) :: options
    integer, parameter :: flags(0:4)=[FFTW_ESTIMATE,FFTW_ESTIMATE_PATIENT,FFTW_MEASURE,FFTW_PATIENT,FFTW_EXHAUSTIVE]
    integer :: npatience,nthreads,saved_patience,level
    common/patience/npatience,nthreads
    saved_patience=npatience
    decoder%host_history_loaded=.true.
    call require(options%fft_flags==FFTW_ESTIMATE,'default FFT planning effort')
    do level=0,4
       npatience=level
       options%fft_flags=-1
       call prepare_fst4_host(decoder,options)
       call require(options%fft_flags==flags(level),'host patience applies with history already loaded')
    enddo
    npatience=saved_patience
  end subroutine

  subroutine verify_hash_output(path,mode)
    character(len=*), intent(in) :: path
    integer, intent(in) :: mode
    integer :: unit,ios,canonical,first_hash,second_hash
    character(len=256) :: line
    canonical=0
    first_hash=0
    second_hash=0
    open(newunit=unit,file=path,status='old',action='read')
    do
       read(unit,'(a)',iostat=ios) line
       if(ios/=0) exit
       if(index(line,'<...> FN42AB')>0) canonical=canonical+1
       if(index(line,'<1234567> FN42AB')>0) first_hash=first_hash+1
       if(index(line,'<2345678> FN42AB')>0) second_hash=second_hash+1
    enddo
    close(unit)
    if(mode==241) then
       call require(canonical==2.and.first_hash==0.and.second_hash==0,'ordinary host keeps canonical hash text')
    else
       call require(canonical==0.and.first_hash==1.and.second_hash==1,'hash-print host renders each distinct hash')
    endif
  end subroutine

  subroutine require(condition,description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    if(.not.condition) then
       write(error_unit,'(a)') 'FAIL: '//description
       error stop 1
    endif
  end subroutine
end program test_decoder_engine_fst4_host
