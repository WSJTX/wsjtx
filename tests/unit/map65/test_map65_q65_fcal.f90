program test_map65_q65_fcal
  ! The cursor (and a clicked frequency) are true frequencies, but a receiver
  ! with a frequency-calibration offset Fcal shows a signal at its true
  ! frequency + Fcal in the spectrum (the Wide Graph adds Fcal when it maps a
  ! frequency to a bin).  q65b searches the sync curve in a +/- Ftol window
  ! around f0, and the cursor fallback also decodes around f0, so the caller
  ! must pass the cursor in the spectrum frame: q65_cursor_f0 does that for
  ! the manual click and the fallback.  The manual click's decode point comes
  ! from fqso, which map65a builds with Fcal included.
  use iso_fortran_env, only: int8, real32, real64
  use datcom_ptrs_mod, only: dd, ss, savg
  use decodes_mod, only: decodes_init, ldecoded
  use filbig_mod, only: filbig, MAXFFT2
  use ftninit_mod, only: ftninit
  use gen_q65_cwave_mod, only: gen_q65_cwave
  use npar_ptrs_mod, only: abort_decode, fcenter, idphi, manualDecodeFlag, ndepthq65, &
       nfft_active, nsmax_active, set_runtime_params, t_start
  use q65_decode, only: msg0, nsnr0
  use q65_cursor_mod, only: q65_cursor_f0
  use q65b_mod, only: q65b
  use symspec_mod, only: symspec
  use timer_impl, only: init_timer, fini_timer
  use wideband_sync, only: candidate, get_candidates, init_wideband_sync, &
       MAX_CANDIDATES, nkhz_center
  implicit none

  integer, parameter :: sample_rate = 96000, fft_size = 32768
  integer, parameter :: nfcal = 120           ! Hz; more than Ftol (100 Hz) away from the cursor
  integer, parameter :: mousefqso = 125       ! kHz; true frequency 49270 Hz in the capture
  character(len=24), parameter :: message = 'W1AAA K2BBB EM00'
  complex(real32), allocatable :: waveform(:), c4a(:), c4b(:)
  real(real32), allocatable :: display_spectrum(:)
  integer, allocatable :: seed(:)
  type(candidate) :: candidates(MAX_CANDIDATES)
  character(len=24) :: sent_message
  character(len=12) :: hiscall
  character(len=6) :: hisgrid
  integer(int8) :: lstrong(0:1023)
  integer :: i, k, nwave, seed_size, ncand, selected, newdat, n4, idec
  integer :: nb, nbslider, ihsym, nzap, nkh
  real(real32) :: gainx, gainy, phasex, phasey, rejectx, rejecty, pxdb, pydb, slimit
  real(real64) :: f0
  real :: fqso, df3
  integer :: ibin

  ! q65_cursor_f0 converts the cursor (a true frequency) to the spectrum frequency.
  call require(abs(q65_cursor_f0(mousefqso, 0, 125, 96000, 0) - 49.27046_real64) < 1.0e-4_real64, &
       'cursor maps to the spectrum with Fcal = 0')
  call require(abs(q65_cursor_f0(mousefqso, 0, 125, 96000, nfcal) - (49.27046_real64 + 0.001_real64*nfcal)) &
       < 1.0e-4_real64, 'Fcal moves the cursor target up by Fcal')
  call require(abs(q65_cursor_f0(mousefqso, -25, 125, 96000, -40) - 49.20546_real64) < 1.0e-4_real64, &
       'mousedf and a negative Fcal both apply')

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

  ! The signal's true frequency is 49270 Hz; a receiver with this Fcal shows
  ! it at 49270 + Fcal Hz. Transmission starts one second in.
  call gen_q65_cwave(message, 48000 + 1270 + nfcal, 2, real(sample_rate, real64), &
       sent_message, waveform, nwave)
  call require(nwave > 0 .and. trim(sent_message) == trim(message), 'generate the Q65 message')
  call require(sample_rate + nwave <= nsmax_active, 'waveform fits in the capture')
  do i = 1, nwave
     k = sample_rate + i
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
  ndepthq65 = 1
  idphi = 180
  fcenter = 144.125_real64
  nkhz_center = 125
  hiscall = ' '
  hisgrid = ' '

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

  ! Fill sync() and the big-FFT spectrum, as a normal pass would before the
  ! cursor targets run.
  call get_candidates(ss, savg, .true., ihsym, 48, 56, 0, 2, candidates, ncand)
  selected = 0
  do i = 1, ncand
     if (candidates(i)%iflip /= 0) cycle
     if (abs(1000.0 * candidates(i)%f - (49270.0 + nfcal)) > 10.0) cycle
     selected = i
     exit
  end do
  call require(selected > 0, 'the offset signal is a wideband candidate at true + Fcal')
  newdat = 1
  call filbig(dd, nsmax_active, 1000.0_real64 * candidates(selected)%f, newdat, &
       sample_rate, .true., c4a, c4b, n4)

  ! fqso as map65a builds it: mousefqso + foffset - 0.5*(nfa+nfb) + nfshift.
  ! MainWindow sets nfshift = nfa + ispan/2 - ifc, with ifc = nkhz_center, so
  ! 0.5*(nfa+nfb) - nfshift = nkhz_center and
  ! fqso = mousefqso - nkhz_center + 1.270 + 0.001*Fcal.
  fqso = real(mousefqso - nkhz_center) + 1.270 + 0.001*real(nfcal)
  df3 = real(sample_rate) / real(nfft_active)
  ibin = nint(1000.0 * (49.270_real64 + 0.001_real64*nfcal) / df3)   ! sync bin of the signal
  f0 = q65_cursor_f0(mousefqso, 0, nkhz_center, sample_rate, nfcal)

  ! Cursor fallback (no candidate was found): the cursor is on the true
  ! frequency and the search window is +/- Ftol (100 Hz) around it.
  ldecoded = .false.
  manualDecodeFlag = 0
  nsnr0 = -99
  msg0 = ' '
  call system_clock(t_start)
  call q65b(1, 1, 0, fcenter, nfcal, sample_rate, mousefqso, 0, 100, .true., idphi, &
       'N0CALL      ', 'FN31  ', hiscall, hisgrid, 2, f0, fqso, 0, 0, 0, 0, idec, &
       cursor_fallback=.true.)
  call require(nsnr0 > -99 .and. trim(msg0) == trim(message), &
       'the cursor fallback decodes a signal that sits at true + Fcal beyond Ftol of the cursor')
  call require(any(ldecoded(ibin-3:ibin+3)), 'the fallback marks the signal''s own sync bin as decoded')

  ! Manual click on the true frequency.  The decode point comes from fqso (which
  ! already includes Fcal); f0 picks the sync-curve peak that supplies the bin
  ! and the polarization, so it must be in the spectrum frame too.  The display
  ! gate that compares f0's frame with the decoded frequency only controls a
  ! stdout line and is not asserted here.
  ldecoded = .false.
  manualDecodeFlag = 1
  nsnr0 = -99
  msg0 = ' '
  call system_clock(t_start)
  call q65b(1, 1, 0, fcenter, nfcal, sample_rate, mousefqso, 0, 100, .true., idphi, &
       'N0CALL      ', 'FN31  ', hiscall, hisgrid, 2, f0, fqso, 0, 1, 0, 0, idec)
  call require(nsnr0 > -99 .and. trim(msg0) == trim(message), &
       'a manual click on the true frequency decodes a signal that sits at true + Fcal beyond Ftol')
  call require(any(ldecoded(ibin-3:ibin+3)), 'the manual click marks the signal''s own sync bin as decoded')
  call require(.not. logical(abort_decode), 'decode stays within the time budget')

  call fini_timer()
  print '(a)', 'MAP65 Q65 Fcal test passed.'

contains

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require
end program test_map65_q65_fcal
