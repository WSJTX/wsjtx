program test_jt9_wav_request
  use, intrinsic :: iso_c_binding, only: c_int64_t
  use jt9_params_init, only: cli_args_t, init_default_params
  use jt9_wav_request, only: build_wav_request
  use decoder_engine, only: attempt_request, params_to_request
  include 'jt9com.f90'
  type(cli_args_t) :: args
  integer :: i, variant
  integer, parameter :: modes(*) = [4,5,9,65,66,240,241,242,8]
  args%mycall='PJ4/K1ABC'
  args%hiscall='W9XYZ/P'
  args%mygrid='EN37aa'
  args%hisgrid='FN20'
  do i=1,size(modes)
    do variant=0,1
      call compare(modes(i),60.d0,50,.false.,.false.,variant)
      call compare(modes(i),30.d0,41,.true.,.false.,variant)
      call compare(modes(i),300.d0,49,.true.,.true.,variant)
    enddo
  enddo
  call compare(8,15.d0,41,.false.,.false.,2)
  call compare(8,15.d0,47,.false.,.false.,2)
  call compare(8,15.d0,50,.false.,.false.,2)
  call compare(8,15.d0,49,.true.,.true.,2)
  call compare(9,60.d0,50,.false.,.false.,2)
  call compare(66,30.d0,50,.false.,.false.,2)
  call compare(66,60.d0,50,.false.,.false.,2)
  call compare(66,300.d0,50,.false.,.false.,2)
  call compare(240,60.d0,50,.false.,.false.,2)
  call compare(241,1800.d0,50,.false.,.false.,2)
  call compare(242,1800.d0,50,.false.,.false.,2)
  call compare(5,7.5d0,50,.false.,.false.,2)
  call compare(65,60.d0,50,.false.,.false.,2)
  call compare(4,60.d0,50,.false.,.false.,2)
