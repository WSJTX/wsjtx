module ft8_mtd_residual

  use iso_fortran_env, only : int64

  implicit none

  integer, parameter, public :: mtd_residual_samples=180000
  integer, parameter, public :: mtd_spectrum_bins=96001
  integer, parameter, public :: mtd_max_subtractions_per_worker=460
  integer, parameter, public :: mtd_subtraction_passes=5
  integer, parameter, public :: mtd_commit_current=1
  integer, parameter, public :: mtd_commit_independent=2
  integer, parameter, public :: mtd_commit_conflict=3
  integer, parameter, public :: mtd_commit_duplicate=4
  integer, parameter, public :: mtd_commit_fallback=5
  integer, parameter :: mtd_frame_samples=151680
  real, parameter :: mtd_interaction_bandwidth=50.0

  type, public :: ft8_subtraction_descriptor
     integer :: tones(79)=0
     real :: frequency=0.0
     real :: dt=0.0
     integer :: frequency_bin=0
     integer :: start_sample=0
     integer :: clipped_start=0
     integer :: clipped_end=-1
     integer(int64) :: epoch=0_int64
     integer :: pass=0
     integer(int64) :: commit_generation=0_int64
  end type ft8_subtraction_descriptor

  real, allocatable, save, public :: mtd_worker_residual(:,:)
  complex, allocatable, save, public :: mtd_worker_spectrum(:,:)

  real, allocatable, save :: canonical_residual(:)
  real, allocatable, save :: post_pass3_residual(:)
  real, allocatable, save :: subtraction_delta(:,:)
  type(ft8_subtraction_descriptor), allocatable, save :: history(:)
  integer(int64), allocatable, save :: worker_generation(:)
  integer(int64), allocatable, save :: worker_epoch(:)
  integer(int64), allocatable, save :: worker_spectrum_generation(:)
  integer(int64), allocatable, save :: worker_spectrum_epoch(:)
  integer, save :: active_workers=0
  integer(int64), save :: canonical_generation=0_int64
  integer(int64), save :: current_epoch=0_int64
  integer, save :: history_count=0

  interface
     subroutine subtractft8var(residual,itone,f0,dt,delta)
       real, intent(inout) :: residual(180000)
       integer, intent(in) :: itone(79)
       real, intent(in) :: f0,dt
       real, intent(out), optional :: delta(151680)
     end subroutine subtractft8var
  end interface

contains

  subroutine mtd_prepare(input_residual,nworkers)
    real, intent(in) :: input_residual(:)
    integer, intent(in) :: nworkers

    if(size(input_residual).ne.mtd_residual_samples) error stop &
         'MTD residual has an unexpected sample count'
    if(nworkers.lt.1) error stop 'MTD requires at least one worker'

    call ensure_capacity(nworkers)
!$omp critical(ft8_mtd_canonical)
    active_workers=nworkers
    current_epoch=current_epoch+1_int64
    canonical_generation=0_int64
    history_count=0
    canonical_residual=input_residual
    post_pass3_residual=input_residual
    worker_generation(1:active_workers)=-1_int64
    worker_epoch(1:active_workers)=current_epoch
    worker_spectrum_generation(1:active_workers)=-1_int64
    worker_spectrum_epoch(1:active_workers)=current_epoch
!$omp end critical(ft8_mtd_canonical)
  end subroutine mtd_prepare

  subroutine ensure_capacity(nworkers)
    integer, intent(in) :: nworkers
    integer :: current_workers,history_capacity

    current_workers=0
    if(allocated(mtd_worker_residual)) current_workers=size(mtd_worker_residual,2)
    if(current_workers.ge.nworkers) return

    if(allocated(mtd_worker_residual)) deallocate(mtd_worker_residual)
    if(allocated(mtd_worker_spectrum)) deallocate(mtd_worker_spectrum)
    if(allocated(subtraction_delta)) deallocate(subtraction_delta)
    if(allocated(history)) deallocate(history)
    if(allocated(worker_generation)) deallocate(worker_generation)
    if(allocated(worker_epoch)) deallocate(worker_epoch)
    if(allocated(worker_spectrum_generation)) deallocate(worker_spectrum_generation)
    if(allocated(worker_spectrum_epoch)) deallocate(worker_spectrum_epoch)
    if(.not.allocated(canonical_residual)) allocate(canonical_residual(mtd_residual_samples))
    if(.not.allocated(post_pass3_residual)) allocate(post_pass3_residual(mtd_residual_samples))

    history_capacity=mtd_subtraction_passes*mtd_max_subtractions_per_worker*nworkers
    allocate(mtd_worker_residual(mtd_residual_samples,nworkers))
    allocate(mtd_worker_spectrum(0:mtd_spectrum_bins-1,nworkers))
    allocate(subtraction_delta(mtd_frame_samples,nworkers))
    allocate(history(history_capacity))
    allocate(worker_generation(nworkers))
    allocate(worker_epoch(nworkers))
    allocate(worker_spectrum_generation(nworkers))
    allocate(worker_spectrum_epoch(nworkers))
  end subroutine ensure_capacity

  subroutine mtd_publish_worker(worker)
    integer, intent(in) :: worker

    call assert_worker(worker)
