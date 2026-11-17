program test_qmap_q65_handoff
  use iso_fortran_env, only: int16, real64
  use cacb_mod, only: ca, init_cacb
  use qmap_q65_samples_mod, only: build_q65_samples
  implicit none

  integer, parameter :: nfft2=336000, nfft1=5376000, k0=2600000, sample_rate=12000
  real, parameter :: df=96000.0/nfft1
  real, parameter :: filter_amplitude=100000000.0
  integer(int16), allocatable :: desired(:), wave(:)
  complex, allocatable :: spectrum(:)
  integer :: i,j
  real(real64) :: t, filter_reference_rms

  allocate(desired(720000),wave(720000),spectrum(0:nfft2))
  desired=0
  do i=1,27*sample_rate
     t=real(i-1,real64)/real(sample_rate,real64)
     desired(i)=int(nint(100.0_real64*sin(2.0_real64*acos(-1.0_real64)*1000.0_real64*t)),int16)
  enddo
  do i=31*sample_rate+1,55*sample_rate
     t=real(i-1,real64)/real(sample_rate,real64)
     desired(i)=int(nint(100.0_real64*sin(2.0_real64*acos(-1.0_real64)*1200.0_real64*t)),int16)
  enddo

  do i=0,nfft2-1
     j=nfft2-1-i
     spectrum(j)=cmplx(real(desired(2*i+2)),real(desired(2*i+1)))
  enddo
  spectrum(nfft2)=0.
  call four2a(spectrum,2*nfft2,1,-1,0)
  call init_cacb(nfft1)
  ca=0.
  ca(k0:k0+nfft2-1)=0.5*spectrum(0:nfft2-1)

  call build_q65_samples(k0,nfft2,df,0,wave)
  call expect_samples(wave,desired,2*sample_rate+1,3*sample_rate,0, &
       'first half preserves 1000 Hz level and phase')
  call require(mean_power(wave,27*sample_rate+1,30*sample_rate) < 1.0_real64, &
       'first-half guard is quiet')
  call require(all(wave(2*nfft2+1:)==0), '56-second extraction has a zero tail')
  call require(maxval(abs(int(wave))) < 1000, 'samples do not clip')

  call build_q65_samples(k0,nfft2,df,1,wave)
  call require(mean_power(wave,sample_rate/4+1,3*sample_rate/4) < 1.0_real64, &
       'second-half onset begins with quiet samples')
  call expect_samples(wave,desired,sample_rate+sample_rate/4+1,sample_rate+3*sample_rate/4, &
       30*sample_rate,'second-half selection preserves 1200 Hz level, phase, and timing')
  call require(mean_power(wave,25*sample_rate+1,30*sample_rate) < 1.0_real64, &
       'second-half tail is quiet')
  call require(maxval(abs(int(wave))) < 1000, 'selected samples do not clip')

  call set_filter_tone(1000)
  call build_q65_samples(k0,nfft2,df,0,wave)
  filter_reference_rms=sqrt(mean_power(wave,1,sample_rate))
  call expect_filter(250,0.5_real64)
  call expect_filter(2750,0.5_real64)
  call expect_filter(3500,0.0_real64)

  deallocate(desired,wave)
  print '(a)', 'QMAP Q65 sample handoff contracts passed.'

contains

  subroutine expect_samples(actual,expected,first,last,offset,description)
    integer(int16), intent(in) :: actual(:),expected(:)
    integer, intent(in) :: first,last,offset
    character(len=*), intent(in) :: description
    integer :: index

    do index=first,last
       if(abs(int(actual(index))-int(expected(index+offset)))>2) then
          print '(a,i0)', 'FAIL: '//description//' at sample ',index
          error stop 1
       endif
    enddo
  end subroutine expect_samples

  subroutine set_filter_tone(frequency)
    integer, intent(in) :: frequency
    ca=0.
    ca(k0+nint(real(frequency)/df))=cmplx(filter_amplitude,0.0)
  end subroutine set_filter_tone

  subroutine expect_filter(frequency,expected_ratio)
    integer, intent(in) :: frequency
    real(real64), intent(in) :: expected_ratio
    real(real64) :: ratio
    character(len=16) :: label

    call set_filter_tone(frequency)
    call build_q65_samples(k0,nfft2,df,0,wave)
    ratio=sqrt(mean_power(wave,1,sample_rate))/filter_reference_rms
    write(label,'(i0,a)') frequency,' Hz'
    call require(abs(ratio-expected_ratio)<0.01_real64,'filter response at '//trim(label))
  end subroutine expect_filter

  real(real64) function mean_power(samples,first,last)
    integer(int16), intent(in) :: samples(:)
    integer, intent(in) :: first,last
    integer :: index
    mean_power=0.0_real64
    do index=first,last
       mean_power=mean_power+real(samples(index),real64)**2
    enddo
    mean_power=mean_power/real(last-first+1,real64)
  end function mean_power

  subroutine require(condition,description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    endif
  end subroutine require
end program test_qmap_q65_handoff
