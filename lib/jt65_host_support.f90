module jt65_host_support
  use jt65_decode, only: jt65_decoder,jt65_average_entry
  use prog_args, only: data_dir
  use streaming_side, only: side_averages
  implicit none
  private
  public :: load_jt65_calls,write_jt65_averages,emit_jt65_averages

contains

  subroutine load_jt65_calls(calls,grids)
    character(len=12), allocatable, intent(out) :: calls(:)
    character(len=4), allocatable, intent(out) :: grids(:)
    character(len=12), allocatable :: call_buffer(:)
    character(len=4), allocatable :: grid_buffer(:)
    character(len=180) :: line
    integer :: unit,ios,count,first,second

    allocate(call_buffer(10000),grid_buffer(10000))
    count=0
    open(newunit=unit,file=trim(data_dir)//'/CALL3.TXT',status='old',action='read',iostat=ios)
    if(ios==0) then
       do while(count<size(call_buffer))
          read(unit,'(a)',iostat=ios) line
          if(ios/=0) exit
          if(line(:4)=='ZZZZ'.or.line(:2)=='//') cycle
          first=index(line,',')
          if(first<4) cycle
          second=index(line(first+1:),',')
          if(second<5) cycle
          count=count+1
          call_buffer(count)=line(:first-1)
          grid_buffer(count)=line(first+1:first+4)
       enddo
       close(unit)
    endif
    calls=call_buffer(:count)
    grids=grid_buffer(:count)
  end subroutine load_jt65_calls

  subroutine write_jt65_averages(decoder,unit)
    class(jt65_decoder), intent(inout) :: decoder
    integer, intent(in) :: unit
    type(jt65_average_entry) :: entries(64)
    integer :: count,i
    character :: used,polarity

    call decoder%get_averages(entries,count)
    rewind(unit)
    do i=1,count
       used='.'
       if(entries(i)%used) used='$'
       polarity=' '
       if(entries(i)%polarity<0) polarity='#'
       if(entries(i)%polarity>0) polarity='*'
       write(unit,'(a1,i5.4,f6.1,f6.2,i6,1x,a1)') used,entries(i)%utc, &
            entries(i)%sync,entries(i)%dt,entries(i)%frequency,polarity
    enddo
    endfile(unit)
    flush(unit)
  end subroutine write_jt65_averages

  ! The averages event (lib/streaming_side.f90): the entries write_jt65_averages
  ! writes, each with its HHMM label, after the attempt labelled utc.
  subroutine emit_jt65_averages(decoder,utc)
    class(jt65_decoder), intent(inout) :: decoder
    integer, intent(in) :: utc
    type(jt65_average_entry) :: entries(64)
    integer :: count

    call decoder%get_averages(entries,count)
    call side_averages('JT65',utc,.false.,count,entries(:count)%used,entries(:count)%utc, &
         entries(:count)%sync,entries(:count)%dt,entries(:count)%frequency,              &
         entries(:count)%polarity)
  end subroutine emit_jt65_averages
end module jt65_host_support
