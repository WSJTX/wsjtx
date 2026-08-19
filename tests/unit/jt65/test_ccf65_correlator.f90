program test_ccf65_correlator

  use ccf65_legacy_mod, only: ccf65
  implicit none

  integer, parameter :: nhsym = 254
  real, parameter :: expected_sync = 12.4682
  real :: ss(4,322)
  integer :: i
  real :: sync1, dt, flipk, syncshort, snr2, dt2
  integer :: ipol1, ipol2

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

  subroutine make_sync_plane(ss_plane, polarity)
    real, intent(out) :: ss_plane(4,322)
    real, intent(in) :: polarity
    integer, parameter :: npr(126) = [ &
         1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0, &
         0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1, &
         0,1,1,0,1,0,1,1,0,0,0,1,1,0,1,0,1,1,0,1, &
         0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1, &
         1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1, &
         0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1, &
         1,1,1,1,1,1]
    integer :: symbol

    ss_plane = 0.0
    do symbol = 1, size(npr)
       ss_plane(1,2*symbol) = polarity * real(2*npr(symbol) - 1)
    end do
  end subroutine make_sync_plane

end program test_ccf65_correlator