!$omp critical(ft8_mtd_canonical)
    mtd_worker_residual(:,worker)=canonical_residual
    worker_generation(worker)=canonical_generation
    worker_epoch(worker)=current_epoch
    worker_spectrum_generation(worker)=-1_int64
    worker_spectrum_epoch(worker)=current_epoch
!$omp end critical(ft8_mtd_canonical)
  end subroutine mtd_publish_worker

  subroutine mtd_refresh_candidate(worker,frequency,newdat1,rebuild_spectrum)
    integer, intent(in) :: worker
    real, intent(in) :: frequency
    logical, intent(inout) :: newdat1
    logical, intent(out) :: rebuild_spectrum
    integer :: i
    logical :: refresh_residual

    call assert_worker(worker)
!$omp critical(ft8_mtd_canonical)
    refresh_residual=worker_epoch(worker).ne.current_epoch
    if(.not.refresh_residual) then
       do i=1,history_count
          if(history(i)%epoch.ne.current_epoch .or. &
               history(i)%commit_generation.le.worker_generation(worker)) cycle
          if(abs(history(i)%frequency-frequency).lt.mtd_interaction_bandwidth) then
             refresh_residual=.true.
             exit
          endif
       enddo
    endif
    if(refresh_residual) then
       mtd_worker_residual(:,worker)=canonical_residual
       worker_generation(worker)=canonical_generation
       worker_epoch(worker)=current_epoch
       newdat1=.true.
    endif
    if(worker_spectrum_epoch(worker).ne.worker_epoch(worker)) newdat1=.true.
    if(.not.newdat1) then
       do i=1,history_count
          if(history(i)%epoch.ne.worker_epoch(worker) .or. &
               history(i)%commit_generation.le.worker_spectrum_generation(worker) .or. &
               history(i)%commit_generation.gt.worker_generation(worker)) cycle
          if(abs(history(i)%frequency-frequency).lt.mtd_interaction_bandwidth) then
             newdat1=.true.
             exit
          endif
       enddo
    endif
    rebuild_spectrum=newdat1
!$omp end critical(ft8_mtd_canonical)
  end subroutine mtd_refresh_candidate

  subroutine mtd_mark_spectrum_current(worker)
    integer, intent(in) :: worker

    call assert_worker(worker)
    worker_spectrum_generation(worker)=worker_generation(worker)
    worker_spectrum_epoch(worker)=worker_epoch(worker)
  end subroutine mtd_mark_spectrum_current

  subroutine mtd_commit_subtraction(worker,ipass,itone,f0,dt,residual,final_outcome)
    integer, intent(in) :: worker,ipass,itone(79)
    real, intent(in) :: f0,dt
    real, intent(inout) :: residual(mtd_residual_samples)
    integer, intent(out), optional :: final_outcome
    type(ft8_subtraction_descriptor) :: descriptor
    integer(int64) :: base_generation,base_epoch
    integer :: outcome

    call assert_worker(worker)
    call mtd_make_descriptor(descriptor,ipass,itone,f0,dt)
    base_generation=worker_generation(worker)
    base_epoch=worker_epoch(worker)
    descriptor%epoch=base_epoch
    call fit_subtraction(worker,residual,descriptor)
    call mtd_try_commit_delta(worker,residual,descriptor,base_epoch, &
         base_generation,subtraction_delta(:,worker),outcome)
    if(outcome.ne.mtd_commit_conflict) then
       if(present(final_outcome)) final_outcome=outcome
       return
    endif

    base_generation=worker_generation(worker)
    base_epoch=worker_epoch(worker)
    descriptor%epoch=base_epoch
    call fit_subtraction(worker,residual,descriptor)
    call mtd_try_commit_delta(worker,residual,descriptor,base_epoch, &
         base_generation,subtraction_delta(:,worker),outcome)
    if(outcome.ne.mtd_commit_conflict) then
       if(present(final_outcome)) final_outcome=outcome
       return
    endif

