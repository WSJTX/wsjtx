module npar_ptrs_mod
  use iso_c_binding
  implicit none

  ! Scalar values
  real(c_double) :: fcenter      ! RF center frequency (MHz)
  integer(c_int) :: nfa          ! Low decode limit (kHz, RF-relative)
  integer(c_int) :: nfb          ! High decode limit (kHz, RF-relative)
  integer(c_int) :: nfshift      ! Display center shift (kHz, RF-relative)
  integer(c_int) :: nfcal        ! Frequency calibration offset (Hz)
  integer(c_int) :: nutc, idphi, mousedf, mousefqso, nagain, ndepth
  integer(c_int) :: ndiskdat, neme, newdat,map65RxLog
  integer(c_int) :: mcall3, ntimeout, ntol, nxant, nfsample
  integer(c_int) :: nxpol, nmode, nsave, max_drift, nhsym
  integer(c_int) :: ndop00,nrxlog,nkeep
  character(len=12) :: mycall, hiscall
  character(len=6)  :: mygrid, hisgrid
  character(len=20) :: datetime  ! changed 1/4/26 to match legacy code
  integer(c_int)  :: stop_m65 = 0
  integer(c_int)  :: decoder_ready = 0
  character(len=512) :: wsjtx_dir = ''

  
contains

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
    stop_m65 = 0
    if (val /= 0) stop_m65 = 1
  end subroutine

  subroutine set_decoder_ready(val) bind(C, name="set_decoder_ready")
    use iso_c_binding
    integer(c_int), value :: val    
    decoder_ready = 0
    if (val /= 0) decoder_ready = 1
  end subroutine
    
  subroutine get_mycall(buf) bind(C, name="get_mycall")
    character(kind=c_char), intent(out) :: buf(12)
    integer :: i

    do i = 1, 12
      buf(i) = mycall(i:i)
    end do
  end subroutine get_mycall

  subroutine get_hiscall(buf) bind(C, name="get_hiscall")
    character(kind=c_char), intent(out) :: buf(12)
    integer :: i
    do i = 1, 12
      buf(i) = hiscall(i:i)
    end do
  end subroutine

  subroutine get_mygrid(buf) bind(C, name="get_mygrid")
    character(kind=c_char), intent(out) :: buf(6)
    integer :: i
    do i = 1, 6
      buf(i) = mygrid(i:i)
    end do
  end subroutine

  subroutine get_hisgrid(buf) bind(C, name="get_hisgrid")
    character(kind=c_char), intent(out) :: buf(6)
    integer :: i
    do i = 1, 6
      buf(i) = hisgrid(i:i)
    end do
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
    do i = 1, 17
      buf(i) = datetime(i:i)
    end do
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
    get_fcenter = fcenter
  end function
  
  function get_nkeep() bind(C, name="get_nkeep")
    integer(c_int) :: get_nkeep
    get_nkeep = nkeep
  end function
  
  function get_ndop00() bind(C, name="get_ndop00")
    integer(c_int) :: get_ndop00
    get_ndop00 = ndop00
  end function
  
  function get_map65RxLog() bind(C, name="get_map65RxLog")
    integer(c_int) :: get_map65RxLog
    get_map65RxLog = map65RxLog
  end function
  
  function get_nutc() bind(C, name="get_nutc")
    integer(c_int) :: get_nutc
    get_nutc = nutc
  end function

  function get_idphi() bind(C, name="get_idphi")
    integer(c_int) :: get_idphi
    get_idphi = idphi
  end function

  function get_mousedf() bind(C, name="get_mousedf")
    integer(c_int) :: get_mousedf
    get_mousedf = mousedf
  end function

  function get_mousefqso() bind(C, name="get_mousefqso")
    integer(c_int) :: get_mousefqso
    get_mousefqso = mousefqso
  end function

  function get_nagain() bind(C, name="get_nagain")
    integer(c_int) :: get_nagain
    get_nagain = nagain
  end function

  function get_ndepth() bind(C, name="get_ndepth")
    integer(c_int) :: get_ndepth
    get_ndepth = ndepth
  end function

  function get_ndiskdat() bind(C, name="get_ndiskdat")
    integer(c_int) :: get_ndiskdat
    get_ndiskdat = ndiskdat
  end function

  function get_neme() bind(C, name="get_neme")
    integer(c_int) :: get_neme
    get_neme = neme
  end function

  function get_newdat() bind(C, name="get_newdat")
    integer(c_int) :: get_newdat
    get_newdat = newdat
  end function

  function get_nfa() bind(C, name="get_nfa")
    integer(c_int) :: get_nfa
    get_nfa = nfa
  end function

  function get_nfb() bind(C, name="get_nfb")
    integer(c_int) :: get_nfb
    get_nfb = nfb
  end function

  function get_nfcal() bind(C, name="get_nfcal")
    integer(c_int) :: get_nfcal
    get_nfcal = nfcal
  end function

  function get_nfshift() bind(C, name="get_nfshift")
    integer(c_int) :: get_nfshift
    get_nfshift = nfshift
  end function

  function get_mcall3() bind(C, name="get_mcall3")
    integer(c_int) :: get_mcall3
    get_mcall3 = mcall3
  end function

  function get_ntimeout() bind(C, name="get_ntimeout")
    integer(c_int) :: get_ntimeout
    get_ntimeout = ntimeout
  end function

  function get_ntol() bind(C, name="get_ntol")
    integer(c_int) :: get_ntol
    get_ntol = ntol
  end function

  function get_nxant() bind(C, name="get_nxant")
    integer(c_int) :: get_nxant
    get_nxant = nxant
  end function

  function get_nfsample() bind(C, name="get_nfsample")
    integer(c_int) :: get_nfsample
    get_nfsample = nfsample
  end function

  function get_nxpol() bind(C, name="get_nxpol")
    integer(c_int) :: get_nxpol
    get_nxpol = nxpol
  end function

  function get_nmode() bind(C, name="get_nmode")
    integer(c_int) :: get_nmode
    get_nmode = nmode
  end function

  function get_nsave() bind(C, name="get_nsave")
    integer(c_int) :: get_nsave
    get_nsave = nsave
  end function

  function get_max_drift() bind(C, name="get_max_drift")
    integer(c_int) :: get_max_drift
    get_max_drift = max_drift
  end function

  function get_nhsym() bind(C, name="get_nhsym")
    integer(c_int) :: get_nhsym
    get_nhsym = nhsym
  end function  

  subroutine set_fcenter(val) bind(C, name="set_fcenter")
  use iso_c_binding
  real(c_double), value :: val
  fcenter = val
  end subroutine
  
  subroutine set_nkeep(val) bind(C, name="set_nkeep")
    use iso_c_binding
    integer(c_int), value :: val
    nkeep = val
  end subroutine
  
  subroutine set_ndop00(val) bind(C, name="set_ndop00")
    use iso_c_binding
    integer(c_int), value :: val
    ndop00 = val
  end subroutine
  
  subroutine set_map65RxLog(val) bind(C, name="set_map65RxLog")
    use iso_c_binding
    integer(c_int), value :: val
    map65RxLog = val
  end subroutine
  
  subroutine set_nutc(val) bind(C, name="set_nutc")
    use iso_c_binding
    integer(c_int), value :: val
    nutc = val
  end subroutine

  subroutine set_idphi(val) bind(C, name="set_idphi")
    use iso_c_binding
    integer(c_int), value :: val
    idphi = val
  end subroutine

  subroutine set_mousedf(val) bind(C, name="set_mousedf")
    use iso_c_binding
    integer(c_int), value :: val
    mousedf = val
  end subroutine

  subroutine set_mousefqso(val) bind(C, name="set_mousefqso")
    use iso_c_binding
    integer(c_int), value :: val
    mousefqso = val
  end subroutine

  subroutine set_nagain(val) bind(C, name="set_nagain")
    use iso_c_binding
    integer(c_int), value :: val
    nagain = val
  end subroutine

  subroutine set_ndepth(val) bind(C, name="set_ndepth")
    use iso_c_binding
    integer(c_int), value :: val
    ndepth = val
  end subroutine

  subroutine set_ndiskdat(val) bind(C, name="set_ndiskdat")
    use iso_c_binding
    integer(c_int), value :: val
    ndiskdat = val
  end subroutine

  subroutine set_neme(val) bind(C, name="set_neme")
    use iso_c_binding
    integer(c_int), value :: val
    neme = val
  end subroutine

  subroutine set_newdat(val) bind(C, name="set_newdat")
    use iso_c_binding
    integer(c_int), value :: val
    newdat = val
  end subroutine

  subroutine set_nfa(val) bind(C, name="set_nfa")
    use iso_c_binding
    integer(c_int), value :: val
    nfa = val
  end subroutine

  subroutine set_nfb(val) bind(C, name="set_nfb")
    use iso_c_binding
    integer(c_int), value :: val
    nfb = val
  end subroutine

  subroutine set_nfcal(val) bind(C, name="set_nfcal")
    use iso_c_binding
    integer(c_int), value :: val
    nfcal = val
  end subroutine

  subroutine set_nfshift(val) bind(C, name="set_nfshift")
    use iso_c_binding
    integer(c_int), value :: val
    nfshift = val
  end subroutine

  subroutine set_mcall3(val) bind(C, name="set_mcall3")
    use iso_c_binding
    integer(c_int), value :: val
    mcall3 = val
  end subroutine

  subroutine set_ntimeout(val) bind(C, name="set_ntimeout")
    use iso_c_binding
    integer(c_int), value :: val
    ntimeout = val
  end subroutine

  subroutine set_ntol(val) bind(C, name="set_ntol")
    use iso_c_binding
    integer(c_int), value :: val
    ntol = val
  end subroutine

  subroutine set_nxant(val) bind(C, name="set_nxant")
    use iso_c_binding
    integer(c_int), value :: val
    nxant = val
  end subroutine

  subroutine set_nfsample(val) bind(C, name="set_nfsample")
    use iso_c_binding
    integer(c_int), value :: val
    nfsample = val
  end subroutine

  subroutine set_nxpol(val) bind(C, name="set_nxpol")
    use iso_c_binding
    integer(c_int), value :: val
    nxpol = val
  end subroutine

  subroutine set_nmode(val) bind(C, name="set_nmode")
    use iso_c_binding
    integer(c_int), value :: val
    nmode = val
  end subroutine

  subroutine set_nsave(val) bind(C, name="set_nsave")
    use iso_c_binding
    integer(c_int), value :: val
    nsave = val
  end subroutine

  subroutine set_max_drift(val) bind(C, name="set_max_drift")
    use iso_c_binding
    integer(c_int), value :: val
    max_drift = val
  end subroutine

  subroutine set_nhsym(val) bind(C, name="set_nhsym")
    use iso_c_binding
    integer(c_int), value :: val
    nhsym = val
  end subroutine

  subroutine set_mycall(buf) bind(C, name="set_mycall")
    use iso_c_binding
    character(kind=c_char), intent(in) :: buf(12)
    integer :: i
    do i = 1, 12
      mycall(i:i) = buf(i)
    end do
  end subroutine

  subroutine set_hiscall(buf) bind(C, name="set_hiscall")
    use iso_c_binding
    character(kind=c_char), intent(in) :: buf(12)
    integer :: i
    do i = 1, 12
      hiscall(i:i) = buf(i)
    end do
  end subroutine

  subroutine set_mygrid(buf) bind(C, name="set_mygrid")
    use iso_c_binding
    character(kind=c_char), intent(in) :: buf(6)
    integer :: i
    do i = 1, 6
      mygrid(i:i) = buf(i)
    end do
  end subroutine

  subroutine set_hisgrid(buf) bind(C, name="set_hisgrid")
    use iso_c_binding
    character(kind=c_char), intent(in) :: buf(6)
    integer :: i
    do i = 1, 6
      hisgrid(i:i) = buf(i)
    end do
end subroutine

subroutine set_datetime(buf) bind(C, name="set_datetime")
  use iso_c_binding
  character(kind=c_char), intent(in) :: buf(17)
  integer :: i
  do i = 1, 17
    datetime(i:i) = buf(i)
  end do
end subroutine

end module npar_ptrs_mod
