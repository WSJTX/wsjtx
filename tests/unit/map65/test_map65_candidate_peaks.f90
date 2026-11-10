program test_map65_candidate_peaks
  ! get_candidates ranks the sync-correlation bins and walks down the ranking.
  ! Two strong JT65B signals each score highly in a hundred or more bins, so a
  ! walk limited to the top 100 ranked bins never reaches a much weaker Q65
  ! signal elsewhere in the band. The Q65 signal must still become a candidate.
  use iso_fortran_env, only: int8, real32, real64
  use cgen65_mod, only: cgen65
  use datcom_ptrs_mod, only: dd, ss, savg
  use decodes_mod, only: decodes_init
  use ftninit_mod, only: ftninit
  use gen_q65_cwave_mod, only: gen_q65_cwave
  use npar_ptrs_mod, only: idphi, nfft_active, nsmax_active, set_runtime_params
  use symspec_mod, only: symspec
  use timer_impl, only: init_timer, fini_timer
  use wideband_sync, only: candidate, get_candidates, init_wideband_sync, &
       MAX_CANDIDATES, nkhz_center
  implicit none

  integer, parameter :: sample_rate = 96000, fft_size = 32768
  real(real64), parameter :: twopi = 6.283185307179586d0
  real(real32), parameter :: jt65_amplitude = 6.0
  real(real32), parameter :: q65_amplitude = 0.3
  character(len=24), parameter :: q65_message = 'W1AAA K2BBB EM00'
  complex(real32), allocatable :: waveform(:), jt65_wave(:)
  real(real32), allocatable :: display_spectrum(:)
  integer, allocatable :: seed(:)
  type(candidate) :: candidates(MAX_CANDIDATES)
  character(len=24) :: sent_message
  character(len=22) :: jt65_message, jt65_sent
  integer(int8) :: lstrong(0:1023)
  integer :: i, k, nwave, njt65, nsending, seed_size, ncand, n_q65, n_q65_found
  integer :: nb, nbslider, ihsym, nzap, nkh
  real(real32) :: gainx, gainy, phasex, phasey, rejectx, rejecty, pxdb, pydb, slimit
  real(real64) :: phase

  call set_runtime_params(sample_rate, fft_size, 56 * sample_rate)
  allocate(dd(4, nsmax_active), ss(4, 322, nfft_active), savg(4, nfft_active))
  allocate(waveform(nsmax_active), jt65_wave(60 * sample_rate), display_spectrum(nfft_active))
  ss = 0.0
  savg = 0.0
  waveform = cmplx(0.0, 0.0)

  call random_seed(size=seed_size)
  allocate(seed(seed_size))
  seed = 104729
  call random_seed(put=seed)
  call random_number(dd)
  dd = 60.0 * (dd - 0.5)

  ! The capture is centered at 48 kHz; transmission starts one second in.
  ! A weak Q65 signal at 49770 Hz ...
  call gen_q65_cwave(q65_message, 48000 + 1770, 2, real(sample_rate, real64), &
       sent_message, waveform, nwave)
  call require(nwave > 0 .and. trim(sent_message) == trim(q65_message), 'generate the Q65 message')
  call require(sample_rate + nwave <= nsmax_active, 'Q65 waveform fits in the capture')
  do i = 1, nwave
     k = sample_rate + i
     dd(1,k) = dd(1,k) + q65_amplitude * real(waveform(i))
     dd(2,k) = dd(2,k) + q65_amplitude * aimag(waveform(i))
     dd(3,k) = dd(3,k) - q65_amplitude * real(waveform(i))
     dd(4,k) = dd(4,k) - q65_amplitude * aimag(waveform(i))
  end do

  ! ... and two much stronger JT65B signals, about a kilohertz either side of it
  ! (at 48800 and 50700 Hz, the mixing frequency being the mirror image).
  call add_jt65('CQ G4SWX JO02', 48000 - 800)
  call add_jt65('CQ DG2YCB JO42', 48000 - 2700)
  dd = real(nint(dd), real32)
  deallocate(waveform, jt65_wave)

  call init_timer()
  call ftninit('.')
  open(unit=19, status='scratch')
  open(unit=20, status='scratch')
  call decodes_init()
  call init_wideband_sync()
  idphi = 0
  nkhz_center = 125

  nb = 0
  nbslider = 40
  gainx = 1.0
  gainy = 1.0
  phasex = 0.0
  phasey = 0.0
  slimit = 0.0
  ihsym = 0
  nzap = 0
  do i = 1, 302
     k = fft_size + nint(real(i - 1, real64) * 2048.0_real64 * sample_rate / 11025.0_real64)
     call symspec(k, 1, 1, nb, nbslider, idphi, 0, 0, gainx, gainy, &
          phasex, phasey, rejectx, rejecty, pxdb, pydb, display_spectrum, &
          nkh, ihsym, nzap, slimit, lstrong)
  end do
  call require(ihsym == 302, 'complete the symbol spectra')

  call get_candidates(ss, savg, .true., ihsym, 48, 56, 2, 2, candidates, ncand)
  n_q65 = 0
  n_q65_found = 0
  do i = 1, ncand
     if (candidates(i)%iflip /= 0) cycle
     n_q65 = n_q65 + 1
     if (abs(1000.0 * candidates(i)%f - 49770.0) <= 10.0) n_q65_found = n_q65_found + 1
  end do
  print '(a,i0,a,i0,a,i0)', 'candidates=', ncand, ' q65-type=', n_q65, ' on the weak Q65=', n_q65_found
  call require(n_q65_found > 0, 'the weak Q65 signal becomes a candidate beside two strong JT65 signals')

  call fini_timer()
  print '(a)', 'MAP65 candidate peaks test passed.'

contains

  subroutine add_jt65(message, mix_hz)
    character(len=*), intent(in) :: message
    integer, intent(in) :: mix_hz
    jt65_message = message
    call cgen65(jt65_message, 2, 1.0_real64, nsending, jt65_sent, jt65_wave, njt65)
    call require(njt65 > 0, 'generate a JT65B message')
    ! cgen65 returns a waveform near -1270 Hz; shift it up by mix_hz + 1270.
    phase = 0.0d0
    do i = 1, njt65
       phase = phase + twopi * real(mix_hz + 1270, real64) / sample_rate
       if (phase > twopi) phase = phase - twopi
       k = sample_rate + i
       if (k > nsmax_active) exit
       dd(1,k) = dd(1,k) + jt65_amplitude * real(jt65_wave(i) * cmplx(cos(phase), sin(phase)))
       dd(2,k) = dd(2,k) + jt65_amplitude * aimag(jt65_wave(i) * cmplx(cos(phase), sin(phase)))
       dd(3,k) = dd(3,k) - jt65_amplitude * real(jt65_wave(i) * cmplx(cos(phase), sin(phase)))
       dd(4,k) = dd(4,k) - jt65_amplitude * aimag(jt65_wave(i) * cmplx(cos(phase), sin(phase)))
    end do
  end subroutine add_jt65

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require
end program test_map65_candidate_peaks