!$omp critical(ft8_mtd_canonical)
    descriptor%epoch=current_epoch
    if(history_contains_duplicate(descriptor)) then
       outcome=mtd_commit_duplicate
    else
       ! No caller holds an FFT lock while entering this transaction region.
       call subtractft8var(canonical_residual,descriptor%tones, &
            descriptor%frequency,descriptor%dt)
       call record_commit(descriptor)
       outcome=mtd_commit_fallback
    endif
    residual=canonical_residual
    worker_generation(worker)=canonical_generation
    worker_epoch(worker)=current_epoch
!$omp end critical(ft8_mtd_canonical)
    if(present(final_outcome)) final_outcome=outcome
  end subroutine mtd_commit_subtraction

  subroutine mtd_capture_generation(worker,epoch,generation)
    integer, intent(in) :: worker
    integer(int64), intent(out) :: epoch,generation

    call assert_worker(worker)
    epoch=worker_epoch(worker)
    generation=worker_generation(worker)
  end subroutine mtd_capture_generation

  subroutine mtd_try_commit_delta(worker,residual,descriptor,base_epoch, &
       base_generation,delta,outcome)
    integer, intent(in) :: worker
    real, intent(inout) :: residual(mtd_residual_samples)
    type(ft8_subtraction_descriptor), intent(in) :: descriptor
    integer(int64), intent(in) :: base_epoch,base_generation
    real, intent(in) :: delta(mtd_frame_samples)
    integer, intent(out) :: outcome
    logical :: stale

    call assert_worker(worker)
!$omp critical(ft8_mtd_canonical)
    if(history_contains_duplicate(descriptor)) then
       outcome=mtd_commit_duplicate
    else if(history_has_conflict(base_epoch,base_generation,descriptor)) then
       outcome=mtd_commit_conflict
    else
       stale=base_epoch.ne.current_epoch .or. base_generation.ne.canonical_generation
       call apply_fitted_subtraction(delta,descriptor)
       if(stale) then
          outcome=mtd_commit_independent
       else
          outcome=mtd_commit_current
       endif
    endif
    if(outcome.eq.mtd_commit_conflict .or. outcome.eq.mtd_commit_duplicate .or. &
         outcome.eq.mtd_commit_independent) residual=canonical_residual
    worker_generation(worker)=canonical_generation
    worker_epoch(worker)=current_epoch
