program test_msk_spectrum_edges
  use msk_spectrum, only: msk_clip_spectrum_window,msk_peak_offset
  use packjt77, only: MAXRECENT
  use, intrinsic :: iso_fortran_env, only: real64
  use, intrinsic :: ieee_arithmetic, only: ieee_quiet_nan,ieee_value
  implicit none

  call test_windows
  call test_peak_offsets
  call test_frequency_searches
  call test_msk40_sync_column
  call test_short_ping_input_bounds

  print *, 'MSK spectrum edge tests passed'

contains

  subroutine test_windows
    integer :: lo,hi
    logical :: usable

    call msk_clip_spectrum_window(240,-3d0,2d0,lo,hi,usable)
    call assert_true(usable .and. lo.eq.1 .and. hi.eq.2, &
         'lower partial window is clipped')

    call msk_clip_spectrum_window(864,863d0,867d0,lo,hi,usable)
    call assert_true(usable .and. lo.eq.863 .and. hi.eq.864, &
         'upper partial window is clipped')

    call msk_clip_spectrum_window(240,1d0,1d0,lo,hi,usable)
    call assert_true(usable .and. lo.eq.1 .and. hi.eq.1, &
         'first FFT bin is usable')

    call msk_clip_spectrum_window(864,864d0,864d0,lo,hi,usable)
    call assert_true(usable .and. lo.eq.864 .and. hi.eq.864, &
         'last FFT bin is usable')

    call assert_rejected_window(240,-5d0,0d0,'window below the FFT is rejected')
    call assert_rejected_window(240,241d0,245d0,'window above the FFT is rejected')
    call assert_rejected_window(240,20d0,10d0,'reversed window is rejected')
    call assert_rejected_window(0,1d0,1d0,'invalid FFT size is rejected')
  end subroutine test_windows

  subroutine assert_rejected_window(nfft,requested_lo,requested_hi,message)
    integer, intent(in) :: nfft
    real(real64), intent(in) :: requested_lo,requested_hi
    character(len=*), intent(in) :: message
    integer :: lo,hi
    logical :: usable

    call msk_clip_spectrum_window(nfft,requested_lo,requested_hi,lo,hi,usable)
    call assert_true(.not.usable .and. lo.eq.1 .and. hi.eq.0,message)
  end subroutine assert_rejected_window

  subroutine test_peak_offsets
    integer, parameter :: NFFT40=240,NFFT144=864
    complex :: spectrum40(NFFT40),spectrum144(NFFT144)
    real :: offset

    spectrum40=cmplx(0.0,0.0)
    spectrum144=cmplx(0.0,0.0)
    spectrum40(1)=cmplx(2.0,1.0)
    spectrum40(NFFT40)=cmplx(3.0,-1.0)
    spectrum144(1)=cmplx(2.0,1.0)
    spectrum144(NFFT144)=cmplx(3.0,-1.0)
    call assert_close(msk_peak_offset(spectrum40,1),0.0, &
         'MSK40 first-bin offset is zero')
    call assert_close(msk_peak_offset(spectrum40,NFFT40),0.0, &
         'MSK40 last-bin offset is zero')
    call assert_close(msk_peak_offset(spectrum144,1),0.0, &
         'MSK144 first-bin offset is zero')
    call assert_close(msk_peak_offset(spectrum144,NFFT144),0.0, &
         'MSK144 last-bin offset is zero')

    spectrum40=cmplx(0.0,0.0)
    offset=msk_peak_offset(spectrum40,120)
    call assert_close(offset,0.0,'all-zero offset is zero')

    spectrum40(119:121)=cmplx(2.0,1.0)
    offset=msk_peak_offset(spectrum40,120)
    call assert_close(offset,0.0,'flat offset is zero')

    spectrum40=cmplx(0.0,0.0)
    spectrum40(119)=cmplx(1.0,0.0)
    spectrum40(120)=cmplx(2.0,0.0)
    spectrum40(121)=cmplx(3.0,0.0)
    offset=msk_peak_offset(spectrum40,120)
    call assert_close(offset,0.0,'zero-curvature offset is zero')

    spectrum40(119)=cmplx(1.0,0.0)
    spectrum40(120)=cmplx(4.0,0.0)
    spectrum40(121)=cmplx(3.0,0.0)
    offset=msk_peak_offset(spectrum40,120)
    call assert_close(offset,0.5,'ordinary interpolation is preserved')
  end subroutine test_peak_offsets

  subroutine test_frequency_searches
    integer, parameter :: NSPM40=240,NFRAMES40=3
    integer, parameter :: NSPM144=864,NFRAMES144=8
    complex :: cb40(42),cb144(42)
    complex :: cdat40(NSPM40*NFRAMES40),cdat2_40(NSPM40*NFRAMES40)
    complex :: cdat144(NSPM144*NFRAMES144),cdat2_144(NSPM144*NFRAMES144)
    complex :: cs40(NSPM40),cs144(NSPM144)
    integer :: navmask40(NFRAMES40),navmask144(NFRAMES144)
    real :: bestf,nan_value,xccs40(0:NSPM40-1),xccs144(0:NSPM144-1),xmax

    cb144=cmplx(0.0,0.0)
    cdat144=cmplx(0.0,0.0)
    cdat2_144=cmplx(1.0,1.0)
    cs144=cmplx(1.0,1.0)
    xccs144=1.0
    navmask144=1
    xmax=1.0
    bestf=1.0
    call msk144_freq_search(cdat144,0.0,-1,1,2.0,NFRAMES144,navmask144, &
         cb144,cdat2_144,xmax,bestf,cs144,xccs144)
    call assert_close(xmax,0.0,'MSK144 zero correlation has zero metric')
    call assert_close(bestf,-2.0,'MSK144 zero correlation selects first frequency')
    call assert_true(all(cs144.eq.cmplx(0.0,0.0)), &
         'MSK144 zero correlation initializes averaged data')
    call assert_true(all(xccs144.eq.0.0), &
         'MSK144 zero correlation initializes correlation data')

    cb40=cmplx(0.0,0.0)
    cdat40=cmplx(0.0,0.0)
    cdat2_40=cmplx(1.0,1.0)
    cs40=cmplx(1.0,1.0)
    xccs40=1.0
    navmask40=0
    xmax=1.0
    bestf=1.0
    call msk40_freq_search(cdat40,0.0,0,0,1.0,NFRAMES40,navmask40,cb40, &
         cdat2_40,xmax,bestf,cs40,xccs40)
    call assert_close(xmax,0.0,'empty MSK40 average has zero metric')
    call assert_close(bestf,0.0,'empty MSK40 average has zero frequency')
    call assert_true(all(cs40.eq.cmplx(0.0,0.0)), &
         'empty MSK40 average initializes averaged data')
    call assert_true(all(xccs40.eq.0.0), &
         'empty MSK40 average initializes correlation data')

    nan_value=ieee_value(0.0,ieee_quiet_nan)
    cdat40=cmplx(0.0,0.0)
    cdat40(1)=cmplx(nan_value,0.0)
    cs40=cmplx(1.0,1.0)
    xccs40=1.0
    navmask40=1
    xmax=1.0
    bestf=1.0
    call msk40_freq_search(cdat40,0.0,0,0,1.0,NFRAMES40,navmask40,cb40, &
         cdat2_40,xmax,bestf,cs40,xccs40)
    call assert_close(xmax,0.0,'non-finite MSK40 correlation has zero metric')
    call assert_close(bestf,0.0,'non-finite MSK40 correlation has zero frequency')
    call assert_true(all(cs40.eq.cmplx(0.0,0.0)), &
         'non-finite MSK40 correlation keeps averaged data initialized')
    call assert_true(all(xccs40.eq.0.0), &
         'non-finite MSK40 correlation keeps correlation data initialized')
  end subroutine test_frequency_searches

  subroutine test_msk40_sync_column
    integer, parameter :: NSPM=240
    complex :: cb(42),c(NSPM),cdat(NSPM)
    integer :: navmask(1),npklocs(1),nsuccess
    real :: fest

    call make_msk40_sync_word(cb)
    cdat=cmplx(0.0,0.0)
    cdat(1:42)=cb
    navmask=1
    call msk40sync(cdat,1,0,1.0,navmask,1,0.0,fest,npklocs,nsuccess,c)
    call assert_true(npklocs(1).eq.1, &
         'MSK40 lag-zero correlation remains in the first column bin')
  end subroutine test_msk40_sync_column

  subroutine make_msk40_sync_word(cb)
    complex, intent(out) :: cb(42)
    integer :: i,s8r(8)
    real :: angle,cbi(42),cbq(42),pi,pp(12)

    pi=4.0*atan(1.0)
    do i=1,12
      angle=(i-1)*pi/12.0
      pp(i)=sin(angle)
    enddo
    s8r=(/1,0,1,1,0,0,0,1/)
    s8r=2*s8r-1
    cbq(1:6)=pp(7:12)*s8r(1)
    cbq(7:18)=pp*s8r(3)
    cbq(19:30)=pp*s8r(5)
    cbq(31:42)=pp*s8r(7)
    cbi(1:12)=pp*s8r(2)
    cbi(13:24)=pp*s8r(4)
    cbi(25:36)=pp*s8r(6)
    cbi(37:42)=pp(1:6)*s8r(8)
    cb=cmplx(cbi,cbq)
  end subroutine make_msk40_sync_word

  subroutine test_short_ping_input_bounds
    integer, parameter :: NSPM40=240,MAXSTEPS40=150
    integer, parameter :: NSPM144=864,MAXSTEPS144=100
    integer, parameter :: N40=NSPM40+(MAXSTEPS40+1)*60
    integer, parameter :: N144=NSPM144+(MAXSTEPS144+1)*216
    character(len=12) :: hiscall,mycall
    character(len=37) :: message
    complex, allocatable :: c40(:),c144(:)
    complex :: ct(NSPM144)
    integer :: navg,nhasharray(MAXRECENT,MAXRECENT),nsuccess
    logical(kind=1) :: bswl
    real :: fret,nan_value,softbits(144),tret

    allocate(c40(N40),c144(N144))
    c144=cmplx(0.0,0.0)
    c40=cmplx(0.0,0.0)
    mycall='K1ABC'
    hiscall='W9XYZ'
    bswl=.false.
    nhasharray=0

    nsuccess=-1
    call msk144spd(c144,1,50,nsuccess,message,1500.0,fret,tret,navg,ct, &
         softbits)
    call assert_true(nsuccess.eq.0 .and. len_trim(message).eq.0, &
         'short MSK144 input returns no decode')

    nsuccess=-1
    call msk40spd(c40,1,50,mycall,hiscall,bswl,nhasharray,nsuccess, &
         message,1500.0,fret,tret,navg)
    call assert_true(nsuccess.eq.0 .and. len_trim(message).eq.0, &
         'short MSK40 input returns no decode')

    nsuccess=-1
    call msk144spd(c144,N144,50,nsuccess,message,1500.0,fret,tret,navg,ct, &
         softbits)
    call assert_true(nsuccess.eq.0 .and. len_trim(message).eq.0, &
         'oversized MSK144 input returns no decode within metric bounds')

    nsuccess=-1
    call msk40spd(c40,N40,50,mycall,hiscall,bswl,nhasharray,nsuccess, &
         message,1500.0,fret,tret,navg)
    call assert_true(nsuccess.eq.0 .and. len_trim(message).eq.0, &
         'oversized MSK40 input returns no decode within metric bounds')

    nsuccess=-1
    call msk144spd(c144,N144,-1,nsuccess,message,1500.0,fret,tret,navg,ct, &
         softbits)
    call assert_true(nsuccess.eq.0 .and. len_trim(message).eq.0, &
         'reversed MSK144 windows return no decode')

    nsuccess=-1
    call msk40spd(c40,N40,-1,mycall,hiscall,bswl,nhasharray,nsuccess, &
         message,1500.0,fret,tret,navg)
    call assert_true(nsuccess.eq.0 .and. len_trim(message).eq.0, &
         'reversed MSK40 windows return no decode')

    nsuccess=-1
    call msk144spd(c144,N144,huge(0),nsuccess,message,1500.0,fret,tret,navg, &
         ct,softbits)
    call assert_true(nsuccess.eq.0 .and. len_trim(message).eq.0, &
         'extreme MSK144 tolerance returns no decode without overflow')

    nsuccess=-1
    call msk40spd(c40,N40,huge(0),mycall,hiscall,bswl,nhasharray,nsuccess, &
         message,1500.0,fret,tret,navg)
    call assert_true(nsuccess.eq.0 .and. len_trim(message).eq.0, &
         'extreme MSK40 tolerance returns no decode without overflow')

    nsuccess=-1
    call msk144spd(c144,N144,50,nsuccess,message,huge(1.0),fret,tret,navg, &
         ct,softbits)
    call assert_true(nsuccess.eq.0 .and. len_trim(message).eq.0, &
         'out-of-range MSK144 frequency returns no decode')

    nan_value=ieee_value(0.0,ieee_quiet_nan)
    nsuccess=-1
    call msk40spd(c40,N40,50,mycall,hiscall,bswl,nhasharray,nsuccess, &
         message,nan_value,fret,tret,navg)
    call assert_true(nsuccess.eq.0 .and. len_trim(message).eq.0, &
         'non-finite MSK40 frequency returns no decode')
  end subroutine test_short_ping_input_bounds

  subroutine assert_close(actual,expected,message)
    real, intent(in) :: actual,expected
    character(len=*), intent(in) :: message

    call assert_true(abs(actual-expected).lt.1.0e-6,message)
  end subroutine assert_close

  subroutine assert_true(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if(.not.condition) then
      write(*,'(a)') message
      error stop 1
    endif
  end subroutine assert_true

end program test_msk_spectrum_edges
