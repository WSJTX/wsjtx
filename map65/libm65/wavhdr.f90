module wavhdr
  use iso_fortran_env, only: int32, int16
  implicit none

  type hdr
     character(len=4) :: ariff
     integer(int32)   :: lenfile
     character(len=4) :: awave
     character(len=4) :: afmt
     integer(int32)   :: lenfmt
     integer(int16)   :: nfmt2
     integer(int16)   :: nchan2
     integer(int32)   :: nsamrate
     integer(int32)   :: nbytesec
     integer(int16)   :: nbytesam2
     integer(int16)   :: nbitsam2
     character(len=4) :: adata
     integer(int32)   :: ndata
  end type hdr

contains

  function default_header(nsamrate,npts) result(hdr_out)
    implicit none
    integer, intent(in) :: nsamrate, npts
    type(hdr) :: hdr_out, h

    h%ariff    = 'RIFF'
    h%awave    = 'WAVE'
    h%afmt     = 'fmt '
    h%lenfmt   = 16
    h%nfmt2    = 1
    h%nchan2   = 1
    h%nsamrate = nsamrate
    h%nbitsam2 = 16
    h%nbytesam2 = h%nbitsam2 * h%nchan2 / 8
    h%adata    = 'data'
    h%nbytesec = h%nsamrate * h%nbitsam2 * h%nchan2 / 8
    h%ndata    = 2*npts
    h%lenfile  = h%ndata + 44 - 8

    hdr_out = h
  end function default_header

  subroutine set_wsjtx_wav_params(fMHz, mode, nsubmode, ntrperiod, id2)
    implicit none
    real,          intent(in)  :: fMHz
    character(len=8),   intent(in)  :: mode
    integer,       intent(in)  :: nsubmode, ntrperiod
    integer(int16),     intent(out) :: id2(4)

    integer, parameter :: NBANDS=23, NMODES=13
    character(len=8) :: modes(NMODES)
    integer(int16)   :: iband, imode, ip
    integer     :: i
    integer     :: iperiod(8)
    real        :: fband(NBANDS), dmin

    data fband/0.137,0.474,1.8,3.5,5.1,7.0,10.14,14.0,18.1,21.0,24.9,  &
         28.0,50.0,144.0,222.0,432.0,902.0,1296.0,2304.0,3400.0,       &
         5760.0,10368.0,24048.0/
    data modes/'Echo','FSK441','ISCAT','JT4','JT65','JT6M','JT9',      &
         'JT9+JT65','JTMS','JTMSK','WSPR','FT8','FT2'/
    data iperiod/5,10,15,30,60,120,900,0/

    dmin  = 1.e30
    iband = 0
    do i=1,NBANDS
       if (abs(fMHz-fband(i)) < dmin) then
          dmin  = abs(fMHz-fband(i))
          iband = i
       end if
    end do

    imode = 0
    do i=1,NMODES
       if (mode == modes(i)) imode = i
    end do

    ip = 0
    do i=1,8
       if (ntrperiod == iperiod(i)) ip = i
    end do

    id2(1) = iband
    id2(2) = imode
    id2(3) = nsubmode
    id2(4) = ip
  end subroutine set_wsjtx_wav_params

  subroutine get_wsjtx_wav_params(id2, band, mode, nsubmode, ntrperiod, ok)
    implicit none
    integer(int16),   intent(in)  :: id2(4)
    character(len=6), intent(out) :: band
    character(len=8), intent(out) :: mode
    integer,     intent(out) :: nsubmode, ntrperiod
    logical,     intent(out) :: ok

    integer, parameter :: NBANDS=23, NMODES=13
    character(len=8) :: modes(NMODES)
    character(len=6) :: bands(NBANDS)
    integer     :: iperiod(8)

    data modes/'Echo','FSK441','ISCAT','JT4','JT65','JT6M','JT9',    &
         'JT9+JT65','JTMS','JTMSK','WSPR','FT8','FT2'/
    data iperiod/5,10,15,30,60,120,900,0/
    data bands/'2190m','630m','160m','80m','60m','40m','30m','20m',  &
         '17m','15m','12m','10m','6m','2m','1.25m','70cm','33cm',    &
         '23cm','13cm','9cm','6cm','3cm','1.25cm'/

    ok = .true.
    if (id2(1) < 1 .or. id2(1) > NBANDS) ok = .false.
    if (id2(2) < 1 .or. id2(2) > NMODES) ok = .false.
    if (id2(3) < 1 .or. id2(3) > 8)      ok = .false.
    if (id2(4) < 1 .or. id2(4) > 8)      ok = .false.

    if (ok) then
       band      = bands(id2(1))
       mode      = modes(id2(2))
       nsubmode  = id2(3)
       ntrperiod = iperiod(id2(4))
    end if
  end subroutine get_wsjtx_wav_params

end module wavhdr
