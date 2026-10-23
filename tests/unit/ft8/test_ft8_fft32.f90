program test_ft8_fft32
  use iso_fortran_env, only: int64,real64
  use ieee_arithmetic, only: ieee_is_finite
  use ft8_fft32, only: fft32_first_eight
  implicit none
  complex :: input(32),bins(0:7)
  complex(real64) :: basis(0:7,32),expected(0:7)
  real(real64) :: twopi,angle,error,maximum_error,scale
  real :: re,im
  integer(int64) :: state
  integer :: i,k,case_id

  twopi=8*atan(1.0_real64)
  do i=1,32
    do k=0,7
      angle=-twopi*k*real(i-1,real64)/32
      basis(k,i)=cmplx(cos(angle),sin(angle),kind=real64)
    enddo
  enddo
  state=20261012_int64
  maximum_error=0.
  do case_id=1,10033
    input=(0.,0.)
    if(case_id.le.32) then
      input(case_id)=(1.,0.5)
    else if(case_id.le.10032) then
      do i=1,32
        state=modulo(48271_int64*state,2147483647_int64)
        re=2*real(state)/2147483647.-1
        state=modulo(48271_int64*state,2147483647_int64)
        im=2*real(state)/2147483647.-1
        input(i)=cmplx(re,im)
      enddo
    endif
    call fft32_first_eight(input,bins)
    if(any(.not.ieee_is_finite(real(bins))) .or. &
        any(.not.ieee_is_finite(aimag(bins)))) error stop 'non-finite FFT32 bins'
    do k=0,7
      expected(k)=sum(cmplx(input,kind=real64)*basis(k,:))
    enddo
    scale=max(1.0_real64,sum(abs(cmplx(input,kind=real64))))
    error=maxval(abs(cmplx(bins,kind=real64)-expected))/scale
    maximum_error=max(maximum_error,error)
    if(error.gt.1.e-6_real64) error stop 'FFT32 differs from double-precision DFT'
  enddo
  print '(a,es12.4)', 'maximum normalized FFT32 error=',maximum_error
end program test_ft8_fft32
