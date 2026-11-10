program test_map65_filbig_boundary
  use iso_fortran_env, only: real32, real64
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite, ieee_quiet_nan, ieee_value
  use filbig_mod, only: filbig, MAXFFT1, MAXFFT2
  use cacb_mod, only: init_cacb, ca, cb
  use decode1a_mod, only: jt65c_center_offset
  use npar_ptrs_mod, only: set_runtime_params, symspec_fft_size
  implicit none

  real(real32) :: dd(4,1)
  complex :: cx(MAXFFT2), cy(MAXFFT2)
  real(real64) :: f0
  integer :: rate, nfft, nfft_big, newdat, n4
  integer, parameter :: nfft_short = 77175
  character(len=16) :: argument

  call get_command_argument(1, argument)
  read(argument, *) rate
  nfft = symspec_fft_size(rate)
  nfft_big = 56 * rate
  call set_runtime_params(rate, nfft, nfft_big)
  call init_cacb(MAXFFT1)

  ! Poison unused storage so reads beyond the active FFT cannot pass unnoticed.
  ca = cmplx(ieee_value(0.0_real32, ieee_quiet_nan), 0.0_real32)
  ca(:nfft_big) = cmplx(1.0_real32, -0.25_real32)
  cb = ca
  dd = 0.0_real32
  newdat = 0

  f0 = real(nfft - 52, real64) * real(rate, real64) / real(nfft, real64)
  call check_extraction(f0)
  call check_extraction(f0 + jt65c_center_offset)
  call check_extraction(0.0_real64)

  call filbig(dd, 1, real(rate, real64) + 1378.125_real64, newdat, rate, .true., cx, cy, n4)
  call require(all(abs(cx(:nfft_short)) == 0.0), 'out-of-band X extraction is zero')
  call require(all(abs(cy(:nfft_short)) == 0.0), 'out-of-band Y extraction is zero')
  print '(a)', 'MAP65 filter boundary tests passed'

contains

  subroutine check_extraction(center)
    real(real64), intent(in) :: center

    call filbig(dd, 1, center, newdat, rate, .true., cx, cy, n4)
    call require(all(ieee_is_finite(real(cx(:nfft_short)))) .and. &
         all(ieee_is_finite(aimag(cx(:nfft_short)))), 'X extraction is finite')
    call require(all(ieee_is_finite(real(cy(:nfft_short)))) .and. &
         all(ieee_is_finite(aimag(cy(:nfft_short)))), 'Y extraction is finite')
    call require(any(abs(cx(:nfft_short)) > 0.0), 'available in-band samples are retained')
    call require(all(cx(:nfft_short) == cy(:nfft_short)), 'polarizations use the same boundary handling')
  end subroutine check_extraction

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    endif
  end subroutine require
end program test_map65_filbig_boundary
