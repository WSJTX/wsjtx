program test_ft8_mtd_transaction_concurrent
  use iso_fortran_env, only : int64
  use omp_lib
  use ft8_mtd_residual
  implicit none

  integer, parameter :: frame_samples=151680

  if(omp_get_max_threads().lt.4) error stop 'four OpenMP workers are required'
  call test_independent_publishers()
  call test_duplicate_publishers()
  call test_coherent_snapshots()

contains

  subroutine test_independent_publishers()
    real, allocatable :: source(:),delta(:,:),actual(:)
    integer :: outcomes(4),worker,tones(79)
    integer(int64) :: epoch,generation
    type(ft8_subtraction_descriptor) :: descriptor

    allocate(source(mtd_residual_samples),delta(frame_samples,4), &
         actual(mtd_residual_samples))
    source=0.0
    delta=0.0
    delta(1,1)=-1.0
    delta(1,2)=-2.0
    delta(1,3)=-3.0
    delta(1,4)=-4.0
    call mtd_prepare(source,4)

!$omp parallel num_threads(4) private(worker,tones,epoch,generation,descriptor)
    worker=omp_get_thread_num()+1
    tones=worker
    call mtd_publish_worker(worker)
    call mtd_capture_generation(worker,epoch,generation)
    call mtd_make_descriptor(descriptor,1,tones, &
         800.0+100.0*real(worker),0.0)
    mtd_worker_residual(1,worker)=mtd_worker_residual(1,worker)+delta(1,worker)
!$omp barrier
    call mtd_try_commit_delta(worker,mtd_worker_residual(:,worker),descriptor, &
         epoch,generation,delta(:,worker),outcomes(worker))
!$omp end parallel

    call mtd_finish(actual)
    if(actual(1).ne.-10.0) error stop 'parallel independent updates were lost'
    if(count(outcomes.eq.mtd_commit_current).ne.1) error stop &
         'exactly one same-generation transaction must commit first'
    if(count(outcomes.eq.mtd_commit_independent).ne.3) error stop &
         'stale independent transactions did not use the fast path'
  end subroutine test_independent_publishers

  subroutine test_duplicate_publishers()
    real, allocatable :: source(:),delta(:,:),actual(:)
    integer :: outcomes(4),worker,tones(79)
    integer(int64) :: epoch,generation
    type(ft8_subtraction_descriptor) :: descriptor

    allocate(source(mtd_residual_samples),delta(frame_samples,4), &
         actual(mtd_residual_samples))
    source=0.0
    delta=0.0
    delta(1,:)=-1.0
    tones=7
    call mtd_prepare(source,4)

!$omp parallel num_threads(4) private(worker,epoch,generation,descriptor) &
!$omp shared(tones)
    worker=omp_get_thread_num()+1
    call mtd_publish_worker(worker)
    call mtd_capture_generation(worker,epoch,generation)
    call mtd_make_descriptor(descriptor,1,tones,1000.0,0.0)
    mtd_worker_residual(1,worker)=mtd_worker_residual(1,worker)+delta(1,worker)
!$omp barrier
    call mtd_try_commit_delta(worker,mtd_worker_residual(:,worker),descriptor, &
         epoch,generation,delta(:,worker),outcomes(worker))
!$omp end parallel

    call mtd_finish(actual)
    if(actual(1).ne.-1.0) error stop 'duplicate transaction was applied more than once'
    if(count(outcomes.eq.mtd_commit_current).ne.1) error stop &
         'duplicate race did not produce one winner'
    if(count(outcomes.eq.mtd_commit_duplicate).ne.3) error stop &
         'duplicate race did not reject all later publishers'
  end subroutine test_duplicate_publishers

  subroutine test_coherent_snapshots()
    integer, parameter :: writer_iterations=50,reader_iterations=500
    real, allocatable :: source(:),delta(:,:),actual(:)
    real :: snapshot_value
    integer :: worker,iteration,tones(79),outcome,writers_started,reader_ready, &
         first_commits,reader_observed
    integer(int64) :: epoch,generation
    logical :: torn_snapshot,intermediate_observed
    type(ft8_subtraction_descriptor) :: descriptor

    allocate(source(mtd_residual_samples),delta(frame_samples,3), &
         actual(mtd_residual_samples))
    source=0.0
    delta=-1.0
    torn_snapshot=.false.
    intermediate_observed=.false.
    writers_started=0
    reader_ready=0
    first_commits=0
    reader_observed=0
    call mtd_prepare(source,4)

!$omp parallel num_threads(4) private(worker,iteration,tones,outcome,epoch, &
!$omp generation,descriptor,snapshot_value) &
!$omp shared(torn_snapshot,intermediate_observed,writers_started,reader_ready, &
!$omp first_commits,reader_observed)
    worker=omp_get_thread_num()+1
    call mtd_publish_worker(worker)
!$omp barrier
    if(worker.le.3) then
!$omp atomic update
       writers_started=writers_started+1
       do
!$omp atomic read
          outcome=reader_ready
          if(outcome.eq.1) exit
       enddo
       do iteration=1,writer_iterations
          tones=0
          tones(1)=iteration
          tones(2)=worker
          call mtd_make_descriptor(descriptor,1,tones, &
               700.0+100.0*real(worker),0.0)
          do
             call mtd_capture_generation(worker,epoch,generation)
             mtd_worker_residual(1:frame_samples,worker)= &
                  mtd_worker_residual(1:frame_samples,worker)+delta(:,worker)
             call mtd_try_commit_delta(worker,mtd_worker_residual(:,worker), &
                  descriptor,epoch,generation,delta(:,worker),outcome)
             if(outcome.ne.mtd_commit_conflict) exit
          enddo
          if(iteration.eq.1) then
!$omp atomic update
             first_commits=first_commits+1
             do
!$omp atomic read
                outcome=reader_observed
                if(outcome.eq.1) exit
             enddo
          endif
       enddo
    else
       do
!$omp atomic read
          outcome=writers_started
          if(outcome.eq.3) exit
       enddo
!$omp atomic write
       reader_ready=1
       do
!$omp atomic read
          outcome=first_commits
          if(outcome.eq.3) exit
       enddo
       call mtd_publish_worker(worker)
       snapshot_value=-mtd_worker_residual(1,worker)
       intermediate_observed=snapshot_value.eq.3.0
!$omp atomic write
       reader_observed=1
       do iteration=1,reader_iterations
          call mtd_publish_worker(worker)
          if(minval(mtd_worker_residual(1:frame_samples,worker)).ne. &
               maxval(mtd_worker_residual(1:frame_samples,worker))) &
               torn_snapshot=.true.
          snapshot_value=-mtd_worker_residual(1,worker)
          if(snapshot_value.gt.0.0 .and. &
               snapshot_value.lt.real(3*writer_iterations)) &
               intermediate_observed=.true.
       enddo
    endif
!$omp end parallel

    if(torn_snapshot) error stop 'reader observed a torn canonical snapshot'
    if(.not.intermediate_observed) error stop &
         'coherence reader did not overlap the publishers'
    call mtd_finish(actual)
    if(actual(1).ne.-real(3*writer_iterations)) error stop &
         'coherence stress did not publish every transaction'
  end subroutine test_coherent_snapshots

end program test_ft8_mtd_transaction_concurrent
