! Runtime sample-rate / FFT-size parameters for QMAP.
!
! The /datcom/ common block cannot hold allocatable arrays, so its dd, ss
! and savg arrays are sized at compile time against MAX_RATE_HZ (the
! largest supported input rate).  The runtime variables below say how
! much of those arrays is in use for the selected rate.  The C++ side
! calls qmap_set_runtime_params(rate, nfft) once at startup, before any
! call into libqmap.
!
! The defaults reproduce the original 96 kHz / NFFT=32768 behaviour.

module qmap_params
  use, intrinsic :: iso_c_binding
  implicit none

  ! Compile-time upper bounds.
  integer, parameter :: MAX_RATE_HZ    = 256000
  integer, parameter :: MAX_NSMAX      = 60 * MAX_RATE_HZ           ! 15,360,000
  integer, parameter :: MAX_NFFT       = 131072                     ! next power of 2 above 32768 * 256/96
  integer, parameter :: BASELINE_RATE  = 96000
  integer, parameter :: BASELINE_NFFT  = 32768

  ! Active runtime values.
  integer :: nrate_active     = BASELINE_RATE
  integer :: nfft_active      = BASELINE_NFFT
  integer :: nsmax_active     = 60 * BASELINE_RATE                  ! 5,760,000
  integer :: nfft_big_active  = 56 * BASELINE_RATE                  ! 5,376,000, length of the long FFT

  ! Number of I/Q pairs in one UDP packet in the nrx=+1 (single pol, i*2)
  ! Linrad datagram.  Set by soundin.cpp on every packet received.
  integer :: npairs_active = 348

  ! Sample rate (Hz) found in the header of the .qm file most recently read by read_qm.
  integer :: qm_file_rate = 0

contains

  subroutine qmap_set_runtime_params(rate_hz, nfft) bind(c, name='qmap_set_runtime_params')
    integer(c_int), value :: rate_hz, nfft
    if (rate_hz <= 0 .or. rate_hz > MAX_RATE_HZ) return
    if (nfft    <= 0 .or. nfft    > MAX_NFFT)       return
    nrate_active    = rate_hz
    nfft_active     = nfft
    nsmax_active    = 60 * rate_hz
    nfft_big_active = 56 * rate_hz
  end subroutine qmap_set_runtime_params

  subroutine qmap_set_npairs_active(npairs) bind(c, name='qmap_set_npairs_active')
    integer(c_int), value :: npairs
    if (npairs > 0) npairs_active = npairs
  end subroutine qmap_set_npairs_active

  ! Blanked samples (per half-symbol) at which symspec stops trusting that half-symbol's level
  ! for the blanker threshold: 50% of the 17800 samples that make 100% at 96 kHz (nzap/178).
  ! The samples a half-symbol holds, and so the number the blanker can zero, grow with the rate.
  real(c_double) function qmap_zap_limit() bind(c, name='qmap_zap_limit')
    qmap_zap_limit = 8900.0d0 * real(nrate_active, c_double) / real(BASELINE_RATE, c_double)
  end function qmap_zap_limit

  ! Rate recorded in the last .qm file header that read_qm looked at (0 if none yet).
  integer(c_int) function qmap_qm_file_rate() bind(c, name='qmap_qm_file_rate')
    qmap_qm_file_rate = qm_file_rate
  end function qmap_qm_file_rate

end module qmap_params
