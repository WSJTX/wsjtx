module jt9_wav_request
  use, intrinsic :: iso_c_binding, only: c_int64_t
  use decoder_engine, only: attempt_request
  use jt9_params_init, only: cli_args_t
  implicit none
  private
  public :: build_wav_request
contains
  subroutine build_wav_request(args,mode,period,utc,stage,mtd_configured,pass_mtd,input_id,attempt,now,request)
    type(cli_args_t), intent(in) :: args
    integer, intent(in) :: mode,utc,stage,attempt,now
    real(8), intent(in) :: period
    logical, intent(in) :: mtd_configured,pass_mtd
    integer(c_int64_t), intent(in) :: input_id
    type(attempt_request), intent(out) :: request
    integer :: low,tolerance,blanker

    request=attempt_request()
    request%input_id=input_id
    request%analysis_id=input_id
    request%attempt_no=attempt
    request%mode=mode
    if(mode==242) request%mode=241
    request%phase=2
    if(mode==8.and.stage<50) request%phase=1
    request%source=1
    low=args%flow
    if(mode==9.and.args%fsplit/=2700) low=args%fsplit
    tolerance=min(args%ntol,1000)
    if(mode==241.or.mode==242) tolerance=min(args%ntol,100)
    if(mode==66.and..not.args%have_ntol) tolerance=10

    select case(mode)
    case(4)
       request%jt4%utc=100*utc
       request%jt4%submode=args%nsubmode
       request%jt4%receive_frequency_hz=args%nrxfreq
       request%jt4%tolerance_hz=tolerance
       request%jt4%depth=iand(args%ndepth,7)
       request%jt4%min_sync=0
       request%jt4%minimum_width_profile=0
       request%jt4%averaging=merge(1,0,iand(args%ndepth,16)/=0)
       request%jt4%deep_search=merge(1,0,iand(args%ndepth,32)/=0)
       request%jt4%mycall=transfer(args%mycall,request%jt4%mycall)
       request%jt4%hiscall=transfer(args%hiscall,request%jt4%hiscall)
       request%jt4%hisgrid=transfer(args%hisgrid,request%jt4%hisgrid)
    case(5)
       request%ft4%utc=utc
       request%ft4%qso_progress=args%nQSOProg
       request%ft4%receive_frequency_hz=args%nrxfreq
       request%ft4%search_low_hz=low
       request%ft4%search_high_hz=args%fhigh
       request%ft4%depth=iand(args%ndepth,7)
       request%ft4%contest=iand(args%nexp_decode,7)
       request%ft4%mycall=transfer(args%mycall,request%ft4%mycall)
       request%ft4%hiscall=transfer(args%hiscall,request%ft4%hiscall)
    case(9)
       request%jt9%utc=utc
       request%jt9%receive_frequency_hz=args%nrxfreq
       request%jt9%search_low_hz=low
       request%jt9%search_high_hz=args%fhigh
       request%jt9%tolerance_hz=tolerance
       request%jt9%depth=iand(args%ndepth,7)
       request%jt9%submode=args%nsubmode
    case(65)
       request%jt65%utc=utc
       request%jt65%qso_progress=args%nQSOProg
       request%jt65%receive_frequency_hz=args%nrxfreq
       request%jt65%search_low_hz=low
       request%jt65%search_high_hz=args%fhigh
       request%jt65%tolerance_hz=tolerance
       request%jt65%depth=iand(args%ndepth,7)
       request%jt65%submode=args%nsubmode
       request%jt65%passes=2
       request%jt65%trials=1000
       request%jt65%single_decode=merge(1,0,iand(args%nexp_decode,32)/=0)
       request%jt65%vhf=merge(1,0,iand(args%nexp_decode,64)/=0)
       request%jt65%averaging=merge(1,0,iand(args%ndepth,16)/=0)
       request%jt65%auto_clear=merge(1,0,iand(args%ndepth,128)/=0)
       request%jt65%deep_search=merge(1,0,iand(args%ndepth,32)/=0)
       request%jt65%ap_enabled=1
       request%jt65%mycall=transfer(args%mycall,request%jt65%mycall)
       request%jt65%hiscall=transfer(args%hiscall,request%jt65%hiscall)
       request%jt65%hisgrid=transfer(args%hisgrid,request%jt65%hisgrid)
    case(66)
       request%q65%utc=utc
       request%q65%period_seconds=int(period)
       request%q65%submode=args%nsubmode
       request%q65%receive_frequency_hz=args%nrxfreq
       request%q65%tolerance_hz=tolerance
       request%q65%search_low_hz=low
       request%q65%search_high_hz=min(args%fhigh,5000)
       request%q65%depth=iand(args%ndepth,3)
       request%q65%qso_progress=args%nQSOProg
       request%q65%contest=iand(args%nexp_decode,7)
       request%q65%averaging=merge(1,0,iand(args%ndepth,16)/=0)
       request%q65%auto_clear=merge(1,0,iand(args%ndepth,128)/=0)
       request%q65%single_decode=merge(1,0,iand(args%nexp_decode,32)/=0)
       request%q65%extended_eme_search=merge(1,0,period==60.d0)
       request%q65%pileup=merge(1,0,iand(args%nexp_decode,128)/=0)
       request%q65%now_seconds=now
       request%q65%mycall=transfer(args%mycall,request%q65%mycall)
       request%q65%hiscall=transfer(args%hiscall,request%q65%hiscall)
       request%q65%hisgrid=transfer(args%hisgrid,request%q65%hisgrid)
    case(240,241,242)
       request%fst4%utc=utc
       request%fst4%period_seconds=int(period)
       request%fst4%receive_frequency_hz=args%nrxfreq
       request%fst4%search_low_hz=max(0,low)
       request%fst4%search_high_hz=min(args%fhigh,5000)
       request%fst4%tolerance_hz=tolerance
       request%fst4%depth=iand(args%ndepth,3)
       request%fst4%qso_progress=args%nQSOProg
       request%fst4%single_decode=merge(1,0,iand(args%nexp_decode,32)/=0)
       blanker=args%nexp_decode/256-3
       if(blanker>=0) then
          request%fst4%blanker_percent=blanker
       else
          select case(blanker)
          case(-1); request%fst4%blanker_mode=5
          case(-2); request%fst4%blanker_mode=2
          case(-3); request%fst4%blanker_mode=1
          end select
       endif
       request%fst4%mycall=transfer(args%mycall,request%fst4%mycall)
       request%fst4%hiscall=transfer(args%hiscall,request%fst4%hiscall)
    case(8)
       request%ft8%half_symbol_stage=stage
       request%ft8%utc=utc
       request%ft8%qso_progress=args%nQSOProg
       request%ft8%receive_frequency_hz=args%nrxfreq
       request%ft8%search_low_hz=low
       request%ft8%search_high_hz=args%fhigh
       request%ft8%tolerance_hz=tolerance
       request%ft8%depth=args%ndepth
       request%ft8%ap_width_hz=75
       request%ft8%contest=args%nexp_decode
       request%ft8%trials=6
       request%ft8%ap_enabled=1
       if(mtd_configured) then
          request%ft8%candidate_thinning=100
          request%ft8%cycles=args%ncycles
          request%ft8%threads=args%nmt
          request%ft8%receive_sensitivity=args%nft8rxfsens
          request%ft8%low_threshold=merge(1,0,args%lft8lowth)
          request%ft8%subtract_pass=merge(1,0,args%lft8subpass)
          request%ft8%hide_duplicates=merge(1,0,args%hidedupes)
          request%ft8%wide_dx_search=merge(1,0,args%lwidedxcsearch)
       endif
       request%ft8%mtd=merge(1,0,pass_mtd)
       request%ft8%mycall=transfer(args%mycall,request%ft8%mycall)
       request%ft8%hiscall=transfer(args%hiscall,request%ft8%hiscall)
       request%ft8%mygrid=transfer(args%mygrid,request%ft8%mygrid)
       request%ft8%hisgrid=transfer(args%hisgrid,request%ft8%hisgrid)
    end select
  end subroutine build_wav_request
end module jt9_wav_request