!$omp end critical(ft8_mtd_canonical)
  end subroutine mtd_try_commit_delta

  subroutine fit_subtraction(worker,residual,descriptor)
    integer, intent(in) :: worker
    real, intent(inout) :: residual(mtd_residual_samples)
    type(ft8_subtraction_descriptor), intent(in) :: descriptor

    call subtractft8var(residual,descriptor%tones, &
         descriptor%frequency,descriptor%dt,subtraction_delta(:,worker))
  end subroutine fit_subtraction

  subroutine apply_fitted_subtraction(delta,descriptor)
    real, intent(in) :: delta(mtd_frame_samples)
    type(ft8_subtraction_descriptor), intent(in) :: descriptor

    integer :: i,j

    do i=1,mtd_frame_samples
       j=descriptor%start_sample+i-1
       if(j.ge.1 .and. j.le.mtd_residual_samples) &
            canonical_residual(j)=canonical_residual(j)+delta(i)
    enddo
    call record_commit(descriptor)
  end subroutine apply_fitted_subtraction

  subroutine record_commit(descriptor)
    type(ft8_subtraction_descriptor), intent(in) :: descriptor

    history_count=history_count+1
    if(history_count.gt.size(history)) error stop 'MTD transaction history capacity exceeded'
    canonical_generation=canonical_generation+1_int64
    history(history_count)=descriptor
    history(history_count)%commit_generation=canonical_generation
  end subroutine record_commit

  logical function history_contains_duplicate(descriptor)
    type(ft8_subtraction_descriptor), intent(in) :: descriptor
    integer :: i

    history_contains_duplicate=.false.
    do i=1,history_count
       if(history(i)%epoch.ne.descriptor%epoch .or. &
            history(i)%pass.ne.descriptor%pass) cycle
       if(mtd_same_signal(history(i),descriptor)) then
          history_contains_duplicate=.true.
          return
       endif
    enddo
  end function history_contains_duplicate

  logical function history_has_conflict(base_epoch,base_generation,descriptor)
    integer(int64), intent(in) :: base_epoch,base_generation
    type(ft8_subtraction_descriptor), intent(in) :: descriptor
    integer :: i

    history_has_conflict=base_epoch.ne.current_epoch .or. &
         descriptor%epoch.ne.current_epoch
    if(history_has_conflict) return
    do i=1,history_count
       if(history(i)%epoch.ne.descriptor%epoch .or. &
            history(i)%commit_generation.le.base_generation) cycle
       if(mtd_descriptors_conflict(history(i),descriptor)) then
          history_has_conflict=.true.
          return
       endif
    enddo
  end function history_has_conflict

  subroutine mtd_make_descriptor(descriptor,ipass,itone,f0,dt)
    type(ft8_subtraction_descriptor), intent(out) :: descriptor
    integer, intent(in) :: ipass,itone(79)
    real, intent(in) :: f0,dt

    descriptor%tones=itone
    descriptor%frequency=f0
    descriptor%dt=dt
    descriptor%frequency_bin=nint(f0/0.0625)
    descriptor%start_sample=mtd_start_sample(dt)
    call mtd_clipped_interval(dt,descriptor%clipped_start,descriptor%clipped_end)
    descriptor%epoch=current_epoch
    descriptor%pass=ipass
    descriptor%commit_generation=0_int64
  end subroutine mtd_make_descriptor

  integer function mtd_start_sample(dt)
    real, intent(in) :: dt

    mtd_start_sample=int(dt*12000.0+1.0)
  end function mtd_start_sample

  subroutine mtd_clipped_interval(dt,first_sample,last_sample)
    real, intent(in) :: dt
    integer, intent(out) :: first_sample,last_sample
    integer :: start_sample

    start_sample=mtd_start_sample(dt)
    first_sample=max(1,start_sample)
    last_sample=min(mtd_residual_samples,start_sample+mtd_frame_samples-1)
  end subroutine mtd_clipped_interval

  logical function mtd_same_signal(left,right)
    type(ft8_subtraction_descriptor), intent(in) :: left,right

    mtd_same_signal=left%frequency_bin.eq.right%frequency_bin .and. &
         left%start_sample.eq.right%start_sample .and. all(left%tones.eq.right%tones)
  end function mtd_same_signal

  logical function mtd_descriptors_conflict(left,right)
    type(ft8_subtraction_descriptor), intent(in) :: left,right
    logical :: overlap

    overlap=left%clipped_start.le.left%clipped_end .and. &
         right%clipped_start.le.right%clipped_end .and. &
         left%clipped_start.le.right%clipped_end .and. &
         right%clipped_start.le.left%clipped_end
    mtd_descriptors_conflict=overlap .and. &
         abs(left%frequency-right%frequency).lt.mtd_interaction_bandwidth
  end function mtd_descriptors_conflict

  subroutine mtd_transform_phase(ipass)
    integer, intent(in) :: ipass

!$omp critical(ft8_mtd_canonical)
    if(ipass.eq.4) then
       post_pass3_residual=canonical_residual
       call mtd_forward_half_sample(canonical_residual)
    else if(ipass.eq.7) then
       canonical_residual=post_pass3_residual
       call mtd_backward_half_sample(canonical_residual)
    else
       error stop 'Invalid MTD transform pass'
    endif
    current_epoch=current_epoch+1_int64
    canonical_generation=canonical_generation+1_int64
!$omp end critical(ft8_mtd_canonical)
  end subroutine mtd_transform_phase

  subroutine mtd_finish(output_residual)
    real, intent(out) :: output_residual(:)

    if(size(output_residual).ne.mtd_residual_samples) error stop &
         'MTD output residual has an unexpected sample count'
!$omp critical(ft8_mtd_canonical)
    output_residual=canonical_residual
!$omp end critical(ft8_mtd_canonical)
  end subroutine mtd_finish

  subroutine mtd_forward_half_sample(residual)
    real, intent(inout) :: residual(:)
    integer :: i

    if(size(residual).ne.mtd_residual_samples) error stop &
         'MTD forward transform has an unexpected sample count'
    do i=1,mtd_residual_samples-1
       residual(i)=(residual(i)+residual(i+1))/2.0
    enddo
  end subroutine mtd_forward_half_sample

  subroutine mtd_backward_half_sample(residual)
    real, intent(inout) :: residual(:)
    integer :: i

    if(size(residual).ne.mtd_residual_samples) error stop &
         'MTD backward transform has an unexpected sample count'
    do i=mtd_residual_samples,2,-1
       residual(i)=(residual(i-1)+residual(i))/2.0
    enddo
  end subroutine mtd_backward_half_sample

  subroutine assert_worker(worker)
    integer, intent(in) :: worker

    if(worker.lt.1 .or. worker.gt.active_workers) error stop 'Invalid MTD worker index'
  end subroutine assert_worker

end module ft8_mtd_residual
