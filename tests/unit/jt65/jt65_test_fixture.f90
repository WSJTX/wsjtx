module jt65_test_fixture

  use iso_fortran_env, only: int64, real32, real64
  implicit none

  integer, parameter :: jt65_sample_rate = 12000
  integer, parameter :: jt65_sample_count = 60 * jt65_sample_rate
  integer, parameter :: jt65_symbol_count = 126
  real(real64), parameter :: jt65_baud = 11025.0_real64 / 4096.0_real64
  real(real64), parameter :: jt65_pi = 3.1415926535897932384626433832795_real64
  real(real64), parameter :: jt65_amplitude = 28000.0_real64

contains

  subroutine make_jt65_wave(samples, tones, mode65, f0, amplitude, noise_amplitude, noise_seed)
    real(real32), intent(out) :: samples(:)
    integer, intent(in) :: tones(jt65_symbol_count)
    integer, intent(in) :: mode65
    real(real64), intent(in) :: f0
    real(real64), intent(in), optional :: amplitude
    real(real64), intent(in), optional :: noise_amplitude
    integer, intent(in), optional :: noise_seed
    real(real64) :: signal_amplitude
    real(real64) :: noise_scale, sample_noise, u1, u2
    real(real64) :: phase, frequency, samples_per_symbol
    integer(int64) :: noise_state
    integer :: sample, signal_sample, output_sample, symbol

    signal_amplitude = jt65_amplitude
    if (present(amplitude)) signal_amplitude = amplitude
    noise_scale = 0.0_real64
    if (present(noise_amplitude)) noise_scale = noise_amplitude
    noise_state = 104729_int64
    if (present(noise_seed)) noise_state = 104729_int64 + int(noise_seed, int64)
    if (noise_scale == 0.0_real64) then
       samples = 0.0_real32
    else
       do sample = 1, size(samples)
          noise_state = modulo(16807_int64 * noise_state, 2147483647_int64)
          u1 = max(real(noise_state, real64) / 2147483647.0_real64, tiny(1.0_real64))
          noise_state = modulo(16807_int64 * noise_state, 2147483647_int64)
          u2 = real(noise_state, real64) / 2147483647.0_real64
          sample_noise = sqrt(-2.0_real64 * log(u1)) * cos(2.0_real64 * jt65_pi * u2)
          samples(sample) = real(noise_scale * sample_noise, real32)
       end do
    end if
    phase = 0.0_real64
    samples_per_symbol = real(jt65_sample_rate, real64) / jt65_baud
    do signal_sample = 1, size(samples) - jt65_sample_rate
       symbol = int(floor(real(signal_sample, real64) / samples_per_symbol)) + 1
       if (symbol > jt65_symbol_count) exit
       frequency = f0 + real(tones(symbol), real64) * jt65_baud * real(mode65, real64)
       phase = phase + 2.0_real64 * jt65_pi * frequency / real(jt65_sample_rate, real64)
       output_sample = jt65_sample_rate + signal_sample
       samples(output_sample) = samples(output_sample) + real(signal_amplitude * sin(phase), real32)
    end do
  end subroutine make_jt65_wave

  subroutine make_map65_wave(dd, tones, mode65, f0)
    real(real32), intent(out) :: dd(:,:)
    integer, intent(in) :: tones(jt65_symbol_count)
    integer, intent(in) :: mode65
    real(real64), intent(in) :: f0
    real(real64) :: phase, frequency, samples_per_symbol
    integer :: signal_sample, output_sample, symbol
    integer, parameter :: map65_sample_rate = 96000

    dd = 0.0_real32
    phase = 0.0_real64
    samples_per_symbol = real(map65_sample_rate, real64) / jt65_baud
    do signal_sample = 1, size(dd, 2) - map65_sample_rate
       symbol = int(floor(real(signal_sample, real64) / samples_per_symbol)) + 1
       if (symbol > jt65_symbol_count) exit
       frequency = f0 + real(tones(symbol), real64) * jt65_baud * real(mode65, real64)
       phase = phase + 2.0_real64 * jt65_pi * frequency / real(map65_sample_rate, real64)
       output_sample = map65_sample_rate + signal_sample
       dd(1, output_sample) = real(jt65_amplitude * cos(phase), real32)
       dd(2, output_sample) = real(-jt65_amplitude * sin(phase), real32)
    end do
  end subroutine make_map65_wave

end module jt65_test_fixture
