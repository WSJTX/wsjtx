program test_map65_astro
  use iso_fortran_env, only: real64
  use astro_mod, only: astro, nt144
  implicit none

  integer :: ntsky_144, ntsky_1296
  real :: db_moon_144, degradation_144
  real :: db_moon_1296, degradation_1296
  real(real64) :: expected_tsky, expected_degradation

  call run_astro(144, ntsky_144, db_moon_144, degradation_144)
  call require(any(int(nt144) == ntsky_144), &
               '144 MHz sky temperature comes from the lookup table')

  call run_astro(1296, ntsky_1296, db_moon_1296, degradation_1296)
  expected_tsky = (real(ntsky_144, real64) - 2.7_real64) * &
                  (144.0_real64 / 1296.0_real64)**2.6_real64 + 2.7_real64
  call require(ntsky_1296 == nint(expected_tsky), &
               'sky temperature scales from 144 MHz to the selected band')

  expected_degradation = -10.0_real64 * log10( &
      (real(ntsky_144, real64) + 80.0_real64) / &
      (13.0_real64 * (408.0_real64 / 144.0_real64)**2.6_real64 + 80.0_real64)) + &
      real(db_moon_144, real64)
  call require(abs(real(degradation_144, real64) - expected_degradation) < 1.0e-4_real64, &
               'degradation uses the cold-sky temperature for the selected band')

  print '(a)', 'MAP65 Astro tests passed.'

contains

  subroutine run_astro(frequency_mhz, ntsky, db_moon, degradation)
    integer, intent(in) :: frequency_mhz
    integer, intent(out) :: ntsky
    real, intent(out) :: db_moon, degradation
    real :: az_sun, el_sun, az_moon, el_moon
    real :: doppler00, doppler, ra_moon, dec_moon, hour_angle
    real :: semidiameter, polarization_offset, nonreciprocal_loss
    real :: day, longitude, latitude, local_sidereal_time

    call astro(2025, 1, 15, 12.0, frequency_mhz, 'FN20rx', 2, 0.0, &
               az_sun, el_sun, az_moon, el_moon, ntsky, doppler00, doppler, &
               db_moon, ra_moon, dec_moon, hour_angle, degradation, semidiameter, &
               polarization_offset, nonreciprocal_loss, day, longitude, latitude, &
               local_sidereal_time)
    call astro(2025, 1, 15, 12.0, frequency_mhz, 'FN20rx', 1, 0.0, &
               az_sun, el_sun, az_moon, el_moon, ntsky, doppler00, doppler, &
               db_moon, ra_moon, dec_moon, hour_angle, degradation, semidiameter, &
               polarization_offset, nonreciprocal_loss, day, longitude, latitude, &
               local_sidereal_time)
  end subroutine run_astro

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
      print '(a)', 'FAIL: '//description
      error stop 1
    end if
  end subroutine require
end program test_map65_astro
