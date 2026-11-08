program test_map65_symspec_contract
  use iso_fortran_env, only: int8, real32
  use, intrinsic :: ieee_arithmetic, only: ieee_quiet_nan, ieee_value
  use datcom_ptrs_mod, only: dd, ss, savg
  use npar_ptrs_mod, only: set_runtime_params, symspec_fft_size
  use symspec_mod, only: symspec
  implicit none

  integer, parameter :: bins(5) = [4000, 4300, 4600, 4900, 5200]
  real(real32), parameter :: pi = 3.14159265358979323846_real32
  integer :: rate, fft_size, expected_fft_size, i, k, ihsym, nzap, nkhz
  integer(int8) :: lstrong(0:1023)
  character(len=16) :: rate_text
  real(real32), allocatable :: waterfall(:)
  real(real32) :: gainx, gainy, phasex, phasey, rejectx, rejecty, pxdb, pydb, slimit
  real(real32) :: power(4,5), expected(4,3), phase, nan_value
  complex(real32) :: x, y

  call get_command_argument(1, rate_text)
  read(rate_text, *) rate
  call require(any(rate == [96000, 128000, 192000, 256000]), 'supported sample rate')
  fft_size = symspec_fft_size(rate)
  select case (rate)
  case (96000);  expected_fft_size = 32768
  case (128000); expected_fft_size = 65536
  case (192000); expected_fft_size = 65536
  case (256000); expected_fft_size = 131072
  end select
  call require(fft_size == expected_fft_size, 'symspec FFT size for this sample rate')
  call set_runtime_params(rate, fft_size, 56 * rate)
  allocate(dd(4, fft_size + 1024), ss(4, 1, fft_size), savg(4, fft_size), waterfall(fft_size))
  dd = 0.0
  ss = 0.0
  savg = 0.0

  do i = 1, fft_size + 1024
     x = cmplx(0.0_real32, 0.0_real32, real32)
     y = x
     phase = 2.0_real32 * pi * real(i - 1, real32) / real(fft_size, real32)
     x = x + tone(bins(1), phase)
     x = x + tone(bins(2), phase)
     y = y + tone(bins(2), phase)
     x = x + tone(bins(3), phase)
     y = y - tone(bins(3), phase)
     x = x + 2.0_real32 * tone(bins(4), phase)
     x = x + tone(bins(5), phase + 0.71_real32)
     dd(1,i) = real(x)
     dd(2,i) = aimag(x)
     dd(3,i) = real(y)
     dd(4,i) = aimag(y)
  end do

  call spectrum(fft_size + 512, 1)
  call require(ihsym == 1, 'first completed row')
  do i = 1, 5
     call require(abs(maxloc(ss(1,1,:), dim=1) - (bins(4) + 1)) <= 1, &
          'strongest X frequency bin')
     power(:,i) = ss(:,1,bins(i)+1)
     call require(all(abs(savg(:,bins(i)+1) - power(:,i)) <= 0.0001_real32 * max(1.0_real32, maxval(power(:,i)))), &
          'savg accumulates completed row')
  end do
  expected(:,1) = [1.0_real32, 0.5_real32, 0.0_real32, 0.5_real32]
  expected(:,2) = [1.0_real32, 2.0_real32, 1.0_real32, 0.0_real32]
  expected(:,3) = [1.0_real32, 0.0_real32, 1.0_real32, 2.0_real32]
  do i = 1, 3
     call require(all(abs(power(:,i) / power(1,i) - expected(:,i)) < 0.03_real32), &
          'polarization relative powers')
  end do
  call require(abs(power(1,4) / power(1,1) - 4.0_real32) < 0.12_real32, 'amplitude squared power')
  call require(abs(power(1,5) / power(1,1) - 1.0_real32) < 0.03_real32, 'phase preserves power')

  nan_value = ieee_value(0.0_real32, ieee_quiet_nan)
  dd = 0.0
  do i = 1, fft_size + 1024
     x = tone(bins(1), 2.0_real32*pi*real(i-1, real32)/real(fft_size, real32))
     dd(1,i) = real(x)
     dd(2,i) = aimag(x)
  end do
  dd(3:4,:) = nan_value
  call spectrum(fft_size, 0)
  call require(ihsym == 1, 'single-polarization reset row')
  call require(abs(maxloc(ss(1,1,:), dim=1) - (bins(1) + 1)) <= 1, 'single-polarization frequency bin')
  call require(abs(ss(1,1,bins(1)+1) / power(1,1) - 1.0_real32) < 0.03_real32, &
       'poisoned Y cannot affect X power')
  call require(abs(savg(1,bins(1)+1) / ss(1,1,bins(1)+1) - 1.0_real32) < 0.001_real32, &
       'single-polarization savg reset')
  print '(a,i0)', 'MAP65 symspec contract passed at ', rate

contains

  function tone(bin, angle) result(value)
    integer, intent(in) :: bin
    real(real32), intent(in) :: angle
    complex(real32) :: value
    real(real32) :: theta

    theta = real(bin, real32) * angle
    value = 300.0_real32 * cmplx(cos(theta), -sin(theta), real32)
  end function tone

  subroutine spectrum(sample_count, polarization_count)
    integer, intent(in) :: sample_count, polarization_count

    k = sample_count
    ihsym = 0
    nzap = 0
    gainx = 1.0
    gainy = 1.0
    phasex = 0.0
    phasey = 0.0
    slimit = 0.0
    call symspec(k, polarization_count, 1, 0, 40, 0, 0, 0, gainx, gainy, phasex, phasey, &
         rejectx, rejecty, pxdb, pydb, waterfall, nkhz, ihsym, nzap, slimit, lstrong)
  end subroutine spectrum

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require
end program test_map65_symspec_contract
