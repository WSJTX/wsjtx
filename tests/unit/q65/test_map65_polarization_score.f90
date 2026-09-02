program test_map65_polarization_score
  use iso_fortran_env, only: real64
  use map65_polarization_score_mod, only: generalized_polarization_score,polarization_matrix
  implicit none

  real(real64), parameter :: pi=acos(-1.0_real64)
  real(real64) :: angle,combine_angle,expected,g00,g01,g11,score
  real(real64) :: projections(4),u0,u1
  logical :: valid

  projections=[3.0_real64,4.0_real64,5.0_real64,4.0_real64]
  call polarization_matrix(projections,g00,g11,g01)
  call assert_close(g00,3.0_real64,0.0_real64,'G00 reconstruction')
  call assert_close(g11,5.0_real64,0.0_real64,'G11 reconstruction')
  call assert_close(g01,0.0_real64,0.0_real64,'G01 reconstruction')

  call rank_one(22.5_real64,3.0_real64,g00,g11,g01)
  call generalized_polarization_score(g00,g11,g01,1.0_real64,1.0_real64,0.0_real64, &
       score,angle,combine_angle,valid)
  if (.not.valid) error stop 'balanced background was rejected'
  call assert_close(score,3.0_real64,1.0e-12_real64,'balanced rank-one score')
  call assert_angle(angle,22.5_real64,1.0e-10_real64,'balanced physical angle')
  call assert_angle(combine_angle,22.5_real64,1.0e-10_real64,'balanced combining angle')
  expected=10.0_real64*log10(1.0_real64/cos(22.5_real64*pi/180.0_real64)**2)
  call assert_close(expected,0.687693_real64,1.0e-6_real64,'worst-angle scalloping')

  call rank_one(30.0_real64,2.0_real64,g00,g11,g01)
  call generalized_polarization_score(g00,g11,g01,4.0_real64,1.0_real64,0.0_real64, &
       score,angle,combine_angle,valid)
  u0=cos(30.0_real64*pi/180.0_real64)
  u1=sin(30.0_real64*pi/180.0_real64)
  expected=2.0_real64*(u0*u0/4.0_real64+u1*u1)
  call assert_close(score,expected,1.0e-12_real64,'unequal-noise score')
  call assert_angle(angle,30.0_real64,1.0e-10_real64,'unequal-noise physical angle')
  call assert_angle(combine_angle,atan2(u1,u0/4.0_real64)*180.0_real64/pi, &
       1.0e-10_real64,'unequal-noise combining angle')
  call rank_one(40.0_real64,1.7_real64,g00,g11,g01)
  call generalized_polarization_score(g00,g11,g01,2.0_real64,3.0_real64,0.8_real64, &
       score,angle,combine_angle,valid)
  u0=cos(40.0_real64*pi/180.0_real64)
  u1=sin(40.0_real64*pi/180.0_real64)
  expected=1.7_real64*(3.0_real64*u0*u0-1.6_real64*u0*u1+2.0_real64*u1*u1)/5.36_real64
  call assert_close(score,expected,1.0e-12_real64,'correlated-noise score')
  call assert_angle(angle,40.0_real64,1.0e-10_real64,'correlated-noise physical angle')

  call generalized_polarization_score(9.0_real64*g00,6.0_real64*g11, &
       sqrt(54.0_real64)*g01,18.0_real64,18.0_real64,sqrt(54.0_real64)*0.8_real64, &
       score,angle,combine_angle,valid)
  call assert_close(score,expected,1.0e-11_real64,'receiver-gain invariance')
  call assert_angle(angle,atan2(sqrt(6.0_real64)*u1,3.0_real64*u0)*180.0_real64/pi, &
       1.0e-10_real64,'receiver-gain physical angle')

  call generalized_polarization_score(g00,g11,g01,1.0_real64,1.0_real64,1.0_real64, &
       score,angle,combine_angle,valid)
  if (.not.valid) error stop 'singular background was rejected'
  if (.not.(score >= 0.0_real64 .and. score < huge(score))) &
       error stop 'regularized score was not finite'

  call generalized_polarization_score(g00,g11,g01,-1.0_real64,1.0_real64,0.0_real64, &
       score,angle,combine_angle,valid)
  if (valid) error stop 'invalid background was accepted'

  call generalized_polarization_score(g00,g11,g01,1.0_real64,1.0_real64,1.1_real64, &
       score,angle,combine_angle,valid)
  if (valid) error stop 'invalid background correlation was accepted'

contains

  subroutine rank_one(degrees,amplitude,a00,a11,a01)
    real(real64), intent(in) :: degrees,amplitude
    real(real64), intent(out) :: a00,a11,a01
    real(real64) :: x,y

    x=cos(degrees*pi/180.0_real64)
    y=sin(degrees*pi/180.0_real64)
    a00=amplitude*x*x
    a11=amplitude*y*y
    a01=amplitude*x*y
  end subroutine rank_one

  subroutine assert_close(actual,wanted,tolerance,label)
    real(real64), intent(in) :: actual,wanted,tolerance
    character(len=*), intent(in) :: label

    if (abs(actual-wanted) > tolerance) then
       print '(a,2es24.15)', trim(label)//': ',actual,wanted
       error stop 'numeric assertion failed'
    endif
  end subroutine assert_close

  subroutine assert_angle(actual,wanted,tolerance,label)
    real(real64), intent(in) :: actual,wanted,tolerance
    character(len=*), intent(in) :: label
    real(real64) :: difference

    difference=abs(modulo(actual-wanted+90.0_real64,180.0_real64)-90.0_real64)
    call assert_close(difference,0.0_real64,tolerance,label)
  end subroutine assert_angle

end program test_map65_polarization_score