contains
  subroutine compare(mode,period,stage,configured,mtd,variant)
    integer, intent(in) :: mode,stage,variant
    real(8), intent(in) :: period
    logical, intent(in) :: configured,mtd
    type(params_block) :: params
    type(attempt_request) :: actual,expected
    type(cli_args_t) :: options
    options=args
    options%ntol=20
    if(mode==240.or.mode==241.or.mode==242) options%nexp_decode=768
    if(variant==1) then
      options%flow=300
      options%fsplit=1800
      options%fhigh=5200
      options%nrxfreq=1550
      options%ndepth=51
      options%nsubmode=1
      options%ntol=75
      options%have_ntol=.true.
      options%nexp_decode=800
    else if(variant==2) then
      select case(mode)
      case(4)
        options%ndepth=3
        options%nsubmode=5
        options%nrxfreq=1200
      case(5)
        options%ndepth=3
      case(65)
        options%ndepth=3
        options%nsubmode=1
        options%nrxfreq=1700
      case(66)
        options%ndepth=3
        options%nrxfreq=1000
        if(period==60.d0) options%nsubmode=3
      case(240)
        options%ndepth=3
        options%nrxfreq=1331
        options%flow=1000
        options%fhigh=1400
      case(241,242)
        options%ndepth=3
        options%nrxfreq=1433
        options%ntol=100
        options%have_ntol=.true.
      case(8)
        if(configured) then
          options%ncycles=3
          options%nmt=4
          options%mycall='K1JT'
          options%hiscall='HA0DU'
          options%nrxfreq=590
          options%nQSOProg=1
        endif
      end select
    endif
    options%multift8=configured
    options%lft8lowth=.true.
    options%lft8subpass=.false.
    options%hidedupes=.true.
    call init_default_params(params,mode,period,options)
    params%nutc=1234
    params%ndiskdat=.true.
    params%nzhsym=stage
    if(configured.and.mode==8) then
      params%lmultift8=mtd
      params%lft8lowth=options%lft8lowth
      params%lft8subpass=options%lft8subpass
      params%lhideft8dupes=options%hidedupes
      params%nft8cycles=options%ncycles
      params%nmt=options%nmt
      params%lwidedxcsearch=options%lwidedxcsearch
      params%ncandthin=100
      params%nft8rxfsens=options%nft8rxfsens
    endif
    expected=attempt_request()
    expected%mode=mode
    if(mode==242) expected%mode=241
    call params_to_request(params,expected,12345)
    call build_wav_request(options,mode,period,1234,stage,mtd,1_c_int64_t,1,12345,actual)
    call check(actual%input_id==1_c_int64_t.and.actual%analysis_id==1_c_int64_t, 'input identity')
    call check(actual%attempt_no==1.and.actual%source==1, 'attempt and source')
    call check(actual%mode==expected%mode, 'normalized mode')
    if(mode==8.and.stage<50) then
      call check(actual%phase==1, 'early phase')
    else
      call check(actual%phase==2, 'final phase')
    endif
    select case(mode)
    case (4)
      call check(actual%jt4%utc == expected%jt4%utc, 'jt4%utc')
      call check(actual%jt4%submode == expected%jt4%submode, 'jt4%submode')
      call check(actual%jt4%receive_frequency_hz == expected%jt4%receive_frequency_hz, 'jt4%receive_frequency_hz')
      call check(actual%jt4%tolerance_hz == expected%jt4%tolerance_hz, 'jt4%tolerance_hz')
      call check(actual%jt4%depth == expected%jt4%depth, 'jt4%depth')
      call check(actual%jt4%min_sync == expected%jt4%min_sync, 'jt4%min_sync')
      call check(actual%jt4%minimum_width_profile == expected%jt4%minimum_width_profile, 'jt4%minimum_width_profile')
      call check(actual%jt4%averaging == expected%jt4%averaging, 'jt4%averaging')
      call check(actual%jt4%deep_search == expected%jt4%deep_search, 'jt4%deep_search')
      call check(all(actual%jt4%mycall == expected%jt4%mycall), 'jt4%mycall')
      call check(all(actual%jt4%hiscall == expected%jt4%hiscall), 'jt4%hiscall')
      call check(all(actual%jt4%hisgrid == expected%jt4%hisgrid), 'jt4%hisgrid')
    case (5)
      call check(actual%ft4%utc == expected%ft4%utc, 'ft4%utc')
      call check(actual%ft4%qso_progress == expected%ft4%qso_progress, 'ft4%qso_progress')
      call check(actual%ft4%receive_frequency_hz == expected%ft4%receive_frequency_hz, 'ft4%receive_frequency_hz')
      call check(actual%ft4%search_low_hz == expected%ft4%search_low_hz, 'ft4%search_low_hz')
      call check(actual%ft4%search_high_hz == expected%ft4%search_high_hz, 'ft4%search_high_hz')
      call check(actual%ft4%depth == expected%ft4%depth, 'ft4%depth')
      call check(actual%ft4%cq_only == expected%ft4%cq_only, 'ft4%cq_only')
      call check(actual%ft4%contest == expected%ft4%contest, 'ft4%contest')
      call check(all(actual%ft4%mycall == expected%ft4%mycall), 'ft4%mycall')
      call check(all(actual%ft4%hiscall == expected%ft4%hiscall), 'ft4%hiscall')
    case (9)
      call check(actual%jt9%utc == expected%jt9%utc, 'jt9%utc')
      call check(actual%jt9%receive_frequency_hz == expected%jt9%receive_frequency_hz, 'jt9%receive_frequency_hz')
      call check(actual%jt9%search_low_hz == expected%jt9%search_low_hz, 'jt9%search_low_hz')
      call check(actual%jt9%search_high_hz == expected%jt9%search_high_hz, 'jt9%search_high_hz')
      call check(actual%jt9%tolerance_hz == expected%jt9%tolerance_hz, 'jt9%tolerance_hz')
      call check(actual%jt9%depth == expected%jt9%depth, 'jt9%depth')
      call check(actual%jt9%submode == expected%jt9%submode, 'jt9%submode')
    case (65)
      call check(actual%jt65%utc == expected%jt65%utc, 'jt65%utc')
      call check(actual%jt65%qso_progress == expected%jt65%qso_progress, 'jt65%qso_progress')
      call check(actual%jt65%receive_frequency_hz == expected%jt65%receive_frequency_hz, 'jt65%receive_frequency_hz')
      call check(actual%jt65%search_low_hz == expected%jt65%search_low_hz, 'jt65%search_low_hz')
      call check(actual%jt65%search_high_hz == expected%jt65%search_high_hz, 'jt65%search_high_hz')
      call check(actual%jt65%tolerance_hz == expected%jt65%tolerance_hz, 'jt65%tolerance_hz')
      call check(actual%jt65%depth == expected%jt65%depth, 'jt65%depth')
      call check(actual%jt65%submode == expected%jt65%submode, 'jt65%submode')
      call check(actual%jt65%min_sync == expected%jt65%min_sync, 'jt65%min_sync')
      call check(actual%jt65%passes == expected%jt65%passes, 'jt65%passes')
      call check(actual%jt65%trials == expected%jt65%trials, 'jt65%trials')
      call check(actual%jt65%aggressiveness == expected%jt65%aggressiveness, 'jt65%aggressiveness')
      call check(actual%jt65%single_decode == expected%jt65%single_decode, 'jt65%single_decode')
      call check(actual%jt65%vhf == expected%jt65%vhf, 'jt65%vhf')
      call check(actual%jt65%averaging == expected%jt65%averaging, 'jt65%averaging')
      call check(actual%jt65%auto_clear == expected%jt65%auto_clear, 'jt65%auto_clear')
      call check(actual%jt65%deep_search == expected%jt65%deep_search, 'jt65%deep_search')
      call check(actual%jt65%ap_enabled == expected%jt65%ap_enabled, 'jt65%ap_enabled')
      call check(all(actual%jt65%mycall == expected%jt65%mycall), 'jt65%mycall')
      call check(all(actual%jt65%hiscall == expected%jt65%hiscall), 'jt65%hiscall')
      call check(all(actual%jt65%hisgrid == expected%jt65%hisgrid), 'jt65%hisgrid')
    case (66)
      call check(actual%q65%utc == expected%q65%utc, 'q65%utc')
      call check(actual%q65%period_seconds == expected%q65%period_seconds, 'q65%period_seconds')
      call check(actual%q65%submode == expected%q65%submode, 'q65%submode')
      call check(actual%q65%receive_frequency_hz == expected%q65%receive_frequency_hz, 'q65%receive_frequency_hz')
      call check(actual%q65%tolerance_hz == expected%q65%tolerance_hz, 'q65%tolerance_hz')
      call check(actual%q65%search_low_hz == expected%q65%search_low_hz, 'q65%search_low_hz')
      call check(actual%q65%search_high_hz == expected%q65%search_high_hz, 'q65%search_high_hz')
      call check(actual%q65%depth == expected%q65%depth, 'q65%depth')
      call check(actual%q65%max_drift_symbol_rates == expected%q65%max_drift_symbol_rates, 'q65%max_drift_symbol_rates')
      call check(actual%q65%qso_progress == expected%q65%qso_progress, 'q65%qso_progress')
      call check(actual%q65%contest == expected%q65%contest, 'q65%contest')
      call check(actual%q65%averaging == expected%q65%averaging, 'q65%averaging')
      call check(actual%q65%auto_clear == expected%q65%auto_clear, 'q65%auto_clear')
      call check(actual%q65%single_decode == expected%q65%single_decode, 'q65%single_decode')
      call check(actual%q65%extended_eme_search == expected%q65%extended_eme_search, 'q65%extended_eme_search')
      call check(actual%q65%pileup == expected%q65%pileup, 'q65%pileup')
      call check(actual%q65%ap_cq_only == expected%q65%ap_cq_only, 'q65%ap_cq_only')
      call check(actual%q65%now_seconds == expected%q65%now_seconds, 'q65%now_seconds')
      call check(all(actual%q65%mycall == expected%q65%mycall), 'q65%mycall')
      call check(all(actual%q65%hiscall == expected%q65%hiscall), 'q65%hiscall')
      call check(all(actual%q65%hisgrid == expected%q65%hisgrid), 'q65%hisgrid')
    case (240,241,242)
      call check(actual%fst4%utc == expected%fst4%utc, 'fst4%utc')
      call check(actual%fst4%period_seconds == expected%fst4%period_seconds, 'fst4%period_seconds')
      call check(actual%fst4%receive_frequency_hz == expected%fst4%receive_frequency_hz, 'fst4%receive_frequency_hz')
      call check(actual%fst4%search_low_hz == expected%fst4%search_low_hz, 'fst4%search_low_hz')
      call check(actual%fst4%search_high_hz == expected%fst4%search_high_hz, 'fst4%search_high_hz')
      call check(actual%fst4%tolerance_hz == expected%fst4%tolerance_hz, 'fst4%tolerance_hz')
      call check(actual%fst4%depth == expected%fst4%depth, 'fst4%depth')
      call check(actual%fst4%qso_progress == expected%fst4%qso_progress, 'fst4%qso_progress')
      call check(actual%fst4%single_decode == expected%fst4%single_decode, 'fst4%single_decode')
      call check(actual%fst4%ap_cq_only == expected%fst4%ap_cq_only, 'fst4%ap_cq_only')
      call check(actual%fst4%blanker_percent == expected%fst4%blanker_percent, 'fst4%blanker_percent')
      call check(actual%fst4%blanker_mode == expected%fst4%blanker_mode, 'fst4%blanker_mode')
      call check(actual%fst4%eme_delay_seconds == expected%fst4%eme_delay_seconds, 'fst4%eme_delay_seconds')
      call check(all(actual%fst4%mycall == expected%fst4%mycall), 'fst4%mycall')
      call check(all(actual%fst4%hiscall == expected%fst4%hiscall), 'fst4%hiscall')
    case (8)
      call check(actual%ft8%reuse_spectrum == expected%ft8%reuse_spectrum, 'ft8%reuse_spectrum')
      call check(actual%ft8%half_symbol_stage == expected%ft8%half_symbol_stage, 'ft8%half_symbol_stage')
      call check(actual%ft8%utc == expected%ft8%utc, 'ft8%utc')
      call check(actual%ft8%qso_progress == expected%ft8%qso_progress, 'ft8%qso_progress')
      call check(actual%ft8%receive_frequency_hz == expected%ft8%receive_frequency_hz, 'ft8%receive_frequency_hz')
      call check(actual%ft8%transmit_frequency_hz == expected%ft8%transmit_frequency_hz, 'ft8%transmit_frequency_hz')
      call check(actual%ft8%search_low_hz == expected%ft8%search_low_hz, 'ft8%search_low_hz')
      call check(actual%ft8%search_high_hz == expected%ft8%search_high_hz, 'ft8%search_high_hz')
      call check(actual%ft8%tolerance_hz == expected%ft8%tolerance_hz, 'ft8%tolerance_hz')
      call check(actual%ft8%depth == expected%ft8%depth, 'ft8%depth')
      call check(actual%ft8%ap_width_hz == expected%ft8%ap_width_hz, 'ft8%ap_width_hz')
      call check(actual%ft8%contest == expected%ft8%contest, 'ft8%contest')
      call check(actual%ft8%candidate_thinning == expected%ft8%candidate_thinning, 'ft8%candidate_thinning')
      call check(actual%ft8%time_center == expected%ft8%time_center, 'ft8%time_center')
      call check(actual%ft8%cycles == expected%ft8%cycles, 'ft8%cycles')
      call check(actual%ft8%trials == expected%ft8%trials, 'ft8%trials')
      call check(actual%ft8%last_transmit == expected%ft8%last_transmit, 'ft8%last_transmit')
      call check(actual%ft8%delay == expected%ft8%delay, 'ft8%delay')
      call check(actual%ft8%threads == expected%ft8%threads, 'ft8%threads')
      call check(actual%ft8%receive_sensitivity == expected%ft8%receive_sensitivity, 'ft8%receive_sensitivity')
      call check(actual%ft8%discard_leading_seconds == expected%ft8%discard_leading_seconds, 'ft8%discard_leading_seconds')
      call check(actual%ft8%date == expected%ft8%date, 'ft8%date')
      call check(actual%ft8%ap_enabled == expected%ft8%ap_enabled, 'ft8%ap_enabled')
      call check(actual%ft8%cq_only == expected%ft8%cq_only, 'ft8%cq_only')
      call check(actual%ft8%even_sequence == expected%ft8%even_sequence, 'ft8%even_sequence')
      call check(actual%ft8%superfox == expected%ft8%superfox, 'ft8%superfox')
      call check(actual%ft8%filtered_retry == expected%ft8%filtered_retry, 'ft8%filtered_retry')
      call check(actual%ft8%stop_hint == expected%ft8%stop_hint, 'ft8%stop_hint')
      call check(actual%ft8%mtd == expected%ft8%mtd, 'ft8%mtd')
      call check(actual%ft8%low_threshold == expected%ft8%low_threshold, 'ft8%low_threshold')
      call check(actual%ft8%subtract_pass == expected%ft8%subtract_pass, 'ft8%subtract_pass')
      call check(actual%ft8%transmitting == expected%ft8%transmitting, 'ft8%transmitting')
      call check(actual%ft8%hide_duplicates == expected%ft8%hide_duplicates, 'ft8%hide_duplicates')
      call check(actual%ft8%hound == expected%ft8%hound, 'ft8%hound')
      call check(actual%ft8%standard_mycall == expected%ft8%standard_mycall, 'ft8%standard_mycall')
      call check(actual%ft8%standard_hiscall == expected%ft8%standard_hiscall, 'ft8%standard_hiscall')
      call check(actual%ft8%ap_mycall == expected%ft8%ap_mycall, 'ft8%ap_mycall')
      call check(actual%ft8%mode_changed == expected%ft8%mode_changed, 'ft8%mode_changed')
      call check(actual%ft8%band_changed == expected%ft8%band_changed, 'ft8%band_changed')
      call check(actual%ft8%dx_search == expected%ft8%dx_search, 'ft8%dx_search')
      call check(actual%ft8%wide_dx_search == expected%ft8%wide_dx_search, 'ft8%wide_dx_search')
      call check(actual%ft8%multiple_instances == expected%ft8%multiple_instances, 'ft8%multiple_instances')
      call check(actual%ft8%skip_first_message == expected%ft8%skip_first_message, 'ft8%skip_first_message')
      call check(actual%ft8%eme_delay_seconds == expected%ft8%eme_delay_seconds, 'ft8%eme_delay_seconds')
      call check(all(actual%ft8%mycall == expected%ft8%mycall), 'ft8%mycall')
      call check(all(actual%ft8%my_base_call == expected%ft8%my_base_call), 'ft8%my_base_call')
      call check(all(actual%ft8%hiscall == expected%ft8%hiscall), 'ft8%hiscall')
      call check(all(actual%ft8%his_base_call == expected%ft8%his_base_call), 'ft8%his_base_call')
      call check(all(actual%ft8%mygrid == expected%ft8%mygrid), 'ft8%mygrid')
      call check(all(actual%ft8%hisgrid == expected%ft8%hisgrid), 'ft8%hisgrid')
    end select
  end subroutine
  subroutine check(condition,name)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: name
    if(.not.condition) then
      write(*,*) 'Mismatch: ',name
      stop 1
    endif
  end subroutine
end program
