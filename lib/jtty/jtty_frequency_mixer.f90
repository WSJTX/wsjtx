module jtty_frequency_mixer
  use, intrinsic :: iso_fortran_env, only: real32
  implicit none
  private

  type, public :: jtty_mixer_state
    private
    complex(real32) :: oscillator, step
  end type jtty_mixer_state

  public :: jtty_mixer_start, jtty_mix_samples

contains

  subroutine jtty_mixer_start(state, frequency, sample_rate, skip_samples)
    type(jtty_mixer_state), intent(out) :: state
    real(real32), intent(in) :: frequency, sample_rate
    integer, intent(in) :: skip_samples
    real(real32), parameter :: TWO_PI = 6.283185307_real32
    real(real32) :: phase_step
    integer :: sample

    phase_step = frequency*(TWO_PI/sample_rate)
    state%step = cmplx(cos(phase_step),sin(phase_step),real32)
    state%oscillator = 1.0_real32
    ! Retain twkfreq's accumulated oscillator rounding before the first sample.
    do sample = 1, skip_samples
      state%oscillator = state%oscillator*state%step
    end do
  end subroutine jtty_mixer_start

  subroutine jtty_mix_samples(state, samples, mixed)
    type(jtty_mixer_state), intent(inout) :: state
    complex(real32), intent(in) :: samples(:)
    complex(real32), intent(out) :: mixed(:)
    integer :: sample
    complex(real32) :: oscillator, step

    oscillator = state%oscillator
    step = state%step
    do sample = 1, size(samples)
      oscillator = oscillator*step
      mixed(sample) = oscillator*samples(sample)
    end do
    state%oscillator = oscillator
  end subroutine jtty_mix_samples

end module jtty_frequency_mixer
