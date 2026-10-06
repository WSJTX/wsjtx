module jt4_host_support
  use jt4_decode, only: jt4_decoder,jt4_call,jt4_average_entry
  use prog_args, only: data_dir
  use fftw3, only: FFTW_ESTIMATE,FFTW_ESTIMATE_PATIENT,FFTW_MEASURE,FFTW_PATIENT,FFTW_EXHAUSTIVE
  implicit none
  private
  public :: prepare_jt4_host,load_jt4_calls,write_jt4_averages,write_jt4_diagnostic

contains


  subroutine prepare_jt4_host(decoder)
    class(jt4_decoder), intent(inout) :: decoder
    integer :: flags,npatience,nthreads
    common/patience/npatience,nthreads

    flags=FFTW_ESTIMATE
    select case(npatience)
    case(1)
       flags=FFTW_ESTIMATE_PATIENT
    case(2)
       flags=FFTW_MEASURE
    case(3)
       flags=FFTW_PATIENT
    case(4)
       flags=FFTW_EXHAUSTIVE
    end select
    call decoder%set_fft_flags(flags)
  end subroutine prepare_jt4_host

  subroutine load_jt4_calls(decoder)
    class(jt4_decoder), intent(inout) :: decoder
    type(jt4_call), allocatable :: calls(:)
    character(len=180) :: line
    integer :: unit,ios,count,i,first,second

    allocate(calls(6999))
    count=0
    open(newunit=unit,file=trim(data_dir)//'/CALL3.TXT',status='old',action='read',iostat=ios)
    if(ios==0) then
       do i=1,size(calls)
          read(unit,'(a80)',iostat=ios) line
          if(ios/=0) exit
          if(line(:4)=='ZZZZ') exit
          if(line(:2)=='//') cycle
          first=index(line,',')
          if(first<4) cycle
          second=index(line(first+1:),',')
          if(second<5) cycle
          count=count+1
          calls(count)%call=line(:first-1)
          calls(count)%grid=line(first+1:first+4)
       enddo
       close(unit)
    endif
    call decoder%set_calls(calls(:count))
  end subroutine load_jt4_calls

  subroutine write_jt4_averages(decoder,unit)
    class(jt4_decoder), intent(in) :: decoder
    integer, intent(in) :: unit
    type(jt4_average_entry) :: entries(64)
    integer :: count,i
    character :: used,polarity

    call decoder%get_averages(entries,count)
    rewind(unit)
    do i=1,count
       used='.'
       if(entries(i)%used) used='$'
       polarity='*'
       if(entries(i)%polarity<0) polarity='$'
       write(unit,'(a1,i5.4,f6.1,f6.2,i6,1x,a1)') used,entries(i)%utc/100, &
            entries(i)%sync,entries(i)%dt_seconds,entries(i)%frequency_hz,polarity
    enddo
    endfile(unit)
    flush(unit)
  end subroutine write_jt4_averages

  subroutine write_jt4_diagnostic(line)
    character(len=*), intent(in) :: line
    integer :: unit,ios

    open(newunit=unit,file='fort.77',status='replace',iostat=ios)
    if(ios/=0) return
    write(unit,'(a)') line
    close(unit)
  end subroutine write_jt4_diagnostic
end module jt4_host_support
