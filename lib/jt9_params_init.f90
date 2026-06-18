! Shared params init — factored from WAV-path (jt9.f90:474-554),
! stream-mode dispatch arm (jt9.f90:298-388), and streaming_io.f90:130-190.
!
! Eliminates the structural drift surface that arose from hand-copying
! a baseline-init block into streaming_io.f90. Now both the WAV-decoder and
! streaming-decoder paths route through one source of truth.
!
! Scope discipline:
!   - init_default_params sets ONLY fields the pre-refactor WAV path was
!     setting (the "common" subset). This preserves WAV byte-identical
!     behavior under run-golden-fixtures.sh — the regression gate.
!   - init_streaming_extra_fields sets the additional fields the pre-refactor
!     streaming dispatch arm (jt9.f90:298-388) set beyond the common set.
!     Streaming callers do init_default_params THEN init_streaming_extra_fields.
!   - The WAV multift8-only block at jt9.f90:519-554 stays inline (out of
!     scope). It sets some of the same fields init_streaming_extra_fields
!     sets, but with multift8-specific logic; not factored here.
!
! Public surface:
!   init_default_params(params, mode, TRperiod, args)
!     Common-subset init. Always called first.
!
!   init_streaming_extra_fields(params, args)
!     Extra fields needed by the streaming subprocess path. Streaming
!     callers call this AFTER init_default_params.
!
!   apply_per_mode_policy(params, mode, TRperiod)
!     Per-mode ntol cap + Q65 60s emedelay. Called from init_default_params,
!     and from streaming_apply's apply_configure_fields on mode/trperiod change.
!
! Per-mode invariants preserved (each documented inline at the policy site):
!   - FST4/FST4W (mode 241/242): ntol = min(ntol, 100)
!   - JT65+JT9 combined (mode 74): ntol hard = 20 unless -F supplied (have_ntol)
!   - Q65 (mode 66): ntol hard = 10 unless -F supplied; emedelay = 2.5 when TRperiod = 60
!   - All other modes: ntol = min(ntol, 1000)
!   - FST4 60s (mode 240): kin = 720000 (init-only; not in policy sub)
!   - JT9 with explicit -S<HZ> (mode 9 ∧ fsplit ≠ 2700): nfa = fsplit
!     (init-only; not in policy sub since fsplit isn't in apply_configure_fields'
!     scope today)

module jt9_params_init
  use, intrinsic :: iso_c_binding, only: c_int, c_short, c_float, c_char, c_bool

  include 'jt9com.f90'

  private
  public :: init_default_params
  public :: init_streaming_extra_fields
  public :: apply_per_mode_policy
  public :: cli_args_t

  ! CLI-arg bundle threaded through to the params init. Defaults match
  ! jt9.f90's local-var defaults (lines 30-32) so a bare call (with all
  ! args at default) produces the same shape as un-flagged jt9 invocation.
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

  ! Common-subset init. Sets the fields the pre-refactor WAV path was
  ! setting, plus the per-mode policy outcomes (ntol cap, emedelay,
  ! kin mode=240 override, nfa mode=9-fsplit override).
  !
  ! Caller-specific overrides happen AFTER the call:
  !   - WAV path: nutc (filename), ndiskdat=.true., nzhsym=nhsym (per-period),
  !     nmode=65+9 if mode=0
  !   - Streaming dispatch: ndiskdat=.false. (default; jt9_stream overrides
  !     via init_streaming_extra_fields side-effect or its own override)
  subroutine init_default_params(params, mode, TRperiod, args)
    type(params_block), intent(out) :: params
    integer,            intent(in)  :: mode
    real(8),            intent(in)  :: TRperiod
    type(cli_args_t),   intent(in)  :: args

    ! Common-subset structural defaults (preserves pre-refactor WAV scope).
    params%nutc            = 0                ! caller may overwrite (filename)
    params%ndiskdat        = .false.          ! caller may overwrite
    params%ntr             = int(TRperiod)
    params%nQSOProgress    = args%nQSOProg
    params%nfqso           = args%nrxfreq
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
    params%nmode           = mode             ! caller may overwrite (WAV mode=0 → 65+9)
    params%nclearave       = .false.
    params%emedelay        = 0.0              ! per-mode policy may override
    params%dttol           = 3.
    params%n2pass          = 2
    params%nranera         = 6
    params%naggressive     = 0
    params%nrobust         = .false.
    params%nexp_decode     = args%nexp_decode

    params%datetime        = transfer(args%datetime, params%datetime)
    params%mycall          = transfer(args%mycall,   params%mycall)
    params%mygrid          = transfer(args%mygrid,   params%mygrid)
    params%hiscall         = transfer(args%hiscall,  params%hiscall)
    params%hisgrid         = transfer(args%hisgrid,  params%hisgrid)

    params%lmultift8       = args%multift8

    ! Per-mode init-only adjustments (NOT folded into apply_per_mode_policy
    ! because they reference CLI args (fsplit) or are mode-specific to a
    ! degree the post-configure policy doesn't need to revisit).

    ! FST4 60s period uses a larger sample window (jt9.f90:485 in WAV path).
    if (mode .eq. 240) params%kin = 720000

    ! JT9 with explicit -S<HZ>: remap nfa to fsplit (jt9.f90:386, :560).
    if (mode .eq. 9 .and. args%fsplit .ne. 2700) params%nfa = args%fsplit

    ! Init-time ntol cap (per-mode). Streaming-config-time ntol policy
    ! lives in apply_configure_fields where it can branch on consumer-set vs
    ! mode-default (see streaming_apply.f90).
    if (mode .eq. 241 .or. mode .eq. 242) then
       params%ntol = min(args%ntol, 100)         ! FST4/FST4W
    else if (mode .eq. 65 + 9 .and. .not. args%have_ntol) then
       params%ntol = 20                          ! JT65+JT9 hard (unless -F given)
    else if (mode .eq. 66 .and. .not. args%have_ntol) then
       params%ntol = 10                          ! Q65 hard (unless -F given)
    else
       params%ntol = min(args%ntol, 1000)        ! FT8/FT4/etc
    end if

    ! Q65 60s emedelay (the only piece left in apply_per_mode_policy
    ! — ntol moved out).
    call apply_per_mode_policy(params, mode, TRperiod)
  end subroutine init_default_params

  ! Streaming-only extras. Mirrors fields the pre-refactor stream-dispatch
  ! arm (jt9.f90:298-388) set beyond the common set. NOT called by WAV
  ! callers — those leave these fields at their bind(C) default state
  ! (matching pre-refactor WAV behavior; the multift8-only block in WAV
  ! will set a subset of these for multift8 mode).
  subroutine init_streaming_extra_fields(params, args)
    type(params_block), intent(inout) :: params
    type(cli_args_t),   intent(in)    :: args

    ! Tx-side companion to nfqso (WAV path leaves this uninit; streaming sets it).
    params%nftx            = args%nrxfreq

    params%minw            = 1
    params%minsync         = 0
    params%nlist           = 0
    params%max_drift       = 0
    params%b_even_seq      = .false.
    params%b_superfox      = .false.
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

  ! Per-mode policy that runs both at init AND on configure-frame mode/trperiod
  ! changes. Currently only Q65 60s emedelay (ntol was moved out since the
  ! cap-vs-restore semantics differ between init and config-time).
  ! Idempotent.
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
