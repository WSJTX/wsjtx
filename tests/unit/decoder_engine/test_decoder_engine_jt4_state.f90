program test_decoder_engine_jt4_state
  use iso_c_binding, only: c_int16_t
  use jt4_decode, only: jt4_decoder,jt4_options,jt4_average_entry
  implicit none
  type(jt4_decoder), allocatable :: decoder,other
  type(jt4_average_entry) :: entries(64)
  type(jt4_options) :: options
  integer(c_int16_t) :: silence(1)=0
  real :: sym(207,7),symbols(207)
  integer :: nsum,i,slot,entry_count,ncount,status
  character(len=22) :: decoded
  interface
     subroutine ignore_result(this,snr,dt,freq,have_sync,sync,is_deep,decoded,qual,ich,is_average,ave)
       import jt4_decoder
       class(jt4_decoder),intent(inout) :: this
       integer,intent(in) :: snr,freq,ich,ave
       real,intent(in) :: dt,qual
       logical,intent(in) :: have_sync,is_deep,is_average
       character,intent(in) :: sync
       character(len=22),intent(in) :: decoded
     end subroutine
  end interface

  allocate(decoder,other)
  allocate(decoder%rsymbol(207,7),decoder%ppsave(207,7,64))
  decoder%rsymbol=2.0
  call decoder%save_average(120000,4.0,0.0,1.0,1000,20)
  call check(decoder%average_count==1,'first reception')
  decoder%rsymbol=4.0
  call decoder%save_average(120000,6.0,0.01,1.0,1010,20)
  call check(decoder%average_count==1,'same minute and nearby frequency deduplicate')
  call decoder%save_average(120200,6.0,0.1,1.0,1010,20)
  call decoder%select_average(120200,0.0,1.0,1000,20,sym,nsum)
  call check(nsum==2.and.all(abs(sym-3.0)<1.e-6),'matching receptions average symbols')
  call check(count(decoder%average_used)==2,'selected reception flags')

  call decoder%save_average(120100,8.0,0.0,1.0,1000,20)
  call decoder%save_average(120400,8.0,0.5,1.0,1000,20)
  call decoder%save_average(120600,8.0,0.0,-1.0,1000,20)
  call decoder%save_average(120800,8.0,0.0,1.0,1100,20)
  call decoder%select_average(120200,0.0,1.0,1000,20,sym,nsum)
  call check(nsum==2,'parity, DT, polarity and frequency exclude incompatible receptions')
  call check(decoder%average_count==6,'excluded receptions remain in history')
  call check(other%average_count==0,'decoder instances do not share history')

  call decoder%clear_averages()
  call check(decoder%average_count==0.and.all(decoder%iutc<0),'clear removes all entries')
  call check(.not.any(decoder%average_used),'clear removes selection flags')
  decoder%rsymbol=1.0
  call decoder%save_average(120000,4.0,0.0,1.0,1000,20)
  call check(decoder%average_count==1,'cleared reception can be inserted again')

  call decoder%clear_averages()
  do i=0,64
     decoder%rsymbol=real(i)
     call decoder%save_average((i/60)*10000+mod(i,60)*100,4.0,0.0,1.0,1000,20)
  enddo
  call check(decoder%average_count==64,'history capacity after ring wrap')
  call check(count(decoder%iutc>=0)==64,'all slots remain visible after ring wrap')
  call check(.not.any(decoder%iutc==0),'oldest reception evicted')
  call check(any(decoder%iutc==10400),'newest reception retained')
  call decoder%get_averages(entries,entry_count)
  call check(entry_count==64,'snapshot reports all entries after ring wrap')
  call check(entries(1)%utc==10400,'snapshot preserves physical slot order')
  call decoder%select_average(10400,0.0,1.0,1000,20,sym,nsum)
  call check(nsum==32,'parity selection remains correct across hour and ring wrap')
  call check(all(abs(sym-33.0)<1.e-6),'wrapped average contains precisely retained even minutes')
  slot=decoder%nsave
  call decoder%save_average(10400,9.0,0.0,1.0,1001,20)
  call check(decoder%nsave==slot.and.decoder%average_count==64,'duplicate does not advance ring')

  options%submode=1
  call decoder%decode_pcm(options,silence,11025,ignore_result,status)
  call check(status==0,'prepare changed submode')
  decoder%rsymbol=1.0
  call decoder%save_average(120000,4.0,0.0,1.0,1000,20)
  call decoder%decode_pcm(options,silence,11025,ignore_result,status)
  call check(status==0.and.decoder%average_count==1,'same width profile retains compatible history')
  options%minimum_width_profile=1
  call decoder%decode_pcm(options,silence,11025,ignore_result,status)
  call check(status==0.and.decoder%average_count==0,'changed width profile clears incompatible history')
  deallocate(decoder,other)

  do i=0,1
     symbols=real(i)
     decoded='stale decoded message'
     ncount=42
     call extract4(symbols,ncount,decoded)
     call check(ncount<0.and.len_trim(decoded)==0,'zero-variance symbols fail without stale message')
  enddo

contains
  subroutine check(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message
    if(.not.condition) then
       print *, 'JT4 average state: ',message
       stop 1
    endif
  end subroutine
end program

subroutine ignore_result(this,snr,dt,freq,have_sync,sync,is_deep,decoded,qual,ich,is_average,ave)
  use jt4_decode, only: jt4_decoder
  implicit none
  class(jt4_decoder),intent(inout) :: this
  integer,intent(in) :: snr,freq,ich,ave
  real,intent(in) :: dt,qual
  logical,intent(in) :: have_sync,is_deep,is_average
  character,intent(in) :: sync
  character(len=22),intent(in) :: decoded
end subroutine
