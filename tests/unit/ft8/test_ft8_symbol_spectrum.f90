program test_ft8_symbol_spectrum
  use ft8_symbol_spectrum, only: ft8_symbol_spectra
  use ieee_arithmetic, only: ieee_is_finite
  implicit none
  complex :: input(32),work(32),reverse_work(32),forward(0:7),reversed(0:7)
  real :: maximum_error,error,scale,twopi
  integer :: i,case_id,npatience,nthreads
  common /patience/ npatience,nthreads

  npatience=1
  nthreads=1
  maximum_error=0.
  twopi=8.*atan(1.)
  do case_id=1,68
    input=(0.,0.)
    if(case_id.le.32) then
      input(case_id)=(1.,0.5)
    else if(case_id.le.64) then
      do i=1,32
        input(i)=cmplx(cos(twopi*(case_id-33)*real(i-1)/32), &
            sin(twopi*(case_id-33)*real(i-1)/32))
      enddo
    else if(case_id.gt.65) then
      do i=1,32
        input(i)=cmplx(sin(0.217*i),cos(0.313*i))
      enddo
      if(case_id.eq.67) input=input*0.001
      if(case_id.eq.68) then
        input(1)=input(1)*1.9
        input(32)=input(32)*1.9
        scale=sqrt(abs(input(1)))/sqrt(abs(input(32)))
        if(scale.gt.1.) then
          input(32)=input(32)*scale
        else
          input(1)=input(1)/scale
        endif
      endif
    endif

    work=input
    call ft8_symbol_spectra(work,forward,reversed)
    if(any(.not.ieee_is_finite(real(forward))) .or. &
        any(.not.ieee_is_finite(aimag(forward))) .or. &
        any(.not.ieee_is_finite(real(reversed))) .or. &
        any(.not.ieee_is_finite(aimag(reversed)))) error stop 'non-finite symbol spectrum'
    work=input
    reverse_work=conjg(input(32:1:-1))
    call four2avar(work,32,1,-1,1)
    call four2avar(reverse_work,32,1,-1,1)
    scale=max(1.,maxval(abs(work(1:8))),maxval(abs(reverse_work(1:8))))
    error=max(maxval(abs(forward-work(1:8))),maxval(abs(reversed-reverse_work(1:8))))/scale
    maximum_error=max(maximum_error,error)
    if(error.gt.5.e-6) error stop 'symbol spectra differ from independent forward and reversed FFTs'
  enddo
  print '(a,es12.4)', 'maximum normalized spectral error=',maximum_error
  call four2avar(work,-1,1,1,1)
end program test_ft8_symbol_spectrum
