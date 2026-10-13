subroutine decoder_engine_test_jt4_signal(rate,submode,seed,amplitude,noise,frequency,samples) bind(C)
  use iso_c_binding, only: c_int,c_int16_t,c_float
  implicit none
  integer(c_int), value :: rate,submode,seed
  real(c_float), value :: amplitude,noise,frequency
  integer(c_int16_t), intent(out) :: samples(60*rate)
  call jt4_engine_make_signal('K1ABC W9XYZ FN42',rate,submode,seed,amplitude,noise,frequency,samples)
end subroutine

subroutine jt4_engine_make_signal(message,rate,submode,seed,amplitude,noise,frequency,samples)
  use iso_c_binding, only: c_int,c_int16_t,c_float
  use iso_fortran_env, only: int64,real64
  use jt4, only: nch
  implicit none
  character(len=*), intent(in) :: message
  integer(c_int), intent(in) :: rate,submode,seed
  real(c_float), intent(in) :: amplitude,noise,frequency
  integer(c_int16_t), intent(out) :: samples(60*rate)
  real(real64), parameter :: twopi=6.283185307179586476925286766559_real64
  integer(int64) :: state
  integer :: tones(206),i,symbol,itype
  real(real64) :: phase,value,u1,u2,freq
  character(len=22) :: encoded,sent

  if(submode<0.or.submode>6) error stop 'invalid JT4 fixture submode'
  if(rate/=11025.and.rate/=12000) error stop 'invalid JT4 fixture sample rate'
  encoded=message
  call gen4(encoded,0,sent,tones,itype)
  state=104729_int64+int(seed,int64)
  phase=0.0_real64
  do i=1,size(samples)
     state=modulo(16807_int64*state,2147483647_int64)
     u1=max(real(state,real64)/2147483647.0_real64,tiny(1.0_real64))
     state=modulo(16807_int64*state,2147483647_int64)
     u2=real(state,real64)/2147483647.0_real64
     value=noise*sqrt(-2.0_real64*log(u1))*cos(twopi*u2)
     if(i>rate) then
        symbol=int(real(i-rate-1,real64)*4.375_real64/rate)+1
        if(symbol<=206) then
           freq=frequency+tones(symbol)*4.375_real64*nch(submode+1)
           phase=modulo(phase+twopi*freq/rate,twopi)
           value=value+amplitude*sin(phase)
        endif
     endif
     samples(i)=int(nint(max(-32768.0_real64,min(32767.0_real64,value))),c_int16_t)
  enddo
end subroutine
