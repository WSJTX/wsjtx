module npar_ptrs_mod
  use iso_c_binding
  implicit none

  type :: decode_parameters
    real(c_double) :: fcenter = 0
    real(c_double) :: ftol_nb = 0
    integer(c_int) :: nfa = 0
    integer(c_int) :: nfb = 0
    integer(c_int) :: nfshift = 0
    integer(c_int) :: nfcal = 0
    integer(c_int) :: nutc = 0
    integer(c_int) :: idphi = 0
    integer(c_int) :: mousedf = 0
    integer(c_int) :: mousefqso = 0
    integer(c_int) :: nagain = 0
    integer(c_int) :: ndepth = 0
    integer(c_int) :: ndiskdat = 0
    integer(c_int) :: neme = 0
    integer(c_int) :: newdat = 0
    integer(c_int) :: map65RxLog = 0
    integer(c_int) :: mcall3 = 0
    integer(c_int) :: ntimeout = 0
    integer(c_int) :: ntol = 0
    integer(c_int) :: nxant = 0
    integer(c_int) :: nfsample = 0
    integer(c_int) :: nxpol = 0
    integer(c_int) :: nmode = 0
    integer(c_int) :: nsave = 0
    integer(c_int) :: max_drift = 0
    integer(c_int) :: nhsym = 0
    integer(c_int) :: ndop00 = 0
    integer(c_int) :: nrxlog = 0
    integer(c_int) :: nkeep = 0
    integer(c_int) :: manualDecodeFlag = 0
    character(len=12) :: mycall = ''
    character(len=12) :: hiscall = ''
    character(len=6) :: mygrid = ''
    character(len=6) :: hisgrid = ''
    character(len=20) :: datetime = ''
  end type

  ! ---------------------------------------------------------------------------
  ! Decode parameters: draft vs. active
  !
  ! Two copies of decode_parameters exist, and which one you touch matters.
  !
  !   draft   Owned by the GUI thread. Every set_xxx()/get_xxx() bind(C)
  !           routine in this module reads or writes draft, under
  !           lock_decode_requests(). It holds the *pending* GUI state, not
  !           necessarily what the decoder is using right now.
  !
  !   active  Owned by the decode thread. The module pointers below (nkeep,
  !           ntimeout, mycall, ...) alias fields of active, so Fortran decode
  !           code that says "ntimeout" is reading active%ntimeout.
  !
  ! Flow:
  !   MainWindow::decode() -> set_xxx() writes draft
  !   publish_decode_request() copies draft into a queued request (forcing
  !                            newdat = 1 in that copy) and clears
  !                            draft%manualDecodeFlag
  !   claim_decode_request()   copies the oldest request into active; an
  !                            automatic request is marked expired, and run_m65
  !                            skips it (<DecodeSkipped>), when the data
  !                            snapshot it was published with has been
  !                            overwritten or never filled (or, for requests
  !                            without a snapshot, when its input generation
  !                            is out of date). A request whose snapshot is
  !                            intact still runs after the minute rolls over.
  !   initialize_decode_parameters() copies draft into active once, at startup
  !   The decoder then sees a consistent snapshot for the whole pass, even if
  !   the GUI changes draft meanwhile.
  !
  ! Why a lock alone is not enough: a decode pass runs for many seconds and
  ! reads its parameters throughout. The lock only protects the instant of a
  ! write, so if setters wrote the decoder's own variables directly, a GUI
  ! change mid-pass would still alter values partway through the decode. The
  ! draft/active copy is what freezes the parameters for a pass and lets
  ! requests queue; the lock keeps publish_decode_request from copying a
  ! half-written draft.
  !
  ! Rules:
  !   * C++ callers: a get_xxx() returns draft, i.e. the pending GUI value,
  !     not the value the running decode is using.
  !   * A parameter only reaches the decoder if the GUI writes it to draft
  !     before a request is published. Setting it once and never re-pushing
  !     works only if nothing else overwrites it (decode() currently re-pushes
  !     most values every cycle).
  !   * Fortran code may write active fields only for state it owns (e.g.
  !     manualDecodeFlag = 0, nkeep = ...). Such writes do NOT flow back to
  !     draft, so the next published request will carry draft's old value.
  !     If a value must persist across requests, change it in draft too.
  !   * Never add a second variable for the same setting (cf. the old
  !     map65RxLog vs. nrxlog bug, where the setter wrote one variable and the
  !     decoder read the other).
  ! ---------------------------------------------------------------------------
  type(decode_parameters), target, save :: active
  type(decode_parameters), save :: draft
  type :: decode_request
    type(decode_parameters) :: parameters
    integer(c_int64_t) :: id
    integer(c_int64_t) :: input_generation
    integer(c_int) :: snap_idx = 0            ! snapshot slot (1 = nhsym1, 2 = nhsym2); 0 = none
    integer(c_int64_t) :: snap_seq = 0        ! fill number of that slot when published
    type(decode_request), pointer :: next => null()
  end type
  type(decode_request), pointer, save :: pending_head => null(), pending_tail => null()
  integer(c_int64_t), save :: published_request_id = 0
  integer(c_int64_t), save :: active_request_id = 0
  integer(c_int64_t), save :: live_input_generation = 0
  integer(c_int64_t), save :: active_input_generation = 0
  logical, save :: active_request_expired = .false.

  ! Decoder data snapshots (see datcom_ptrs_mod). All of this state is
  ! protected by lock_decode_requests().
  !   snap_seq(i)       fill number, bumped each time slot i is (re)filled
  !   snap_valid_seq(i) fill number whose contents are intact; 0 while a fill
  !                     is in progress or after a fill was skipped
  !   snap_busy(i)      slot i is being read by the running decode pass; a fill
  !                     is skipped rather than overwrite it
  integer(c_int64_t), save :: snap_seq(2) = 0
  integer(c_int64_t), save :: snap_valid_seq(2) = 0
  logical, save :: snap_busy(2) = .false.
  integer(c_int), save :: snap_latest_idx = 0   ! most recently completed fill
  integer(c_int), save :: active_snap_idx = 0   ! slot read by the active pass

  real(c_double), pointer :: fcenter => active%fcenter
  real(c_double), pointer :: ftol_nb => active%ftol_nb
  integer(c_int), pointer :: nfa => active%nfa
  integer(c_int), pointer :: nfb => active%nfb
  integer(c_int), pointer :: nfshift => active%nfshift
  integer(c_int), pointer :: nfcal => active%nfcal
  integer(c_int), pointer :: nutc => active%nutc
  integer(c_int), pointer :: idphi => active%idphi
  integer(c_int), pointer :: mousedf => active%mousedf
  integer(c_int), pointer :: mousefqso => active%mousefqso
  integer(c_int), pointer :: nagain => active%nagain
  integer(c_int), pointer :: ndepth => active%ndepth
  integer(c_int), pointer :: ndiskdat => active%ndiskdat
  integer(c_int), pointer :: neme => active%neme
  integer(c_int), pointer :: newdat => active%newdat
  integer(c_int), pointer :: map65RxLog => active%map65RxLog
  integer(c_int), pointer :: mcall3 => active%mcall3
  integer(c_int), pointer :: ntimeout => active%ntimeout
  integer(c_int), pointer :: ntol => active%ntol
  integer(c_int), pointer :: nxant => active%nxant
  integer(c_int), pointer :: nfsample => active%nfsample
  integer(c_int), pointer :: nxpol => active%nxpol
  integer(c_int), pointer :: nmode => active%nmode
  integer(c_int), pointer :: nsave => active%nsave
  integer(c_int), pointer :: max_drift => active%max_drift
  integer(c_int), pointer :: nhsym => active%nhsym
  integer(c_int), pointer :: ndop00 => active%ndop00
  integer(c_int), pointer :: nrxlog => active%nrxlog
  integer(c_int), pointer :: nkeep => active%nkeep
  integer(c_int), pointer :: manualDecodeFlag => active%manualDecodeFlag
  character(len=12), pointer :: mycall => active%mycall
  character(len=12), pointer :: hiscall => active%hiscall
  character(len=6), pointer :: mygrid => active%mygrid
  character(len=6), pointer :: hisgrid => active%hisgrid
  character(len=20), pointer :: datetime => active%datetime
  integer(c_int) :: stop_m65 = 0
  integer(c_int) :: decoder_ready = 0
  character(len=512) :: wsjtx_dir = ''

  integer(c_int) :: nrate_active    = 96000
  integer(c_int) :: nfft_active     = 32768
  integer(c_int) :: nsmax_active    = 60*96000
  integer(c_int) :: nfft_big_active = 56*96000
  integer(c_int) :: t_start = -1
  logical(c_bool) :: abort_decode = .false.

  interface
    subroutine lock_decode_requests() bind(C)
    end subroutine
    subroutine unlock_decode_requests() bind(C)
    end subroutine
  end interface

