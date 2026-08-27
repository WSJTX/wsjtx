program test_jt65_xcor_silence

  implicit none

  integer, parameter :: lag1 = -32, lag2 = 82, nhmax = 3413, nsmax = 552
  real :: ss(nsmax,nhmax), ccf(lag1:lag2), ccf0, flip
  integer :: lagpk

  common /sync/ ss

  call setup65
  ss = 0.0
  lagpk = huge(0)

  call xcor(1,nsmax,126,lag1,lag2,ccf,ccf0,lagpk,flip,0.0,0)

  call require(lagpk == lag1, 'silence returns the first valid lag')
  call require(all(ccf == 0.0), 'silence produces zero correlation')
  call require(ccf0 == 0.0, 'silence produces a zero peak')
  call require(flip == 1.0, 'silence keeps normal polarity')

contains

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require

end program test_jt65_xcor_silence
