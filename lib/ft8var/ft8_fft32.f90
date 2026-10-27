module ft8_fft32
  implicit none
  private
  public :: fft32_first_eight

  complex, parameter :: eighth_root=cmplx(0.7071067812,-0.7071067812)
  complex, parameter :: twiddle1(0:7)=[ &
    cmplx(1.0,0.0), cmplx(0.9807852804,-0.1950903220), &
    cmplx(0.9238795325,-0.3826834324), cmplx(0.8314696123,-0.5555702330), &
    cmplx(0.7071067812,-0.7071067812), cmplx(0.5555702330,-0.8314696123), &
    cmplx(0.3826834324,-0.9238795325), cmplx(0.1950903220,-0.9807852804)]
  complex, parameter :: twiddle2(0:7)=[ &
    cmplx(1.0,0.0), cmplx(0.9238795325,-0.3826834324), &
    cmplx(0.7071067812,-0.7071067812), cmplx(0.3826834324,-0.9238795325), &
    cmplx(0.0,-1.0), cmplx(-0.3826834324,-0.9238795325), &
    cmplx(-0.7071067812,-0.7071067812), cmplx(-0.9238795325,-0.3826834324)]
  complex, parameter :: twiddle3(0:7)=[ &
    cmplx(1.0,0.0), cmplx(0.8314696123,-0.5555702330), &
    cmplx(0.3826834324,-0.9238795325), cmplx(-0.1950903220,-0.9807852804), &
    cmplx(-0.7071067812,-0.7071067812), cmplx(-0.9807852804,-0.1950903220), &
    cmplx(-0.9238795325,0.3826834324), cmplx(-0.5555702330,0.8314696123)]

contains

  pure subroutine fft32_first_eight(input,bins)
    complex, intent(in) :: input(32)
    complex, intent(out) :: bins(0:7)
    real, parameter :: twiddle1_real(0:7)=real(twiddle1)
    real, parameter :: twiddle1_imag(0:7)=aimag(twiddle1)
    real, parameter :: twiddle2_real(0:7)=real(twiddle2)
    real, parameter :: twiddle2_imag(0:7)=aimag(twiddle2)
    real, parameter :: twiddle3_real(0:7)=real(twiddle3)
    real, parameter :: twiddle3_imag(0:7)=aimag(twiddle3)
    real :: lane_real(0:1,0:3),lane_imag(0:1,0:3),bins_real(0:7),bins_imag(0:7)
    real :: sum_real(0:3,0:3),sum_imag(0:3,0:3),difference_real(0:3,0:3),difference_imag(0:3,0:3)
    real :: even_sum_real,even_sum_imag,even_difference_real,even_difference_imag
    real :: odd_sum_real,odd_sum_imag,odd_difference_real,odd_difference_imag
    real :: work_real,work_imag
    real :: pair0_real,pair0_imag,pair1_real,pair1_imag,pair2_real,pair2_imag,pair3_real,pair3_imag,base_real,base_imag
    integer :: m,p,q,h

    ! Pair positions four apart before combining the eight-point bins.
    do q=0,3
      do h=0,1
        m=q+4*h
        even_sum_real=real(input(m+1))+real(input(m+17))
        even_sum_imag=aimag(input(m+1))+aimag(input(m+17))
        even_difference_real=real(input(m+1))-real(input(m+17))
        even_difference_imag=aimag(input(m+1))-aimag(input(m+17))
        odd_sum_real=real(input(m+9))+real(input(m+25))
        odd_sum_imag=aimag(input(m+9))+aimag(input(m+25))
        odd_difference_real=0.0*(real(input(m+9))-real(input(m+25)))-(-1.0)*(aimag(input(m+9))-aimag(input(m+25)))
        odd_difference_imag=0.0*(aimag(input(m+9))-aimag(input(m+25)))+(-1.0)*(real(input(m+9))-real(input(m+25)))
        lane_real(h,0)=even_sum_real+odd_sum_real
        lane_imag(h,0)=even_sum_imag+odd_sum_imag
        work_real=even_difference_real+odd_difference_real
        work_imag=even_difference_imag+odd_difference_imag
        lane_real(h,1)=work_real*twiddle1_real(m)-work_imag*twiddle1_imag(m)
        lane_imag(h,1)=work_real*twiddle1_imag(m)+work_imag*twiddle1_real(m)
        work_real=even_sum_real-odd_sum_real
        work_imag=even_sum_imag-odd_sum_imag
        lane_real(h,2)=work_real*twiddle2_real(m)-work_imag*twiddle2_imag(m)
        lane_imag(h,2)=work_real*twiddle2_imag(m)+work_imag*twiddle2_real(m)
        work_real=even_difference_real-odd_difference_real
        work_imag=even_difference_imag-odd_difference_imag
        lane_real(h,3)=work_real*twiddle3_real(m)-work_imag*twiddle3_imag(m)
        lane_imag(h,3)=work_real*twiddle3_imag(m)+work_imag*twiddle3_real(m)
      enddo
      do p=0,3
        sum_real(q,p)=lane_real(0,p)+lane_real(1,p)
        sum_imag(q,p)=lane_imag(0,p)+lane_imag(1,p)
        difference_real(q,p)=lane_real(0,p)-lane_real(1,p)
        difference_imag(q,p)=lane_imag(0,p)-lane_imag(1,p)
      enddo
    enddo

    ! The radix-four branches need only their first two eight-point bins.
    do p=0,3
      pair0_real=sum_real(0,p)
      pair0_imag=sum_imag(0,p)
      pair1_real=sum_real(1,p)
      pair1_imag=sum_imag(1,p)
      pair2_real=sum_real(2,p)
      pair2_imag=sum_imag(2,p)
      pair3_real=sum_real(3,p)
      pair3_imag=sum_imag(3,p)
      bins_real(p)=(pair0_real+pair2_real)+(pair1_real+pair3_real)
      bins_imag(p)=(pair0_imag+pair2_imag)+(pair1_imag+pair3_imag)
      pair0_real=difference_real(0,p)
      pair0_imag=difference_imag(0,p)
      pair1_real=difference_real(1,p)
      pair1_imag=difference_imag(1,p)
      pair2_real=difference_real(2,p)
      pair2_imag=difference_imag(2,p)
      pair3_real=difference_real(3,p)
      pair3_imag=difference_imag(3,p)
      base_real=pair0_real+(0.0*pair2_real-(-1.0)*pair2_imag)
      base_imag=pair0_imag+(0.0*pair2_imag+(-1.0)*pair2_real)
      work_real=pair1_real+(0.0*pair3_real-(-1.0)*pair3_imag)
      work_imag=pair1_imag+(0.0*pair3_imag+(-1.0)*pair3_real)
      bins_real(p+4)=base_real+(real(eighth_root)*work_real-aimag(eighth_root)*work_imag)
      bins_imag(p+4)=base_imag+(real(eighth_root)*work_imag+aimag(eighth_root)*work_real)
    enddo
    bins(0:3)=cmplx(bins_real(0:3),bins_imag(0:3))
    bins(4:7)=cmplx(bins_real(4:7),bins_imag(4:7))
  end subroutine fft32_first_eight

end module ft8_fft32
