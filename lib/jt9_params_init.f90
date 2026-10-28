! Shared parameter defaults for WAV and streaming CLI decoding.

module jt9_params_init
  use, intrinsic :: iso_c_binding, only: c_int, c_short, c_float, c_char, c_bool

  include 'jt9com.f90'

  private
  public :: init_default_params
  public :: init_streaming_extra_fields
  public :: apply_per_mode_policy
  public :: cli_args_t
  public :: cli_tolerance, cli_exp_decode, normalize_station_options

  ! CLI options used to initialize decoder parameters.
  type :: cli_args_t
     integer :: flow            = 200
     integer :: fsplit          = 2700
     integer :: fhigh           = 4000
     integer :: nrxfreq         = 1500
     integer :: ndepth          = 1
     integer :: ntol            = 1000   ! caller pre-clamps; per-mode policy applies on top
     integer :: nQSOProg        = 0
     integer :: nexp_decode     = 0
     integer :: ncycles         = 3
     integer :: nft8rxfsens     = 3
     integer :: nmt             = 0
     integer :: ndecoderstart   = 3
     integer :: nsubmode        = 0
     character(len=12) :: mycall  = '            '
     character(len=12) :: hiscall = '            '
     character(len=6)  :: mygrid  = '      '
     character(len=6)  :: hisgrid = '      '
     character(len=20) :: datetime = '2026-Apr-25 00:00   '
     logical :: tx9              = .false.
     logical :: multift8         = .false.
     logical :: hidedupes        = .false.
     logical :: lft8lowth        = .true.
     logical :: lft8subpass      = .true.
     logical :: lwidedxcsearch   = .true.
     logical :: have_ntol        = .false.   ! tracks whether -F was supplied
     logical :: nexp_decode_set  = .false.   ! tracks whether --exp-decode was supplied
  end type cli_args_t

