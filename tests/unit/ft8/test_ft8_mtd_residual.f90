program test_ft8_mtd_residual
  use iso_fortran_env, only : int64
  use ft8_mtd_residual
  use ft8_mod1, only : cw,endcorr
  implicit none

  integer, parameter :: frame_samples=151680

  call test_interaction_boundary()
  call test_independent_delta_commits()
  call test_conflict_and_duplicate()
  call test_candidate_refresh()
  call test_real_subtractor_equivalence()
  call test_phase_epoch_conflict()
  call test_half_sample_endpoints()
  call test_pass7_restore()

contains

  subroutine test_interaction_boundary()
    type(ft8_subtraction_descriptor) :: left,right
    integer :: tones(79)

    tones=1
    call mtd_prepare(spread(0.0,1,mtd_residual_samples),1)
    call mtd_make_descriptor(left,1,tones,1000.0,0.5)
    call mtd_make_descriptor(right,1,tones,1049.999,0.5)
    if(.not.mtd_descriptors_conflict(left,right)) error stop &
         'frequencies less than 50 Hz apart must interact'
    call mtd_make_descriptor(right,1,tones,1050.0,0.5)
    if(mtd_descriptors_conflict(left,right)) error stop &
         'frequencies exactly 50 Hz apart must be independent'
    call mtd_make_descriptor(left,1,tones,1000.0,-12.5)
    call mtd_make_descriptor(right,1,tones,1000.0,14.9)
    if(mtd_descriptors_conflict(left,right)) error stop &
         'disjoint subtraction supports must be independent'
    if(mtd_start_sample(0.00005).ne.1) error stop &
         'positive fractional start samples must truncate toward zero'
    if(mtd_start_sample(-0.00023333333).ne.-1) error stop &
         'negative fractional start samples must truncate toward zero'
  end subroutine test_interaction_boundary

  subroutine test_independent_delta_commits()
    type(ft8_subtraction_descriptor) :: first,second
    real, allocatable :: source(:),worker_one(:),worker_two(:),actual(:)
    real, allocatable :: first_delta(:),second_delta(:)
    integer(int64) :: first_epoch,first_generation,second_epoch,second_generation
    integer :: tones(79),outcome

    allocate(source(mtd_residual_samples),worker_one(mtd_residual_samples), &
         worker_two(mtd_residual_samples),actual(mtd_residual_samples), &
         first_delta(frame_samples),second_delta(frame_samples))
    source=0.0
    first_delta=0.0
    second_delta=0.0
    first_delta(1)=-1.0
    second_delta(1)=-2.0
    tones=1

    call mtd_prepare(source,2)
    call mtd_publish_worker(1)
    call mtd_publish_worker(2)
    worker_one=mtd_worker_residual(:,1)
    worker_two=mtd_worker_residual(:,2)
    call mtd_capture_generation(1,first_epoch,first_generation)
    call mtd_capture_generation(2,second_epoch,second_generation)
    call mtd_make_descriptor(first,1,tones,1000.0,0.0)
    call mtd_make_descriptor(second,1,tones,1100.0,0.0)

    worker_one(1)=worker_one(1)+first_delta(1)
    call mtd_try_commit_delta(1,worker_one,first,first_epoch,first_generation, &
         first_delta,outcome)
    if(outcome.ne.mtd_commit_current) error stop 'first commit was not current'
    worker_two(1)=worker_two(1)+second_delta(1)
    call mtd_try_commit_delta(2,worker_two,second,second_epoch,second_generation, &
         second_delta,outcome)
    if(outcome.ne.mtd_commit_independent) error stop &
         'stale independent commit did not use the fast path'
    call mtd_finish(actual)
    if(actual(1).ne.-3.0) error stop 'independent updates were not both preserved'
    if(worker_two(1).ne.-3.0) error stop 'stale worker did not receive canonical state'
  end subroutine test_independent_delta_commits

  subroutine test_conflict_and_duplicate()
    type(ft8_subtraction_descriptor) :: first,second,duplicate
    real, allocatable :: source(:),worker_one(:),worker_two(:),actual(:)
    real, allocatable :: delta(:)
    integer(int64) :: epoch_one,generation_one,epoch_two,generation_two
    integer :: tones(79),outcome

    allocate(source(mtd_residual_samples),worker_one(mtd_residual_samples), &
         worker_two(mtd_residual_samples),actual(mtd_residual_samples), &
         delta(frame_samples))
    source=0.0
    delta=0.0
    delta(1)=-1.0
    tones=2
    call mtd_prepare(source,2)
    call mtd_publish_worker(1)
    call mtd_publish_worker(2)
    worker_one=mtd_worker_residual(:,1)
    worker_two=mtd_worker_residual(:,2)
    call mtd_capture_generation(1,epoch_one,generation_one)
    call mtd_capture_generation(2,epoch_two,generation_two)
    call mtd_make_descriptor(first,1,tones,1000.0,0.0)
    call mtd_make_descriptor(second,1,tones,1025.0,0.0)
    worker_one(1)=worker_one(1)+delta(1)
    call mtd_try_commit_delta(1,worker_one,first,epoch_one,generation_one,delta,outcome)
    worker_two(1)=worker_two(1)+delta(1)
    call mtd_try_commit_delta(2,worker_two,second,epoch_two,generation_two,delta,outcome)
    if(outcome.ne.mtd_commit_conflict) error stop &
         'interacting stale commit must request a refit'

    call mtd_capture_generation(2,epoch_two,generation_two)
    worker_two(1)=worker_two(1)+delta(1)
    call mtd_try_commit_delta(2,worker_two,second,epoch_two,generation_two,delta,outcome)
    if(outcome.ne.mtd_commit_current) error stop 'refitted commit was not published'

    duplicate=second
    call mtd_capture_generation(1,epoch_one,generation_one)
    worker_one(1)=worker_one(1)+delta(1)
    call mtd_try_commit_delta(1,worker_one,duplicate,epoch_one,generation_one, &
         delta,outcome)
    if(outcome.ne.mtd_commit_duplicate) error stop 'duplicate subtraction was repeated'
    call mtd_finish(actual)
    if(actual(1).ne.-2.0) error stop 'conflict or duplicate lost transaction state'
  end subroutine test_conflict_and_duplicate

  subroutine test_candidate_refresh()
    type(ft8_subtraction_descriptor) :: descriptor
    real, allocatable :: source(:),worker_one(:)
    real, allocatable :: delta(:)
    integer(int64) :: epoch,generation
    integer :: tones(79),outcome
    logical :: newdat1,rebuild

    allocate(source(mtd_residual_samples),worker_one(mtd_residual_samples), &
         delta(frame_samples))
    source=0.0
    delta=0.0
    delta(1)=-1.0
    tones=3
    call mtd_prepare(source,2)
    call mtd_publish_worker(1)
    call mtd_publish_worker(2)
    call mtd_mark_spectrum_current(2)
    worker_one=mtd_worker_residual(:,1)
    call mtd_capture_generation(1,epoch,generation)
    call mtd_make_descriptor(descriptor,1,tones,1100.0,0.0)
    worker_one(1)=worker_one(1)+delta(1)
    call mtd_try_commit_delta(1,worker_one,descriptor,epoch,generation,delta,outcome)

    newdat1=.false.
    call mtd_refresh_candidate(2,1000.0,newdat1,rebuild)
    if(rebuild) error stop 'far publication unnecessarily invalidated the spectrum'
    call mtd_refresh_candidate(2,1100.0,newdat1,rebuild)
    if(.not.rebuild) error stop 'near publication did not invalidate the spectrum'
    if(mtd_worker_residual(1,2).ne.-1.0) error stop &
         'near publication did not refresh the worker residual'
    call mtd_mark_spectrum_current(2)
    newdat1=.false.
    call mtd_refresh_candidate(2,1100.0,newdat1,rebuild)
    if(rebuild) error stop 'current spectrum was invalidated again'
  end subroutine test_candidate_refresh

  subroutine test_real_subtractor_equivalence()
    real, allocatable :: source(:),expected(:),actual(:)
    integer :: i,tones(79),outcome

    allocate(source(mtd_residual_samples),expected(mtd_residual_samples), &
         actual(mtd_residual_samples))
    do i=1,mtd_residual_samples
       source(i)=0.01*sin(real(i)/300.0)
    enddo
    tones=0
    cw=cmplx(1.0,0.0)
    endcorr=1.0
    expected=source
    call subtractft8var(expected,tones,1000.0,0.5)

    call mtd_prepare(source,1)
    call mtd_publish_worker(1)
    call mtd_commit_subtraction(1,1,tones,1000.0,0.5, &
         mtd_worker_residual(:,1),outcome)
    call mtd_finish(actual)
    if(outcome.ne.mtd_commit_current) error stop &
         'uncontended real subtraction did not use the current path'
    if(any(actual.ne.expected)) error stop &
         'prepared delta publication differs from serial subtraction'
  end subroutine test_real_subtractor_equivalence

  subroutine test_phase_epoch_conflict()
    type(ft8_subtraction_descriptor) :: descriptor
    real, allocatable :: source(:),delta(:),worker_residual(:)
    integer(int64) :: epoch,generation
    integer :: tones(79),outcome

    allocate(source(mtd_residual_samples),delta(frame_samples), &
         worker_residual(mtd_residual_samples))
    source=0.0
    delta=0.0
    delta(1)=-1.0
    tones=4
    call mtd_prepare(source,1)
    call mtd_publish_worker(1)
    worker_residual=mtd_worker_residual(:,1)
    call mtd_capture_generation(1,epoch,generation)
    call mtd_make_descriptor(descriptor,3,tones,1000.0,0.0)
    call mtd_transform_phase(4)
    worker_residual(1)=worker_residual(1)+delta(1)
    call mtd_try_commit_delta(1,worker_residual,descriptor,epoch,generation, &
         delta,outcome)
    if(outcome.ne.mtd_commit_conflict) error stop &
         'pre-transform transaction was not rejected'
    call mtd_capture_generation(1,epoch,generation)
    worker_residual(1)=worker_residual(1)+delta(1)
    call mtd_try_commit_delta(1,worker_residual,descriptor,epoch,generation, &
         delta,outcome)
    if(outcome.ne.mtd_commit_conflict) error stop &
         'stale descriptor epoch was accepted after refresh'
  end subroutine test_phase_epoch_conflict

  subroutine test_half_sample_endpoints()
    real, allocatable :: residual(:)

    allocate(residual(mtd_residual_samples))
    residual=0.0
    residual(1:3)=[2.0,4.0,8.0]
    residual(mtd_residual_samples)=16.0
    call mtd_forward_half_sample(residual)
    if(residual(1).ne.3.0 .or. residual(2).ne.6.0) error stop &
         'forward half-sample transform changed interior behavior'
    if(residual(mtd_residual_samples).ne.16.0) error stop &
         'forward half-sample transform changed the trailing endpoint'

    residual=0.0
    residual(1:3)=[2.0,4.0,8.0]
    call mtd_backward_half_sample(residual)
    if(residual(1).ne.2.0) error stop &
         'backward half-sample transform changed the leading endpoint'
    if(residual(2).ne.3.0 .or. residual(3).ne.6.0) error stop &
         'backward half-sample transform changed interior behavior'
  end subroutine test_half_sample_endpoints

  subroutine test_pass7_restore()
    type(ft8_subtraction_descriptor) :: descriptor
    real, allocatable :: source(:),expected(:),forward(:),actual(:),delta(:)
    integer(int64) :: epoch,generation
    integer :: i,tones(79),outcome

    allocate(source(mtd_residual_samples),expected(mtd_residual_samples), &
         forward(mtd_residual_samples),actual(mtd_residual_samples), &
         delta(frame_samples))
    do i=1,mtd_residual_samples
       source(i)=real(mod(i,101))
    enddo
    expected=source
    call mtd_backward_half_sample(expected)
    forward=source
    call mtd_forward_half_sample(forward)
    delta=0.0
    delta(1)=-1.0
    tones=5
    call mtd_prepare(source,1)
    call mtd_transform_phase(4)
    call mtd_publish_worker(1)
    call mtd_capture_generation(1,epoch,generation)
    call mtd_make_descriptor(descriptor,4,tones,1000.0,0.0)
    mtd_worker_residual(1,1)=mtd_worker_residual(1,1)+delta(1)
    call mtd_try_commit_delta(1,mtd_worker_residual(:,1),descriptor,epoch, &
         generation,delta,outcome)
    call mtd_finish(actual)
    if(all(actual.eq.forward)) error stop &
         'cycle-two transaction did not mutate the forward residual'
    call mtd_transform_phase(7)
    call mtd_finish(actual)
    if(any(actual.ne.expected)) error stop &
         'pass 7 must restore and shift the saved post-pass-3 residual'
  end subroutine test_pass7_restore

end program test_ft8_mtd_residual
