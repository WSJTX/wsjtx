program test_qmap_symspec_contract
  use iso_fortran_env, only: real32, real64
  use qmap_params, only: MAX_NSMAX, MAX_NFFT
  implicit none

  integer, parameter :: nfft = 32768, sample_rate = 96000
  integer, parameter :: bins(3) = [4000, 4300, 4600]
  real(real32), parameter :: pi = 3.14159265358979323846_real32
  real(real32) :: dd(2,MAX_NSMAX), ss(400,MAX_NFFT), savg(MAX_NFFT), junk(42)
  real(real64) :: fcenter
  integer :: nutc
  common /datcom/ dd, ss, savg, fcenter, nutc, junk
  real(real32) :: waterfall(nfft), pxdb, slimit, phase, powers(3)
  complex(real32) :: x
  logical(kind=1) :: lstrong(0:1023)
  integer :: i, k, ihsym, nzap, nkhz

  dd = 0.0
  ss = 0.0
  savg = 0.0
  fcenter = 144.125_real64
  do i = 1, nfft + 512
     phase = 2.0_real32*pi*real(i-1, real32)/real(nfft, real32)
     x = tone(bins(1), phase) + 2.0_real32*tone(bins(2), phase) &
          + tone(bins(3), phase + 0.71_real32)
     dd(1,i) = real(x)
     dd(2,i) = aimag(x)
  end do

  k = nfft
  ihsym = 0
  nzap = 0
  slimit = 0.0
  call symspec(k, 1, 0, 40, sample_rate, pxdb, waterfall, nkhz, ihsym, nzap, slimit, lstrong)
  call require(ihsym == 1, 'first completed row')
  call require(abs(maxloc(ss(1,1:nfft), dim=1) - (bins(2) + 1)) <= 1, 'strongest frequency bin')
  do i = 1, 3
     powers(i) = ss(1,bins(i)+1)
     call require(powers(i) > 0.0_real32, 'tone power')
     call require(abs(savg(bins(i)+1) / powers(i) - 1.0_real32) < 0.001_real32, &
          'savg accumulates completed row')
  end do
  call require(abs(powers(2) / powers(1) - 4.0_real32) < 0.12_real32, 'amplitude squared power')
  call require(abs(powers(3) / powers(1) - 1.0_real32) < 0.03_real32, 'phase preserves power')
  print '(a)', 'QMAP symspec contract passed.'

contains

  function tone(bin, angle) result(value)
    integer, intent(in) :: bin
    real(real32), intent(in) :: angle
    complex(real32) :: value
    real(real32) :: theta

    theta = real(bin, real32) * angle
    value = 300.0_real32 * cmplx(cos(theta), -sin(theta), real32)
  end function tone

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require
end program test_qmap_symspec_contract
