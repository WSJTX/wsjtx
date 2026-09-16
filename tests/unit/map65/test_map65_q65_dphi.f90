program test_map65_q65_dphi
  use iso_fortran_env, only: int8, real32, real64
  use datcom_ptrs_mod, only: dd, ss, savg
  use decodes_mod, only: decodes_init
  use filbig_mod, only: filbig, MAXFFT2
  use ftninit_mod, only: ftninit
  use gen_q65_cwave_mod, only: gen_q65_cwave
  use npar_ptrs_mod, only: abort_decode, fcenter, idphi, manualDecodeFlag, ndepth, &
       nfft_active, nsmax_active, set_runtime_params, t_start
  use q65_decode, only: msg0, nsnr0
  use q65b_mod, only: q65b
  use symspec_mod, only: symspec
  use timer_impl, only: init_timer, fini_timer
  use wideband_sync, only: candidate, get_candidates, init_wideband_sync, &
       MAX_CANDIDATES, nkhz_center
  implicit none

  integer, parameter :: sample_rate = 96000, fft_size = 32768
  character(len=24), parameter :: message = 'W1AAA K2BBB EM00'
  complex(real32), allocatable :: waveform(:), c4a(:), c4b(:)
  real(real32), allocatable :: display_spectrum(:)
  integer, allocatable :: seed(:)
  type(candidate) :: candidates(MAX_CANDIDATES)
  character(len=24) :: sent_message
  integer(int8) :: lstrong(0:1023)
  integer :: i, k, nwave, seed_size, ncand, selected, newdat, n4, idec
  integer :: nb, nbslider, ihsym, nzap, nkh
  real(real32) :: gainx, gainy, phasex, phasey, rejectx, rejecty, pxdb, pydb, slimit

  call set_runtime_params(sample_rate, fft_size, 56 * sample_rate)
  allocate(dd(4, nsmax_active), ss(4, 322, nfft_active), savg(4, nfft_active))
  allocate(waveform(nsmax_active), display_spectrum(nfft_active))
  allocate(c4a(MAXFFT2), c4b(MAXFFT2))
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
  call gen_q65_cwave(message, 48000 + 1270, 2, real(sample_rate, real64), &
       sent_message, waveform, nwave)
  call require(nwave > 0 .and. trim(sent_message) == trim(message), 'generate the Q65 message')
  call require(sample_rate + nwave <= nsmax_active, 'waveform fits in the capture')
  do i = 1, nwave
     k = sample_rate + i
     ! Equal signal amplitudes with a 180-degree receiver phase offset.
     dd(1,k) = dd(1,k) + real(waveform(i))
     dd(2,k) = dd(2,k) + aimag(waveform(i))
     dd(3,k) = dd(3,k) - real(waveform(i))
     dd(4,k) = dd(4,k) - aimag(waveform(i))
  end do
  dd = real(nint(dd), real32)
  deallocate(waveform)

  call init_timer()
  call ftninit('.')
  open(unit=19, status='scratch')
  open(unit=20, status='scratch')
  call decodes_init()
  call init_wideband_sync()
  abort_decode = .false.
  manualDecodeFlag = 0
  ndepth = 1
  idphi = 180
  fcenter = 144.125_real64
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

  call get_candidates(ss, savg, .true., ihsym, 48, 56, 0, 2, candidates, ncand)
  selected = 0
  do i = 1, ncand
     if (candidates(i)%iflip /= 0) cycle
     if (abs(1000.0 * candidates(i)%f - 49270.0) > 10.0) cycle
     selected = i
     exit
  end do
  call require(selected > 0, 'acquire the Q65 signal with calibrated spectra')

  newdat = 1
  call filbig(dd, nsmax_active, 1000.0_real64 * candidates(selected)%f, newdat, &
       sample_rate, .true., c4a, c4b, n4)
  call system_clock(t_start)
  call q65b(1, 0, 0, fcenter, 0, sample_rate, nkhz_center, 0, 100, .true., idphi, &
       'N0CALL      ', 'FN31  ', '            ', '      ', 2, &
       real(candidates(selected)%f, real64), 1.270, newdat, 0, 0, 0, idec)

  call require(.not. logical(abort_decode), 'decode within the time budget')
  call require(nsnr0 > -99 .and. trim(msg0) == trim(message), &
       'recover the Q65 message despite opposite-phase receiver channels')
  call fini_timer()
  print '(a)', 'MAP65 Q65 Dphi test passed.'

contains

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require
end program test_map65_q65_dphi
