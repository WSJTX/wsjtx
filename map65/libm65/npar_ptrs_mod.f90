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

  ! GUI setters prepare draft; only the decoder changes active after claiming a request.
  type(decode_parameters), target, save :: active
  type(decode_parameters), save :: draft
  type :: decode_request
    type(decode_parameters) :: parameters
    integer(c_int64_t) :: id
    integer(c_int64_t) :: input_generation
    type(decode_request), pointer :: next => null()
  end type
  type(decode_request), pointer, save :: pending_head => null(), pending_tail => null()
  integer(c_int64_t), save :: published_request_id = 0
  integer(c_int64_t), save :: active_request_id = 0
  integer(c_int64_t), save :: live_input_generation = 0
  integer(c_int64_t), save :: active_input_generation = 0
  logical, save :: active_request_expired = .false.

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
    ! map65RxLog is read back by get_map65RxLog() for debug logging only.
    ! nrxlog is the variable m65c()/decode0.f90 actually check (iand bits
    ! 1/2/4/8) to write the date header, rewind map65_rx.log, clear the
    ! decode-history file on Erase, and force manual dPhi. Before this fix
    ! nrxlog was never assigned anywhere, so all four of those flag bits
    ! were permanently dead -- e.g. Erase Band Map and Messages never
    ! actually cleared unit 26, so the next display() cycle just replayed
    ! the full decode history back into the Messages window.
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
      active_request_expired = active%ndiskdat == 0 .and. active%nagain == 0 .and. &
        active%manualDecodeFlag == 0 .and. request%input_generation /= live_input_generation
      deallocate(request)
    endif
    call unlock_decode_requests()
  end function

  subroutine initialize_decode_parameters()
    call lock_decode_requests()
    active = draft
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
