module jt4_host_support
  use jt4_decode, only: jt4_decoder,jt4_call,jt4_average_entry
  use decoder_engine_types, only: engine_jt4_options
  use prog_args, only: data_dir
  use streaming_side, only: side_averages
  use fftw3, only: FFTW_ESTIMATE,FFTW_ESTIMATE_PATIENT,FFTW_MEASURE,FFTW_PATIENT,FFTW_EXHAUSTIVE
  include 'jt9com.f90'
  private
  public :: convert_jt4_host_options,prepare_jt4_host,load_jt4_calls,write_jt4_averages,write_jt4_diagnostic
  public :: emit_jt4_averages

contains

  subroutine convert_jt4_host_options(params,options,utc_is_hhmmss)
    type(params_block), intent(in) :: params
    type(engine_jt4_options), intent(out) :: options
    logical, optional, intent(in) :: utc_is_hhmmss

    options=engine_jt4_options()
    options%utc=100*params%nutc
    if(present(utc_is_hhmmss)) then
       if(utc_is_hhmmss) options%utc=params%nutc
    endif
    options%submode=params%nsubmode
    options%receive_frequency_hz=params%nfqso
    options%tolerance_hz=params%ntol
    options%depth=iand(params%ndepth,7)
    options%min_sync=params%minsync
    options%minimum_width_profile=params%minw
    options%averaging=merge(1_c_int,0_c_int,iand(params%ndepth,16)/=0)
    options%deep_search=merge(1_c_int,0_c_int,iand(params%ndepth,32)/=0)
    options%mycall=params%mycall
    options%hiscall=params%hiscall
    options%hisgrid=params%hisgrid
  end subroutine convert_jt4_host_options

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

  ! The averages event (lib/streaming_side.f90): the entries write_jt4_averages
  ! writes, each with its HHMMSS label, after the attempt labelled utc.
  subroutine emit_jt4_averages(decoder,utc)
    class(jt4_decoder), intent(in) :: decoder
    integer, intent(in) :: utc
    type(jt4_average_entry) :: entries(64)
    integer :: count

    call decoder%get_averages(entries,count)
    call side_averages('JT4',utc,.true.,count,entries(:count)%used,entries(:count)%utc, &
         entries(:count)%sync,entries(:count)%dt_seconds,entries(:count)%frequency_hz,    &
         entries(:count)%polarity)
  end subroutine emit_jt4_averages

  subroutine write_jt4_diagnostic(line)
    character(len=*), intent(in) :: line
    integer :: unit,ios

    open(newunit=unit,file='fort.77',status='replace',iostat=ios)
    if(ios/=0) return
    write(unit,'(a)') line
    close(unit)
  end subroutine write_jt4_diagnostic
end module jt4_host_support
