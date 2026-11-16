module q65_host_support
  use q65_decode, only: q65_decoder,q65_options
  use q65_callers, only: Q65_MAX_CALLERS,q65_load_callers,q65_save_callers
  use types, only: q3list
  use prog_args, only: data_dir,temp_dir
  use streaming_side, only: side_q65_sync,side_q65_callers
  implicit none
  private
  public :: load_q65_history,save_q65_history,write_q65_curves,write_q65_diagnostic
contains
  subroutine load_q65_history(decoder,status)
    class(q65_decoder), intent(inout) :: decoder
    integer, intent(out) :: status
    type(q3list) :: callers(Q65_MAX_CALLERS)
    integer :: count
    call q65_load_callers(trim(data_dir)//'/tsil.3q',callers,count,status)
    call decoder%set_callers(callers,count)
  end subroutine

  ! utc: the attempt's label, for the q65_callers event (lib/streaming_side.f90),
  ! which carries the records the attempt leaves, saved or not.
  subroutine save_q65_history(decoder,load_status,utc)
    class(q65_decoder), intent(in) :: decoder
    integer, intent(in) :: load_status,utc
    type(q3list) :: callers(Q65_MAX_CALLERS)
    integer :: count,status
    if(load_status/=0) return
    call decoder%get_callers(callers,count)
    call q65_save_callers(trim(data_dir)//'/tsil.3q',callers,count,status)
    call side_q65_callers(utc,count,callers(:count)%call,callers(:count)%grid, &
         callers(:count)%nsec,callers(:count)%nfreq)
  end subroutine

  subroutine write_q65_diagnostic(line)
    character(len=*), intent(in) :: line
    integer :: unit,status
    open(newunit=unit,file=trim(data_dir)//'/q65_decodes.txt',status='unknown',position='append',iostat=status)
    if(status/=0) return
    write(unit,'(a)') trim(line)
    close(unit)
  end subroutine

  subroutine write_q65_curves(decoder,options)
    class(q65_decoder), intent(in) :: decoder
    type(q65_options), intent(in) :: options
    real, allocatable :: current(:),average(:)
    real :: df,dt,y0,y0_average,g_average,empty_current(0),empty_average(0)
    integer :: even_count,odd_count,count,i1,i2,i,unit,status,active_count
    call decoder%get_curves(even_count,odd_count,df,dt,empty_current,empty_average,count)
    if(count<=0.or.df<=0) return
    allocate(current(count),average(count))
    call decoder%get_curves(even_count,odd_count,df,dt,current,average,count,active_count)
    i1=max(1,nint(options%low_frequency/df))
    i2=min(count,int(options%high_frequency/df))
    if(i2<i1) return
    y0=minval(current(i1:i2))
    y0_average=minval(average(i1:i2))
    g_average=0
    if(active_count>=2) g_average=0.4
    ! The q65_sync event (lib/streaming_side.f90): the values below, as written.
    call side_q65_sync(options%utc,dt,g_average*minval(average),g_average*maxval(average),df,i1, &
         g_average*(average(i1:i2)-y0_average),0.4*(current(i1:i2)-y0))
    open(newunit=unit,file=trim(temp_dir)//'/red.dat',status='replace',iostat=status)
    if(status/=0) return
    write(unit,1000) dt,g_average*minval(average),g_average*maxval(average)
    do i=i1,i2
       write(unit,1000) i*df,g_average*(average(i)-y0_average),0.4*(current(i)-y0)
    enddo
1000 format(f10.3,2f15.6)
    close(unit)
  end subroutine
end module q65_host_support