contains

  subroutine set_manual_decode_flag(val) bind(C, name="set_manual_decode_flag")
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%manualDecodeFlag = val
    call unlock_decode_requests()
  end subroutine

  function get_manual_decode_flag() bind(C, name="get_manual_decode_flag")
      use iso_c_binding
      integer(c_int) :: get_manual_decode_flag
    call lock_decode_requests()
      get_manual_decode_flag = draft%manualDecodeFlag
    call unlock_decode_requests()
  end function
  
  subroutine set_ftol_nb(val) bind(C, name="set_ftol_nb")
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%ftol_nb = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_runtime_params(rate_hz, nfft, nfft_big) bind(C, name="set_runtime_params_")
    use iso_c_binding
    implicit none
    integer(c_int), value :: rate_hz, nfft, nfft_big

    nrate_active    = rate_hz
    nfft_active     = nfft
    nsmax_active    = 60*rate_hz
    nfft_big_active = nfft_big
  end subroutine set_runtime_params

  subroutine set_wsjtx_dir(path, path_len) bind(C, name="set_wsjtx_dir_")
    use iso_c_binding
    implicit none
    character(kind=c_char), dimension(*), intent(in) :: path
    integer(c_int), value :: path_len
    integer :: n

    wsjtx_dir = ''
    if (path_len > 0) then
       n = min(path_len, len(wsjtx_dir))
       wsjtx_dir(1:n) = transfer(path(1:n), wsjtx_dir(1:n))
    end if
  end subroutine set_wsjtx_dir


  subroutine set_stop_m65(val) bind(C, name="set_stop_m65")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    stop_m65 = 0
    if (val /= 0) then
      stop_m65 = 1
      call clear_pending_requests()
    endif
    call unlock_decode_requests()
  end subroutine

  subroutine set_decoder_ready(val) bind(C, name="set_decoder_ready")
    use iso_c_binding
    integer(c_int), value :: val    
    call lock_decode_requests()
    decoder_ready = 0
    if (val /= 0) decoder_ready = 1
    call unlock_decode_requests()
  end subroutine
    
  subroutine get_mycall(buf) bind(C, name="get_mycall")
    character(kind=c_char), intent(out) :: buf(12)
    integer :: i

    call lock_decode_requests()
    do i = 1, 12
      buf(i) = draft%mycall(i:i)
    end do
    call unlock_decode_requests()
  end subroutine get_mycall

  subroutine get_hiscall(buf) bind(C, name="get_hiscall")
    character(kind=c_char), intent(out) :: buf(12)
    integer :: i
    call lock_decode_requests()
    do i = 1, 12
      buf(i) = draft%hiscall(i:i)
    end do
    call unlock_decode_requests()
  end subroutine

  subroutine get_mygrid(buf) bind(C, name="get_mygrid")
    character(kind=c_char), intent(out) :: buf(6)
    integer :: i
    call lock_decode_requests()
    do i = 1, 6
      buf(i) = draft%mygrid(i:i)
    end do
    call unlock_decode_requests()
  end subroutine

  subroutine get_hisgrid(buf) bind(C, name="get_hisgrid")
    character(kind=c_char), intent(out) :: buf(6)
    integer :: i
    call lock_decode_requests()
    do i = 1, 6
      buf(i) = draft%hisgrid(i:i)
    end do
    call unlock_decode_requests()
  end subroutine

