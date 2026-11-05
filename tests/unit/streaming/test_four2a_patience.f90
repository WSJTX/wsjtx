! SPDX-License-Identifier: GPL-3.0-or-later
! four2a plans at the calling thread's patience override when it is 0 or more,
! and at /patience/'s otherwise. With /patience/ at 1 (FFTW_ESTIMATE_PATIENT,
! the codec's default), a new plan leaves wisdom that FFTW_WISDOM_ONLY with
! FFTW_ESTIMATE_PATIENT accepts; an override of 0 plans FFTW_ESTIMATE, whose
! wisdom it does not accept. Neither measures, so the rows take no time.

! What the worker saw, written and read as sequentially consistent atomics:
! TSan does not see libgomp's barrier.
module four2a_patience_test_controls
  implicit none
  integer, save :: workers = 0, worker_override = 0
  logical, save :: worker_patient = .false.
end module four2a_patience_test_controls

program test_four2a_patience
  use omp_lib, only: omp_get_thread_num, omp_get_num_threads
  use fftw3, only: FFTW_FORWARD, FFTW_ESTIMATE_PATIENT, FFTW_WISDOM_ONLY
  use four2a_patience, only: patience_override
  use four2a_patience_test_controls
  implicit none
  integer :: npatience, nthreads, nfail, seen_workers, seen_override, nworkers, override
  logical :: seen_patient, good
  common/patience/npatience,nthreads

  nfail = 0
  npatience = 1
  nthreads = 1

  patience_override = 0
  call ok('an override of 0 plans FFTW_ESTIMATE', .not. patient(60))

  ! An OpenMP worker's override starts at -1, whatever the master's is.
  !$omp parallel num_threads(2) private(nworkers, override, good)
  if (omp_get_thread_num() .eq. 1) then
     nworkers = omp_get_num_threads()
     override = patience_override
     good = patient(72)
     !$omp atomic write seq_cst
     workers = nworkers
     !$omp atomic write seq_cst
     worker_override = override
     !$omp atomic write seq_cst
     worker_patient = good
  end if
  !$omp end parallel
  !$omp atomic read seq_cst
  seen_workers = workers
  !$omp atomic read seq_cst
  seen_override = worker_override
  !$omp atomic read seq_cst
  seen_patient = worker_patient
  if (seen_workers .ne. 2) error stop 'two OpenMP threads required'
  call ok('a worker plans at /patience/''s while the master overrides',         &
       seen_override .eq. -1 .and. seen_patient)
  call ok('the master''s override stays its own', patience_override .eq. 0)

  patience_override = -1
  call ok('below 0, four2a plans at /patience/''s', patient(84))

  if (nfail .ne. 0) error stop 1

contains

  ! Whether four2a's new plan of size nfft left FFTW_ESTIMATE_PATIENT wisdom.
  logical function patient(nfft)
    integer, intent(in) :: nfft
    complex :: a(nfft)
    integer*8 :: probe
    a = 0
    call four2a(a, nfft, 1, -1, 1)
    ! FFTW stores a pointer, which fills only 4 of probe's 8 bytes on a 32-bit target.
    probe = 0
    call sfftw_plan_dft_1d(probe, nfft, a, a, FFTW_FORWARD,                  &
         ior(FFTW_ESTIMATE_PATIENT, FFTW_WISDOM_ONLY))
    patient = probe .ne. 0
    if (patient) call sfftw_destroy_plan(probe)
  end function patient

  subroutine ok(what, good)
    character(len=*), intent(in) :: what
    logical, intent(in) :: good
    if (good) then
       write(*, '(a)') 'PASS: ' // what
    else
       write(*, '(a)') 'FAIL: ' // what
       nfail = nfail + 1
    end if
  end subroutine ok

end program test_four2a_patience
