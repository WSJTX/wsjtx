program test_fst4_osd_depth

  implicit none

  integer, parameter :: n = 240
  real :: llr(n)
  integer(kind=1) :: apmask(n)
  integer(kind=1) :: message_zero(74), message_negative(74)
  integer(kind=1) :: message101_zero(101), message101_negative(101)
  integer(kind=1) :: cw_zero(n), cw_negative(n)
  integer :: nhard_zero, nhard_negative
  real :: dmin_zero, dmin_negative
  integer :: i
  external :: osd240_74, osd240_101

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