! NOTE ABOUT DATETIME LENGTH (17 vs 20)
!
! datetime is declared as CHARACTER(len=20) for legacy compatibility with the
! old COMMON block, but only the first 17 characters are meaningful. The decoder
! in m65_mod writes:
!
!     write(..., '( "UTC Date: ", a17 )') datetime(1:17)
!
! so the timestamp format is always 17 characters (YYYY-MM-DD HH:MM). The extra
! 3 characters are unused padding. The C interface therefore exchanges only the
! first 17 bytes, which is intentional and correct.
!
! If the timestamp format changes in the future, this interface boundary should
! be updated accordingly.

  subroutine get_datetime(buf) bind(C, name="get_datetime")
    character(kind=c_char), intent(out) :: buf(17)
    integer :: i
    call lock_decode_requests()
    do i = 1, 17
      buf(i) = draft%datetime(i:i)
    end do
    call unlock_decode_requests()
  end subroutine

  ! Helper subroutine to copy fixed-length Fortran string to C buffer
  subroutine move_chars(dest, dest_len, src)
    integer(c_int), intent(in) :: dest_len
    character(kind=c_char), intent(out) :: dest(dest_len)
    character(len=*), intent(in) :: src
    integer :: i, n

    n = min(len(src), dest_len)
    do i = 1, n
      dest(i) = src(i:i)
    end do
  end subroutine move_chars
   
  function get_fcenter() bind(C, name="get_fcenter")
    real(c_double) :: get_fcenter
    call lock_decode_requests()
    get_fcenter = draft%fcenter
    call unlock_decode_requests()
  end function
  
  function get_nkeep() bind(C, name="get_nkeep")
    integer(c_int) :: get_nkeep
    call lock_decode_requests()
    get_nkeep = draft%nkeep
    call unlock_decode_requests()
  end function
  
  function get_ndop00() bind(C, name="get_ndop00")
    integer(c_int) :: get_ndop00
    call lock_decode_requests()
    get_ndop00 = draft%ndop00
    call unlock_decode_requests()
  end function
  
  function get_map65RxLog() bind(C, name="get_map65RxLog")
    integer(c_int) :: get_map65RxLog
    call lock_decode_requests()
    get_map65RxLog = draft%map65RxLog
    call unlock_decode_requests()
  end function
  
  function get_nutc() bind(C, name="get_nutc")
    integer(c_int) :: get_nutc
    call lock_decode_requests()
    get_nutc = draft%nutc
    call unlock_decode_requests()
  end function

  function get_idphi() bind(C, name="get_idphi")
    integer(c_int) :: get_idphi
    call lock_decode_requests()
    get_idphi = draft%idphi
    call unlock_decode_requests()
  end function

  function get_mousedf() bind(C, name="get_mousedf")
    integer(c_int) :: get_mousedf
    call lock_decode_requests()
    get_mousedf = draft%mousedf
    call unlock_decode_requests()
  end function

  function get_mousefqso() bind(C, name="get_mousefqso")
    integer(c_int) :: get_mousefqso
    call lock_decode_requests()
    get_mousefqso = draft%mousefqso
    call unlock_decode_requests()
  end function

  function get_nagain() bind(C, name="get_nagain")
    integer(c_int) :: get_nagain
    call lock_decode_requests()
    get_nagain = draft%nagain
    call unlock_decode_requests()
  end function

  function get_ndepth() bind(C, name="get_ndepth")
    integer(c_int) :: get_ndepth
    call lock_decode_requests()
    get_ndepth = draft%ndepth
    call unlock_decode_requests()
  end function

  function get_ndiskdat() bind(C, name="get_ndiskdat")
    integer(c_int) :: get_ndiskdat
    call lock_decode_requests()
    get_ndiskdat = draft%ndiskdat
    call unlock_decode_requests()
  end function

  function get_neme() bind(C, name="get_neme")
    integer(c_int) :: get_neme
    call lock_decode_requests()
    get_neme = draft%neme
    call unlock_decode_requests()
  end function

  function get_newdat() bind(C, name="get_newdat")
    integer(c_int) :: get_newdat
    call lock_decode_requests()
    get_newdat = draft%newdat
    call unlock_decode_requests()
  end function

  function get_nfa() bind(C, name="get_nfa")
    integer(c_int) :: get_nfa
    call lock_decode_requests()
    get_nfa = draft%nfa
    call unlock_decode_requests()
  end function

  function get_nfb() bind(C, name="get_nfb")
    integer(c_int) :: get_nfb
    call lock_decode_requests()
    get_nfb = draft%nfb
    call unlock_decode_requests()
  end function

  function get_nfcal() bind(C, name="get_nfcal")
    integer(c_int) :: get_nfcal
    call lock_decode_requests()
    get_nfcal = draft%nfcal
    call unlock_decode_requests()
  end function

  function get_nfshift() bind(C, name="get_nfshift")
    integer(c_int) :: get_nfshift
    call lock_decode_requests()
    get_nfshift = draft%nfshift
    call unlock_decode_requests()
  end function

  function get_mcall3() bind(C, name="get_mcall3")
    integer(c_int) :: get_mcall3
    call lock_decode_requests()
    get_mcall3 = draft%mcall3
    call unlock_decode_requests()
  end function

  function get_ntimeout() bind(C, name="get_ntimeout")
    integer(c_int) :: get_ntimeout
    call lock_decode_requests()
    get_ntimeout = draft%ntimeout
    call unlock_decode_requests()
  end function

  function get_ntol() bind(C, name="get_ntol")
    integer(c_int) :: get_ntol
    call lock_decode_requests()
    get_ntol = draft%ntol
    call unlock_decode_requests()
  end function

  function get_nxant() bind(C, name="get_nxant")
    integer(c_int) :: get_nxant
    call lock_decode_requests()
    get_nxant = draft%nxant
    call unlock_decode_requests()
  end function

  function get_nfsample() bind(C, name="get_nfsample")
    integer(c_int) :: get_nfsample
    call lock_decode_requests()
    get_nfsample = draft%nfsample
    call unlock_decode_requests()
  end function

  function get_nxpol() bind(C, name="get_nxpol")
    integer(c_int) :: get_nxpol
    call lock_decode_requests()
    get_nxpol = draft%nxpol
    call unlock_decode_requests()
  end function

  function get_nmode() bind(C, name="get_nmode")
    integer(c_int) :: get_nmode
    call lock_decode_requests()
    get_nmode = draft%nmode
    call unlock_decode_requests()
  end function

  function get_nsave() bind(C, name="get_nsave")
    integer(c_int) :: get_nsave
    call lock_decode_requests()
    get_nsave = draft%nsave
    call unlock_decode_requests()
  end function

  function get_max_drift() bind(C, name="get_max_drift")
    integer(c_int) :: get_max_drift
    call lock_decode_requests()
    get_max_drift = draft%max_drift
    call unlock_decode_requests()
  end function

  function get_nhsym() bind(C, name="get_nhsym")
    integer(c_int) :: get_nhsym
    call lock_decode_requests()
    get_nhsym = draft%nhsym
    call unlock_decode_requests()
  end function  

  subroutine set_fcenter(val) bind(C, name="set_fcenter")
  use iso_c_binding
  real(c_double), value :: val
    call lock_decode_requests()
  draft%fcenter = val
    call unlock_decode_requests()
  end subroutine
  
  subroutine set_nkeep(val) bind(C, name="set_nkeep")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nkeep = val
    call unlock_decode_requests()
  end subroutine
  
  subroutine set_ndop00(val) bind(C, name="set_ndop00")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%ndop00 = val
    call unlock_decode_requests()
  end subroutine
  
  subroutine set_map65RxLog(val) bind(C, name="set_map65RxLog")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%map65RxLog = val
    draft%nrxlog = val
    call unlock_decode_requests()
  end subroutine
  
  subroutine set_nutc(val) bind(C, name="set_nutc")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nutc = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_idphi(val) bind(C, name="set_idphi")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%idphi = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_mousedf(val) bind(C, name="set_mousedf")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%mousedf = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_mousefqso(val) bind(C, name="set_mousefqso")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%mousefqso = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nagain(val) bind(C, name="set_nagain")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nagain = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_ndepth(val) bind(C, name="set_ndepth")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%ndepth = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_ndiskdat(val) bind(C, name="set_ndiskdat")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%ndiskdat = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_neme(val) bind(C, name="set_neme")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%neme = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_newdat(val) bind(C, name="set_newdat")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%newdat = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nfa(val) bind(C, name="set_nfa")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nfa = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nfb(val) bind(C, name="set_nfb")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nfb = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nfcal(val) bind(C, name="set_nfcal")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nfcal = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nfshift(val) bind(C, name="set_nfshift")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nfshift = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_mcall3(val) bind(C, name="set_mcall3")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%mcall3 = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_ntimeout(val) bind(C, name="set_ntimeout")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%ntimeout = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_ntol(val) bind(C, name="set_ntol")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%ntol = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nxant(val) bind(C, name="set_nxant")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nxant = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nfsample(val) bind(C, name="set_nfsample")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nfsample = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nxpol(val) bind(C, name="set_nxpol")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nxpol = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nmode(val) bind(C, name="set_nmode")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nmode = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nsave(val) bind(C, name="set_nsave")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nsave = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_max_drift(val) bind(C, name="set_max_drift")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%max_drift = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_nhsym(val) bind(C, name="set_nhsym")
    use iso_c_binding
    integer(c_int), value :: val
    call lock_decode_requests()
    draft%nhsym = val
    call unlock_decode_requests()
  end subroutine

  subroutine set_mycall(buf) bind(C, name="set_mycall")
    use iso_c_binding
    character(kind=c_char), intent(in) :: buf(12)
    integer :: i
    call lock_decode_requests()
    do i = 1, 12
      draft%mycall(i:i) = buf(i)
    end do
    call unlock_decode_requests()
  end subroutine

  subroutine set_hiscall(buf) bind(C, name="set_hiscall")
    use iso_c_binding
    character(kind=c_char), intent(in) :: buf(12)
    integer :: i
    call lock_decode_requests()
    do i = 1, 12
      draft%hiscall(i:i) = buf(i)
    end do
    call unlock_decode_requests()
  end subroutine

  subroutine set_mygrid(buf) bind(C, name="set_mygrid")
    use iso_c_binding
    character(kind=c_char), intent(in) :: buf(6)
    integer :: i
    call lock_decode_requests()
    do i = 1, 6
      draft%mygrid(i:i) = buf(i)
    end do
    call unlock_decode_requests()
  end subroutine

  subroutine set_hisgrid(buf) bind(C, name="set_hisgrid")
    use iso_c_binding
    character(kind=c_char), intent(in) :: buf(6)
    integer :: i
    call lock_decode_requests()
    do i = 1, 6
      draft%hisgrid(i:i) = buf(i)
    end do
    call unlock_decode_requests()
