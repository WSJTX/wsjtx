! SPDX-License-Identifier: GPL-3.0-or-later
! The stream's spectrum rows, one definition for every mode, with the state
! of each row stream in the caller's hands (lib/streaming_io.f90,
! lib/streaming_jtty.f90, lib/decode_msk144.f90).
!   wide_rows  a row per 3456 samples of a stream from its first sample: the
!              block's power and peak, and the power spectrum of the last
!              16384 samples, as symspec computes them (lib/symspec.f90), so
!              a period's rows equal the GUI's waterfall rows.
!   fast_rows  MSK144's Fast Graph columns, as hspec computes them
!              (lib/hspec.f90) without its decode.
! The FFTs are planned at patience 0, as the GUI's process plans them.

module spectrum_rows
  use, intrinsic :: iso_fortran_env, only: int16, int64
  use four2a_patience, only: patience_override
  implicit none
  private

  ! A row's samples, its FFT length and bins (0 to 5000 Hz), and their width.
  integer, parameter, public :: WIDE_BLOCK = 3456, WIDE_NFFT = 16384, WIDE_BINS = 6827
  real,    parameter, public :: WIDE_DF = 12000.0 / WIDE_NFFT
  ! The Fast Graph's columns and bins per column.
  integer, parameter, public :: FAST_COLUMNS = 703, FAST_BINS = 64

  type, public :: wide_rows
     ! The stream's last WIDE_NFFT samples, sample p (from 1) at mod(p-1, WIDE_NFFT).
     integer(int16) :: ring(0:WIDE_NFFT-1) = 0
     ! Samples since the stream's start, and in the block in progress.
     integer(int64) :: taken = 0
     integer :: pending = 0
  end type wide_rows

  type, public :: fast_rows
     ! hspec's saved ja and its k0 (huge: the next call starts its columns).
     integer :: ja = 0, k0 = 99999999
     ! The column last written (hspec's jh), and the columns.
     integer :: jh = 0
     real :: green(0:FAST_COLUMNS-1) = 0
     real :: bins(0:FAST_BINS-1, 0:FAST_COLUMNS-1) = 0
  end type fast_rows

  public :: wide_begin, wide_take, wide_row, fast_begin, fast_block

contains

  ! A stream starts: zeros before its first sample.
  subroutine wide_begin(state)
    type(wide_rows), intent(inout) :: state
    state%ring = 0
    state%taken = 0
    state%pending = 0
  end subroutine wide_begin

  ! The stream's next samples, up to the end of the block in progress:
  ! taken of them, and ready when that block is complete (wide_row's).
  subroutine wide_take(state, samples, taken, ready)
    type(wide_rows), intent(inout) :: state
    integer(int16),  intent(in)    :: samples(:)
    integer,         intent(out)   :: taken
    logical,         intent(out)   :: ready
    integer :: i
    taken = min(size(samples), WIDE_BLOCK - state%pending)
    do i = 1, taken
       state%ring(int(mod(state%taken, int(WIDE_NFFT, int64)))) = samples(i)
       state%taken = state%taken + 1
    end do
    state%pending = state%pending + taken
    ready = state%pending .eq. WIDE_BLOCK
    if (ready) state%pending = 0
  end subroutine wide_take

  ! The row of the block wide_take last completed: power and peak in dB of
  ! its samples, less the stream's first 10, and bins, the power spectrum of
  ! the last WIDE_NFFT samples, Nuttall-windowed with low_sidelobes.
  subroutine wide_row(state, low_sidelobes, power, peak, bins)
    type(wide_rows), intent(in)  :: state
    logical,         intent(in)  :: low_sidelobes
    real,            intent(out) :: power, peak, bins(WIDE_BINS)
    real, save :: window(WIDE_NFFT)
    logical, save :: have_window = .false.
    real :: xc(0:WIDE_NFFT-1)
    complex :: cx(0:WIDE_NFFT/2)
    equivalence (xc, cx)
    save xc
    real :: sq, pxmax, x1, sx, fac
    integer(int64) :: p, oldest
    integer :: i, patience

    sq = 0.
    pxmax = 0.
    do p = state%taken - WIDE_BLOCK + 1, state%taken
       if (p .le. 10) cycle
       x1 = state%ring(int(mod(p - 1, int(WIDE_NFFT, int64))))
       if (abs(x1) .gt. pxmax) pxmax = abs(x1)
       sq = sq + x1*x1
    end do
    power = 0.
    if (sq .gt. 0.0) power = 10*log10(sq/WIDE_BLOCK)
    peak = 0.
    if (pxmax .gt. 0) peak = 20*log10(pxmax)

    oldest = mod(state%taken, int(WIDE_NFFT, int64))
    do i = 0, WIDE_NFFT - 1
       xc(i) = 0.1*state%ring(int(mod(oldest + i, int(WIDE_NFFT, int64))))
    end do
    if (low_sidelobes) then
       if (.not. have_window) then
          window = 0
          call nuttal_window(window, WIDE_NFFT)
          have_window = .true.
       end if
       xc(0:WIDE_NFFT-1) = window(1:WIDE_NFFT)*xc(0:WIDE_NFFT-1)
    end if
    patience = patience_override
    patience_override = 0
    call four2a(xc, WIDE_NFFT, 1, -1, 0)
    patience_override = patience

    fac = (1.0/WIDE_NFFT)**2
    do i = 1, WIDE_BINS
       sx = fac*(real(cx(i-1))**2 + aimag(cx(i-1))**2)
       bins(i) = 1000.0*sx
    end do
  end subroutine wide_row

  ! A period starts: the next fast_block call starts the columns at 0.
  subroutine fast_begin(state)
    type(fast_rows), intent(inout) :: state
    state%ja = 0
    state%k0 = 99999999
    state%jh = 0
  end subroutine fast_begin

  ! The Fast Graph through k of id2, the period's samples from index 0, at
  ! a block end: the columns hspec writes, up to state%jh, with power and
  ! peak in dB, its dbNoGain and pxmax (0 when it sets none). trperiod below
  ! 30 s steps 256 samples, else 512. Nothing past 30 s.
  subroutine fast_block(state, id2, k, trperiod, power, peak)
    type(fast_rows), intent(inout) :: state
    integer(int16),  intent(in)    :: id2(0:*)
    integer,         intent(in)    :: k, trperiod
    real,            intent(out)   :: power, peak
    integer, parameter :: NFFT = 512
    real :: x(NFFT)
    complex :: cx(0:NFFT/2)
    equivalence (x, cx)
    save x
    real :: sq, xmax, xmin, rms, rms2, fac, sx
    integer :: nstep, nblks, iblk, jb, i, j, patience

    power = 0.
    peak = 0.
    nstep = NFFT
    nblks = 7
    if (trperiod .lt. 30) then
       nstep = 256
       nblks = 14
    end if
    if (k .gt. 30*12000) return
    if (k .lt. NFFT) then
       state%jh = 0
       return
    end if
    if (k .lt. state%k0) then
       state%ja = -nstep
       state%jh = -1
    end if

    do iblk = 1, nblks
       if (state%jh .lt. FAST_COLUMNS - 1) state%jh = state%jh + 1
       state%ja = state%ja + nstep
       jb = state%ja + NFFT - 1
       x = id2(state%ja:jb)
       sq = dot_product(x, x)
       xmax = maxval(x)
       xmin = abs(minval(x))
       if (xmin > xmax) xmax = xmin
       if (xmax .gt. 0.0) peak = 20.0*log10(xmax)
       rms = sqrt(sq/NFFT)
       rms2 = sqrt(sq/NFFT)
       state%green(state%jh) = 0.
       if (rms .gt. 0.0) then
          state%green(state%jh) = 20.0*log10(rms)
          power = 20.0*log10(rms2)
       end if
       patience = patience_override
       patience_override = 0
       call four2a(x, NFFT, 1, -1, 0)
       patience_override = patience
       fac = (1.0/NFFT)**2
       do i = 1, FAST_BINS
          j = 2*i
          sx = real(cx(j))**2 + aimag(cx(j))**2 + real(cx(j-1))**2 +           &
               aimag(cx(j-1))**2
          state%bins(i-1, state%jh) = fac*sx
       end do
       if (state%ja + 2*NFFT .gt. k) exit
    end do
    state%k0 = k
  end subroutine fast_block

end module spectrum_rows
