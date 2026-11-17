program test_jtty_frequency_mixer
  use, intrinsic :: iso_fortran_env, only: real32, real64
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use jtty_frequency_mixer, only: jtty_mixer_state, jtty_mixer_start, jtty_mix_samples
  implicit none
  integer, parameter :: lengths(4) = [120,160,192,240]
  real(real32), parameter :: frequencies(7) = [-2850.25,-1650.0,-1350.1,-0.5,0.0,100.25,2999.75]
  integer, parameter :: sample_count = 20000
  complex(real32) :: samples(0:sample_count-1), reference(0:sample_count-1)
  complex(real32) :: mixed(0:sample_count-1)
  type(jtty_mixer_state) :: mixer
  real(real32) :: coefficients(3)
  integer :: sample, frequency, length, first, sync_end, payload_end, offset
  integer :: offsets(5)

  do sample = 0, sample_count-1
    samples(sample) = cmplx(sin(real(sample,real32)*0.17_real32), &
         cos(real(sample,real32)*0.31_real32),real32)
  end do
  do frequency = 1, size(frequencies)
    coefficients = 0.0_real32
    coefficients(1) = frequencies(frequency)
    call twkfreq(samples,reference,sample_count,6000.0_real32,coefficients)
    do length = 1, size(lengths)
      offsets = [0,1,23,59*lengths(length)/4,sample_count-13*lengths(length)]
      do offset = 1, size(offsets)
        first = offsets(offset)
        sync_end = first+13*lengths(length)
        payload_end = min(sample_count,sync_end+46*lengths(length))
        mixed = cmplx(-99.0_real32,99.0_real32,real32)
        call jtty_mixer_start(mixer,frequencies(frequency),6000.0_real32,first)
        call jtty_mix_samples(mixer,samples(first:sync_end-1),mixed(first:sync_end-1))
        call jtty_mix_samples(mixer,samples(sync_end:sync_end-1),mixed(sync_end:sync_end-1))
        call jtty_mix_samples(mixer,samples(sync_end:payload_end-1),mixed(sync_end:payload_end-1))
        call check_samples(first,payload_end)
        if (any(mixed(:first-1) /= cmplx(-99.0_real32,99.0_real32,real32)) .or. &
             any(mixed(payload_end:) /= cmplx(-99.0_real32,99.0_real32,real32))) &
             error stop 'JTTY mixer modified samples outside requested range'
        call jtty_mixer_start(mixer,frequencies(frequency),6000.0_real32,sync_end)
        call jtty_mix_samples(mixer,samples(sync_end:payload_end-1),mixed(sync_end:payload_end-1))
        call check_samples(sync_end,payload_end)
      end do
    end do
  end do

contains

  subroutine check_samples(first,last)
    integer, intent(in) :: first,last
    real(real64), parameter :: roundoff = real(epsilon(1.0_real32),real64)
    complex(real64) :: step, left, right
    real(real32) :: phase_step
    real(real64) :: budget, scale
    integer :: index

    if (any(.not.ieee_is_finite(real(mixed(first:last-1)))) .or. &
         any(.not.ieee_is_finite(aimag(mixed(first:last-1))))) &
         error stop 'JTTY mixer produced non-finite samples'
    if (any(.not.ieee_is_finite(real(reference(first:last-1)))) .or. &
         any(.not.ieee_is_finite(aimag(reference(first:last-1))))) &
         error stop 'JTTY mixer reference produced non-finite samples'
    phase_step = frequencies(frequency)*(6.283185307_real32/6000.0_real32)
    step = cmplx(cos(phase_step),sin(phase_step),real64)
    do index = first,last-1
      ! Recurrence rounding accumulates with the absolute sample position.
      budget = 4.0_real64*real(index+2,real64)*roundoff
      scale = max(abs(cmplx(samples(index),kind=real64)),abs(cmplx(reference(index),kind=real64)))
      if (abs(cmplx(mixed(index),kind=real64)-cmplx(reference(index),kind=real64)) > &
           budget/(1.0_real64-budget)*scale) &
           error stop 'JTTY mixer exceeds accumulated rounding budget'
      if (index == first) cycle
      ! A local phase-step check keeps the accumulated bound from hiding discontinuities.
      left = cmplx(mixed(index),kind=real64)*cmplx(samples(index-1),kind=real64)
      right = step*cmplx(mixed(index-1),kind=real64)*cmplx(samples(index),kind=real64)
      if (abs(left-right) > 16.0_real64*roundoff*max(abs(left),abs(right))) &
           error stop 'JTTY mixer phase step exceeds rounding budget'
    end do
  end subroutine check_samples

end program test_jtty_frequency_mixer
