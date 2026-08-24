program test_ccf65_correlator

  use ccf65_legacy_mod, only: ccf65
  use four2a_legacy_wrap_mod, only: c2r_legacy, r2c_legacy, JT65_NFFT, JT65_NH
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite, ieee_quiet_nan, ieee_value
  implicit none

  integer, parameter :: nhsym = 254
  real, parameter :: expected_sync = 25.8397
  real :: ss(4,322)
  integer :: i
  real :: sync1, dt, flipk, syncshort, snr2, dt2
  integer :: ipol1, ipol2

  call test_fft_roundtrip()

  call make_sync_plane(ss, 1.0)
  call ccf65(ss, nhsym, 1.0e30, sync1, ipol1, 1, dt, flipk, syncshort, snr2, ipol2, dt2)
  call require(sync1 > 0.0, 'normal JT65 sync is detected')
  call require(abs(sync1 - expected_sync) < 0.05, 'normal JT65 sync strength')
  call require(flipk > 0.0, 'normal JT65 polarity')
  call require(ipol1 == 1, 'normal JT65 polarization')
  call require(abs(dt + 2.5) < 0.02, 'normal JT65 timing')
  call require(syncshort < 0.0, 'normal JT65 is not shorthand')

  call make_sync_plane(ss, -1.0)
  call ccf65(ss, nhsym, 1.0e30, sync1, ipol1, 1, dt, flipk, syncshort, snr2, ipol2, dt2)
  call require(sync1 > 0.0, 'inverted JT65 sync is detected')
  call require(abs(sync1 - expected_sync) < 0.05, 'inverted JT65 sync strength')
  call require(flipk < 0.0, 'inverted JT65 polarity')
  call require(ipol1 == 1, 'inverted JT65 polarization')
  call require(abs(dt + 2.5) < 0.02, 'inverted JT65 timing')
  call require(syncshort < 0.0, 'inverted JT65 is not shorthand')

  ss = 0.0
  sync1 = -99.0
  do i = 1, nhsym - 1
     ss(1,i) = 0.01 * real(mod(i,3) - 1)
  end do
  call ccf65(ss, nhsym, 1.0e30, sync1, ipol1, 1, dt, flipk, syncshort, snr2, ipol2, dt2)
  call require(sync1 < 0.0, 'low-signal input is rejected')
  call require(syncshort < 0.0, 'low-signal input is not shorthand')

  ss = 0.0
  call ccf65(ss, nhsym, 1.0e30, sync1, ipol1, 1, dt, flipk, syncshort, snr2, ipol2, dt2)
  call require(sync1 == -4.0 .and. syncshort == -4.0, 'zero signal has explicit sync defaults')
  call require(all_finite(sync1, dt, flipk, syncshort, snr2, dt2), 'zero signal outputs are finite')

  call make_sync_plane(ss, 1.0)
  ss(2:4,:) = ieee_value(0.0, ieee_quiet_nan)
  call ccf65(ss, nhsym, 1.0e30, sync1, ipol1, 1, dt, flipk, syncshort, snr2, ipol2, dt2)
  call require(sync1 > 0.0, 'inactive polarization data does not affect JT65 detection')
  call require(all_finite(sync1, dt, flipk, syncshort, snr2, dt2), &
       'inactive polarization data produces finite correlator outputs')
  print '(a)', 'ccf65 correlator tests passed'

contains

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require

  subroutine test_fft_roundtrip()
    real :: input(JT65_NFFT), output(JT65_NFFT)
    complex :: spectrum(0:JT65_NH)
    integer :: k

    do k = 1, JT65_NFFT
       input(k) = sin(0.013*real(k)) + 0.25*cos(0.071*real(k))
    end do

    call r2c_legacy(input, spectrum)
    call c2r_legacy(spectrum, output)
    call require(maxval(abs(output/JT65_NFFT - input)) < 1.0e-5, &
         'legacy packed FFT round trip preserves real samples')
  end subroutine test_fft_roundtrip

  logical function all_finite(v1, v2, v3, v4, v5, v6)
    real, intent(in) :: v1, v2, v3, v4, v5, v6

    all_finite = ieee_is_finite(v1) .and. ieee_is_finite(v2) .and. &
         ieee_is_finite(v3) .and. ieee_is_finite(v4) .and. &
         ieee_is_finite(v5) .and. ieee_is_finite(v6)
  end function all_finite

  subroutine make_sync_plane(ss_plane, polarity)
    real, intent(out) :: ss_plane(4,322)
    real, intent(in) :: polarity
    integer, parameter :: npr(126) = [ &
         1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0, &
         0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1, &
         0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1, &
         0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1, &
         1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1, &
         0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1, &
         1,1,1,1,1,1]
    integer :: i
    real :: target_sum

    ! Build a non-negative spectrum whose adjacent-bin sums have one lag only.
    ss_plane = 0.0
    ss_plane(1,1) = 2.0
    do i = 1, nhsym - 1
       target_sum = 4.0
       if (mod(i,2) == 1 .and. (i+1)/2 <= size(npr)) then
          target_sum = target_sum + polarity * real(2*npr((i+1)/2) - 1)
       endif
       ss_plane(1,i+1) = target_sum - ss_plane(1,i)
    end do
  end subroutine make_sync_plane

end program test_ccf65_correlator
