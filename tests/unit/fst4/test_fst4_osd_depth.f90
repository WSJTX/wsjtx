program test_fst4_osd_depth

  use jtty_fec, only: osd80_32
  implicit none

  integer, parameter :: n = 240
  real :: llr(n)
  integer(kind=1) :: apmask(n)
  integer(kind=1) :: message_zero(74), message_negative(74)
  integer(kind=1) :: message101_zero(101), message101_negative(101)
  integer(kind=1) :: cw_zero(n), cw_negative(n)
  real :: llr128(128), llr80(80)
  integer(kind=1) :: apmask128(128)
  integer(kind=1) :: message77_zero(77), message77_negative(77)
  integer(kind=1) :: message32_zero(32), message32_negative(32)
  integer(kind=1) :: cw128_zero(128), cw128_negative(128)
  integer(kind=1) :: cw80_zero(80), cw80_negative(80)
  integer :: nhard_zero, nhard_negative
  real :: dmin_zero, dmin_negative
  integer :: i
  integer :: nhard128_zero, nhard128_negative
  integer :: nhard80_zero, nhard80_negative
  real :: dmin128_zero, dmin128_negative
  real :: dmin80_zero, dmin80_negative
  external :: osd240_74, osd240_101, osd128_90

  do i = 1, n
     llr(i) = real(mod(7*i, 23) - 11)
  end do
  apmask = 0

  call osd240_74(llr, 50, apmask, 0, message_zero, cw_zero, nhard_zero, dmin_zero)
  call osd240_74(llr, 50, apmask, -1, message_negative, cw_negative, &
       nhard_negative, dmin_negative)
  call require(all(message_zero == message_negative), &
       'negative (240,74) OSD depth matches zero depth')
  call require(all(cw_zero == cw_negative), &
       'negative (240,74) OSD depth preserves the codeword')
  call require(nhard_zero == nhard_negative .and. dmin_zero == dmin_negative, &
       'negative (240,74) OSD depth preserves metrics')

  call osd240_101(llr, 77, apmask, 0, message101_zero, cw_zero, &
       nhard_zero, dmin_zero)
  call osd240_101(llr, 77, apmask, -1, message101_negative, cw_negative, &
       nhard_negative, dmin_negative)
  call require(all(message101_zero == message101_negative), &
       'negative (240,101) OSD depth matches zero depth')
  call require(all(cw_zero == cw_negative), &
       'negative (240,101) OSD depth preserves the codeword')
  call require(nhard_zero == nhard_negative .and. dmin_zero == dmin_negative, &
       'negative (240,101) OSD depth preserves metrics')

  do i = 1, 128
     llr128(i) = real(mod(11*i, 29) - 14)
  end do
  apmask128 = 0
  call osd128_90(llr128, apmask128, 0, message77_zero, cw128_zero, &
       nhard128_zero, dmin128_zero)
  call osd128_90(llr128, apmask128, -1, message77_negative, cw128_negative, &
       nhard128_negative, dmin128_negative)
  call require(all(message77_zero == message77_negative), &
       'negative (128,90) OSD depth matches zero depth')
  call require(all(cw128_zero == cw128_negative), &
       'negative (128,90) OSD depth preserves the codeword')
  call require(nhard128_zero == nhard128_negative .and. &
       dmin128_zero == dmin128_negative, &
       'negative (128,90) OSD depth preserves metrics')

  do i = 1, 80
     llr80(i) = real(mod(13*i, 31) - 15)
  end do
  call osd80_32(llr80, 0, message32_zero, cw80_zero, nhard80_zero, dmin80_zero)
  call osd80_32(llr80, -1, message32_negative, cw80_negative, &
       nhard80_negative, dmin80_negative)
  call require(all(message32_zero == message32_negative), &
       'negative (80,42) OSD depth matches zero depth')
  call require(all(cw80_zero == cw80_negative), &
       'negative (80,42) OSD depth preserves the codeword')
  call require(nhard80_zero == nhard80_negative .and. &
       dmin80_zero == dmin80_negative, &
       'negative (80,42) OSD depth preserves metrics')

  print '(a)', 'FST4 OSD depth tests passed'

contains

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require

end program test_fst4_osd_depth
