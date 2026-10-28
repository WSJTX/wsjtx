module jt9_params_test_types
  include 'jt9com.f90'
end module jt9_params_test_types

program test_jt9_params_init
  use jt9_params_init, only: cli_args_t, init_default_params, init_streaming_extra_fields
  use jt9_params_test_types, only: params_block
  implicit none

  type(params_block) :: params
  type(cli_args_t) :: args

  args%mycall = 'PJ4/K1ABC'
  args%hiscall = 'W9XYZ/P'
  args%mygrid = 'FN20ab'
  args%hisgrid = 'EN37'
  call init_default_params(params, 8, 15.d0, args)
  call assert_baseline(params)
  call assert_true(params%nmode == 8 .and. params%ntr == 15, 'mode and period are initialized')
  call assert_true(params%nfqso == 1500 .and. params%nfa == 200 .and. params%nfb == 4000, &
       'receive and search frequencies retain CLI defaults')
  call assert_true(logical(params%newdat .and. params%lft8apon .and. params%ljt65apon), &
       'common decode flags retain their defaults')
  call assert_true(transfer(params%mycall, args%mycall) == args%mycall .and. &
       transfer(params%hiscall, args%hiscall) == args%hiscall .and. &
       transfer(params%mygrid, args%mygrid) == args%mygrid .and. &
       transfer(params%hisgrid, args%hisgrid) == args%hisgrid, 'station identity is copied literally')

  args%nrxfreq = 1743
  args%ncycles = 2
  args%nmt = 4
  args%nft8rxfsens = 1
  args%ndecoderstart = 2
  args%hidedupes = .true.
  args%lwidedxcsearch = .false.
  call init_streaming_extra_fields(params, args)
  call assert_true(params%nftx == 1743 .and. params%minw == 1, 'streaming transmit and window defaults apply')
  call assert_true(params%nft8cycles == 2 .and. params%nmt == 4 .and. params%nft8rxfsens == 1 .and. &
       params%ndecoderstart == 2, 'streaming tuning options override baseline zeros')
  call assert_true(logical(params%lft8lowth .and. params%lft8subpass .and. params%lhideft8dupes .and. &
       params%lenabledxcsearch .and. params%lcommonft8b .and. params%lmycallstd .and. params%lhiscallstd), &
       'streaming enables its decode flags')
  call assert_true(logical(.not. params%lwidedxcsearch), 'streaming preserves the wide-search option')
  call assert_true(params%ncandthin == 100 .and. params%nprepass == 4 .and. params%nsdecatt == 1, &
       'streaming algorithm defaults override baseline zeros')

  params%max_drift = 19
  params%listutc = 123456
  params%yymmdd = 261027
  params%mybcall = 'X'
  params%hisbcall = 'Y'
  params%ntrials10 = 25
  params%ntrialsrxf10 = 30
  params%ntopfreq65 = 1234
  params%nft4depth = 3
  params%fmaskact = .true.
  params%ltxing = .true.
  params%lmultinst = .true.
  call init_default_params(params, 8, 15.d0, args)
  call assert_baseline(params)

  call init_default_params(params, 66, 60.d0, args)
  call assert_true(params%ntol == 10 .and. params%emedelay == 2.5, 'Q65 keeps its tolerance and EME delay')
  args%have_ntol = .true.
  args%ntol = 75
  call init_default_params(params, 66, 30.d0, args)
  call assert_true(params%ntol == 75 .and. params%emedelay == 0., 'explicit Q65 tolerance survives initialization')
  args%have_ntol = .false.
  args%ntol = 1000
  call init_default_params(params, 241, 120.d0, args)
  call assert_true(params%ntol == 100, 'FST4W tolerance is capped')
  call init_default_params(params, 240, 60.d0, args)
  call assert_true(params%kin == 720000, 'FST4 keeps its sample window')
  call init_default_params(params, 4, 60.d0, args)
  call assert_true(params%kin == 624000 .and. params%minw == 0 .and. params%minsync == 0, &
       'JT4 keeps its sample window and synchronization defaults')
  call init_streaming_extra_fields(params, args)
  call assert_true(params%minw == 0, 'streaming preserves the JT4A window default')
  args%nsubmode = 1
  call init_default_params(params, 4, 60.d0, args)
  call init_streaming_extra_fields(params, args)
  call assert_true(params%minw == 1, 'other streaming JT4 submodes use the streaming window default')
  args%fsplit = 1800
  call init_default_params(params, 9, 60.d0, args)
  call assert_true(params%nfa == 1800, 'JT9 uses an explicit split as its low search frequency')

contains

  subroutine assert_baseline(value)
    type(params_block), intent(in) :: value

    call assert_true(all([value%nftx, value%minw, value%minsync, value%nlist, value%max_drift, &
         value%yymmdd, value%ncandthin, value%ndtcenter, value%nft8cycles, value%ntrials10, &
         value%ntrialsrxf10, value%nharmonicsdepth, value%ntopfreq65, value%nprepass, value%nsdecatt, &
         value%nlasttx, value%ndelay, value%nmt, value%nft8rxfsens, value%nft4depth, &
         value%nsecbandchanged, value%ndecoderstart] == 0), 'unused numeric fields have a zero baseline')
    call assert_true(all(value%listutc == 0), 'unused decode-list times have a zero baseline')
    call assert_true(all(value%mybcall == ' ') .and. all(value%hisbcall == ' '), &
         'unused base calls are blank padded')
    call assert_true(.not. any(logical([value%nagainfil, value%nstophint, value%nhint, value%fmaskact, &
         value%lft8lowth, value%lft8subpass, value%ltxing, value%lhideft8dupes, value%lhound, &
         value%lcommonft8b, value%lmycallstd, value%lhiscallstd, value%lapmyc, value%lmodechanged, &
         value%lbandchanged, value%lenabledxcsearch, value%lwidedxcsearch, value%lmultinst, value%lskiptx1])), &
         'unused logical fields have a false baseline')
  end subroutine assert_baseline

  subroutine assert_true(condition, message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if (.not. condition) then
      write(*, '(A)') message
      error stop 1
    end if
  end subroutine assert_true

end program test_jt9_params_init