contains

  pure integer function cli_tolerance(mode, tolerance, explicitly_set) result(value)
    integer, intent(in) :: mode, tolerance
    logical, intent(in) :: explicitly_set

    if (mode == 241 .or. mode == 242) then
       value = min(tolerance, 100)
    else if (mode == 66 .and. .not. explicitly_set) then
       value = 10
    else
       value = min(tolerance, 1000)
    end if
  end function cli_tolerance

  pure integer function cli_exp_decode(mode, value, explicitly_set) result(flags)
    integer, intent(in) :: mode, value
    logical, intent(in) :: explicitly_set

    flags = value
    if (.not. explicitly_set .and. (mode == 240 .or. mode == 241 .or. mode == 242)) flags = 3 * 256
  end function cli_exp_decode

  pure subroutine normalize_station_options(mycall, hiscall, hisgrid)
    character(len=*), intent(inout) :: mycall, hiscall, hisgrid

    if (mycall == 'b') mycall = ' '
    if (hiscall == 'b') then
       hiscall = ' '
       hisgrid = ' '
    end if
  end subroutine normalize_station_options

  ! Define every parameter before callers apply WAV or streaming overrides.
  subroutine init_default_params(params, mode, TRperiod, args)
    type(params_block), intent(out) :: params
    integer,            intent(in)  :: mode
    real(8),            intent(in)  :: TRperiod
    type(cli_args_t),   intent(in)  :: args

    params%nutc            = 0                ! caller may overwrite (filename)
    params%ndiskdat        = .false.          ! caller may overwrite
    params%ntr             = int(TRperiod)
    params%nQSOProgress    = args%nQSOProg
    params%nfqso           = args%nrxfreq
    params%nftx            = 0
    params%newdat          = .true.
    params%npts8           = 74736
    params%nfa             = args%flow
    params%nfsplit         = args%fsplit
    params%nfb             = args%fhigh
    params%ntol            = args%ntol        ! per-mode policy applies below
    params%kin             = 64800            ! mode=240 override below
    params%nzhsym          = 50               ! caller may overwrite per-period
    params%nsubmode        = args%nsubmode
    params%nagain          = .false.
    params%b_even_seq      = .false.
    params%b_superfox      = .false.
    params%ndepth          = args%ndepth
    params%lft8apon        = .true.
    params%lapcqonly       = .false.
    params%ljt65apon       = .true.
    params%napwid          = 75
    if (args%tx9) then
       params%ntxmode      = 9
    else
       params%ntxmode      = 65
    end if
    params%nmode           = mode
    params%minw            = 0
    params%nclearave       = .false.
    params%minsync         = 0
    params%emedelay        = 0.0              ! per-mode policy may override
    params%dttol           = 3.
    params%nlist           = 0
    params%listutc         = 0
    params%n2pass          = 2
    params%nranera         = 6
    params%naggressive     = 0
    params%nrobust         = .false.
    params%nexp_decode     = args%nexp_decode
    params%max_drift       = 0

    params%datetime        = transfer(args%datetime, params%datetime)
    params%mycall          = transfer(args%mycall,   params%mycall)
    params%mygrid          = transfer(args%mygrid,   params%mygrid)
    params%hiscall         = transfer(args%hiscall,  params%hiscall)
    params%hisgrid         = transfer(args%hisgrid,  params%hisgrid)
    params%yymmdd          = 0
    params%mybcall         = ' '
    params%hisbcall        = ' '

    params%ncandthin       = 0
    params%ndtcenter       = 0
    params%nft8cycles      = 0
    params%ntrials10       = 0
    params%ntrialsrxf10    = 0
    params%nharmonicsdepth = 0
    params%ntopfreq65      = 0
    params%nprepass        = 0
    params%nsdecatt        = 0
    params%nlasttx         = 0
    params%ndelay          = 0
    params%nmt             = 0
    params%nft8rxfsens     = 0
    params%nft4depth       = 0
    params%nsecbandchanged = 0
    params%nagainfil       = .false.
    params%nstophint       = .false.
    params%nhint           = .false.
    params%fmaskact        = .false.
    params%lmultift8       = args%multift8
    params%lft8lowth       = .false.
    params%lft8subpass     = .false.
    params%ltxing          = .false.
    params%lhideft8dupes   = .false.
    params%lhound          = .false.
    params%lcommonft8b     = .false.
    params%lmycallstd      = .false.
    params%lhiscallstd     = .false.
    params%lapmyc          = .false.
    params%lmodechanged   = .false.
    params%lbandchanged   = .false.
    params%lenabledxcsearch= .false.
    params%lwidedxcsearch  = .false.
    params%lmultinst       = .false.
    params%lskiptx1        = .false.
    params%ndecoderstart   = 0

    ! FST4 and JT4 use larger sample windows.
    if (mode .eq. 240) params%kin = 720000
    if (mode .eq. 4) params%kin = 624000

    if (mode .eq. 9 .and. args%fsplit .ne. 2700) params%nfa = args%fsplit

    params%ntol = cli_tolerance(mode, args%ntol, args%have_ntol)

    call apply_per_mode_policy(params, mode, TRperiod)
  end subroutine init_default_params

  ! Apply streaming defaults after the common parameter initialization.
  subroutine init_streaming_extra_fields(params, args)
    type(params_block), intent(inout) :: params
    type(cli_args_t),   intent(in)    :: args

    params%nftx            = args%nrxfreq

    params%minw            = 1
    if(params%nmode==4.and.params%nsubmode==0) params%minw=0
    params%minsync         = 0
    params%nlist           = 0
    params%max_drift       = 0
    params%lft8lowth       = args%lft8lowth
    params%lft8subpass     = args%lft8subpass
    params%lwidedxcsearch  = args%lwidedxcsearch
    params%lhound          = .false.
    params%lcommonft8b     = .true.
    params%lhideft8dupes   = args%hidedupes
    params%nft8cycles      = args%ncycles
    params%nft8rxfsens     = args%nft8rxfsens
    params%nmt             = args%nmt
    params%ndecoderstart   = args%ndecoderstart
    params%ncandthin       = 100
    params%ndtcenter       = 0
    params%nharmonicsdepth = 0
    params%nprepass        = 4
    params%nsdecatt        = 1
    params%nlasttx         = 0
    params%ndelay          = 0
    params%nsecbandchanged = 0
    params%nagainfil       = .false.
    params%nstophint       = .false.
    params%nhint           = .false.
    params%fmaskact        = .false.
    params%ltxing          = .false.
    params%lmycallstd      = .true.
    params%lhiscallstd     = .true.
    params%lapmyc          = .false.
    params%lmodechanged    = .false.
    params%lbandchanged    = .false.
    params%lenabledxcsearch= .true.
    params%lmultinst       = .false.
    params%lskiptx1        = .false.
  end subroutine init_streaming_extra_fields

  ! Set the EME delay for the current mode and period.
  subroutine apply_per_mode_policy(params, mode, TRperiod)
    type(params_block), intent(inout) :: params
    integer,            intent(in)    :: mode
    real(8),            intent(in)    :: TRperiod

    ! Q65 60s mode requires 2.5s eme-delay; other modes 0.0.
    if (mode .eq. 66 .and. TRperiod .eq. 60.d0) then
       params%emedelay = 2.5
    else
       params%emedelay = 0.0
    end if
  end subroutine apply_per_mode_policy

end module jt9_params_init
