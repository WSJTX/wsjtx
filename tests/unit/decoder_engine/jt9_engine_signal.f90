module jt9_engine_test_reference
  use jt9_decode, only: jt9_decoder
  implicit none
  type, extends(jt9_decoder) :: reference_decoder
     logical :: found=.false.
     real :: result(4)=0.
  end type
contains
  subroutine observe_reference(this,sync,snr,dt,freq,drift,decoded)
    class(jt9_decoder), intent(inout) :: this
    real, intent(in) :: sync,dt,freq
    integer, intent(in) :: snr,drift
    character(len=22), intent(in) :: decoded
    if(decoded/='CQ K1JT FN20') return
    select type(this)
    type is(reference_decoder)
       this%found=.true.
       this%result=[freq,dt,sync,real(snr)]
    end select
  end subroutine
end module

subroutine decoder_engine_test_jt9_signal(submode,start_sample,spread_hz,frequency_hz,samples) bind(C)
  use iso_c_binding, only: c_int,c_int16_t,c_float
  implicit none
  integer(c_int), value :: submode,start_sample
  real(c_float), value :: spread_hz,frequency_hz
  integer(c_int16_t), intent(out) :: samples(720000)
  interface
     subroutine decoder_engine_test_jt9_signal_level(submode,start_sample,spread_hz, &
          frequency_hz,amplitude,samples) bind(C)
       import :: c_int,c_int16_t,c_float
       integer(c_int), value :: submode,start_sample
       real(c_float), value :: spread_hz,frequency_hz,amplitude
       integer(c_int16_t), intent(out) :: samples(720000)
     end subroutine
  end interface
  call decoder_engine_test_jt9_signal_level(submode,start_sample,spread_hz,frequency_hz,45.0,samples)
end subroutine

! The signal at the given amplitude over the test noise (uniform, +/-100).
subroutine decoder_engine_test_jt9_signal_level(submode,start_sample,spread_hz, &
     frequency_hz,amplitude,samples) bind(C)
  use iso_c_binding, only: c_int,c_int16_t,c_int64_t,c_float
  implicit none
  integer(c_int), value :: submode,start_sample
  real(c_float), value :: spread_hz,frequency_hz,amplitude
  integer(c_int16_t), intent(out) :: samples(720000)
  integer :: tones(85),itype,i,symbol,j
  integer(c_int64_t) :: random_state
  real(kind=8) :: phase,step,twopi
  character(len=22) :: message,sent

  message='CQ K1JT FN20'
  call gen9(message,0,sent,tones,itype)
  if(sent/=message) error stop 'JT9 engine test message did not encode'
  random_state=1234567_c_int64_t
  do i=1,size(samples)
     random_state=mod(16807_c_int64_t*random_state,2147483647_c_int64_t)
     samples(i)=int(mod(random_state,201_c_int64_t)-100_c_int64_t,c_int16_t)
  enddo
  phase=0.0d0
  twopi=8.0d0*atan(1.0d0)
  do symbol=1,85
     step=twopi*(real(frequency_hz,kind=8)+tones(symbol)*(12000.0d0/6912.0d0)*2**submode)/12000.0d0
     do i=1,6912
        j=start_sample+(symbol-1)*6912+i
        phase=modulo(phase+step+twopi*spread_hz*sin(twopi*0.4d0*j/12000.0d0)/12000.0d0,twopi)
        samples(j)=samples(j)+int(real(amplitude,kind=8)*sin(phase),c_int16_t)
     enddo
  enddo
end subroutine

subroutine decoder_engine_test_jt9_reference(submode,windowed,samples,result) bind(C)
  use jt9_decode, only: jt9_spectrum_workspace
  use jt9_engine_test_reference, only: reference_decoder,observe_reference
  include 'jt9com.f90'
  integer(c_int), value :: submode,windowed
  integer(c_short), intent(in) :: samples(720000)
  real(c_float), intent(out) :: result(4)
  type(dec_data), allocatable :: shared
  type(reference_decoder) :: decoder
  type(jt9_spectrum_workspace) :: workspace
  real, allocatable :: spectra(:,:),waterfall(:)
  real :: power,peak,df
  integer :: row,half_symbols,npts8
  logical(c_bool) :: low_sidelobes

  allocate(shared,spectra(184,NSMAX),waterfall(NSMAX))
  shared%ss=0.
  shared%id2(:720000)=samples
  low_sidelobes=windowed/=0
  do row=1,181
     call symspec(shared,row*3456,6912,0,low_sidelobes,0,power,waterfall, &
          df,half_symbols,npts8,peak,0.0)
  enddo
  call workspace%prepare_spectra(samples,181,spectra,logical(low_sidelobes))
  if(maxval(abs(spectra-shared%ss))>2.e-6*maxval(shared%ss)) &
       error stop 'JT9 PCM spectrum differs from the legacy symspec spectrum'
  call decoder%decode(observe_reference,shared%ss,shared%id2,1000,.true.,90000, &
       900,900,1100,20,181,.false.,3,9,submode,0)
  call decoder%reset()
  call workspace%clear()
  if(.not.decoder%found) error stop 'JT9 legacy spectrum reference did not decode'
  result=decoder%result
end subroutine