end subroutine

subroutine set_datetime(buf) bind(C, name="set_datetime")
  use iso_c_binding
  character(kind=c_char), intent(in) :: buf(17)
  integer :: i
    call lock_decode_requests()
  do i = 1, 17
    draft%datetime(i:i) = buf(i)
  end do
    call unlock_decode_requests()
end subroutine

  function publish_decode_request() result(request_id) bind(C)
    integer(c_int64_t) :: request_id
    type(decode_request), pointer :: request

    allocate(request)
    call lock_decode_requests()
    published_request_id = published_request_id + 1
    request_id = published_request_id
    request%id = request_id
    request%input_generation = live_input_generation
    request%parameters = draft
    request%parameters%newdat = 1
    ! Automatic passes (nhsym1/nhsym2) were just snapshotted by dataSink();
    ! remember which slot and fill they own. Manual repeats use the latest.
    if (draft%manualDecodeFlag == 0 .and. draft%nagain == 0 .and. &
        (draft%nhsym == 280 .or. draft%nhsym == 302)) then
      request%snap_idx = merge(1, 2, draft%nhsym == 280)
      request%snap_seq = snap_seq(request%snap_idx)
      ! No fill has ever happened for this slot (no snapshot support in the
      ! caller): fall back to the input-generation rule.
      if (request%snap_seq == 0) request%snap_idx = 0
    endif
    if (associated(pending_tail)) then
      pending_tail%next => request
    else
      pending_head => request
    endif
    pending_tail => request
    decoder_ready = 1
    draft%manualDecodeFlag = 0
    call unlock_decode_requests()
  end function

  logical function claim_decode_request()
    type(decode_request), pointer :: request

    call lock_decode_requests()
    claim_decode_request = decoder_ready /= 0 .and. associated(pending_head)
    if (claim_decode_request) then
      request => pending_head
      pending_head => request%next
      if (.not. associated(pending_head)) nullify(pending_tail)
      active = request%parameters
      active_request_id = request%id
      active_input_generation = request%input_generation
      active_snap_idx = 0
      if (request%snap_idx > 0) then
        ! Automatic pass: valid while its snapshot is intact, even if the
        ! minute has rolled over. A live request more than one generation old
        ! (monitoring stopped, period changed) is still dropped.
        active_request_expired = snap_valid_seq(request%snap_idx) /= request%snap_seq
        if (active%ndiskdat == 0 .and. &
            live_input_generation - request%input_generation >= 2) active_request_expired = .true.
        if (.not. active_request_expired) then
          active_snap_idx = request%snap_idx
          snap_busy(active_snap_idx) = .true.
        endif
      else
        active_request_expired = active%ndiskdat == 0 .and. active%nagain == 0 .and. &
          active%manualDecodeFlag == 0 .and. request%input_generation /= live_input_generation
        if (.not. active_request_expired .and. snap_latest_idx > 0) then
          active_snap_idx = snap_latest_idx            ! manual repeat: latest snapshot
          snap_busy(active_snap_idx) = .true.
        endif
      endif
      deallocate(request)
    endif
    call unlock_decode_requests()
  end function

  subroutine initialize_decode_parameters()
    call lock_decode_requests()
    active = draft
    call unlock_decode_requests()
  end subroutine

  ! A snapshot fill starts: invalidate the slot's contents and say whether the
  ! copy may proceed (ok = .false. when the running pass is still reading it).
  subroutine snapshot_fill_begin(idx, ok)
    integer, intent(in) :: idx
    logical, intent(out) :: ok
    call lock_decode_requests()
    snap_seq(idx) = snap_seq(idx) + 1
    snap_valid_seq(idx) = 0
    ok = .not. snap_busy(idx)
    call unlock_decode_requests()
  end subroutine

  subroutine snapshot_fill_end(idx)
    integer, intent(in) :: idx
    call lock_decode_requests()
    snap_valid_seq(idx) = snap_seq(idx)
    snap_latest_idx = idx
    call unlock_decode_requests()
  end subroutine

  ! The active pass is finished with its snapshot slot.
  subroutine snapshot_release()
    call lock_decode_requests()
    if (active_snap_idx > 0) snap_busy(active_snap_idx) = .false.
    active_snap_idx = 0
    call unlock_decode_requests()
  end subroutine

  subroutine advance_live_input_generation() bind(C)
    call lock_decode_requests()
    live_input_generation = live_input_generation + 1
    call unlock_decode_requests()
  end subroutine

  subroutine clear_pending_requests()
    type(decode_request), pointer :: request
    do while (associated(pending_head))
      request => pending_head
      pending_head => request%next
      deallocate(request)
    enddo
    nullify(pending_tail)
  end subroutine

  subroutine cancel_pending_decode_requests() bind(C)
    call lock_decode_requests()
    call clear_pending_requests()
    decoder_ready = 0
    published_request_id = published_request_id + 1
    call unlock_decode_requests()
  end subroutine

  function get_stop_m65() result(stopped)
    integer(c_int) :: stopped
    call lock_decode_requests()
    stopped = stop_m65
    call unlock_decode_requests()
  end function

  logical(c_bool) function is_current_decode_request(request_id) bind(C)
    integer(c_int64_t), value :: request_id
    call lock_decode_requests()
    is_current_decode_request = request_id == published_request_id
    call unlock_decode_requests()
  end function

end module npar_ptrs_mod
