module q65_workspace

  use iso_c_binding, only: c_int64_t
  use fftw3
  implicit none

  integer, parameter :: Q65_NN=63
  integer, parameter :: Q65_MAXFFT=20736

  type :: q65_workspace_type
     complex, allocatable :: c0(:)
     real, allocatable :: s3(:)
     complex, allocatable :: cs(:)
     integer(c_int64_t) :: plan=0
     integer :: plan_length=0,planning=-1
   contains
     procedure :: ensure => ensure_q65_workspace
     procedure :: prepare_fft
     procedure :: destroy
     final :: finalize
  end type q65_workspace_type

contains

  subroutine ensure_q65_workspace(this,npts2,ll)
    class(q65_workspace_type), intent(inout) :: this
    integer, intent(in) :: npts2,ll
    integer n3

    ! Keep the largest capacity so repeated searches do not churn the allocator.
    n3=ll*Q65_NN

    if(.not.allocated(this%c0)) then
       allocate(this%c0(0:npts2-1))
    else if(size(this%c0).lt.npts2) then
       deallocate(this%c0)
       allocate(this%c0(0:npts2-1))
    endif

    if(.not.allocated(this%s3)) then
       allocate(this%s3(1:n3))
    else if(size(this%s3).lt.n3) then
       deallocate(this%s3)
       allocate(this%s3(1:n3))
    endif

    if(.not.allocated(this%cs)) allocate(this%cs(0:Q65_MAXFFT-1))
  end subroutine ensure_q65_workspace

  subroutine prepare_fft(this,length)
    use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
    class(q65_workspace_type), intent(inout) :: this
    integer, intent(in) :: length
    integer npatience,nthreads,flags
    common/patience/npatience,nthreads
    if(this%plan/=0.and.this%plan_length==length.and.this%planning==npatience) return
    !$omp critical(fftw)
    call wsjt_tsan_acquire_fftw()
    if(this%plan/=0) call sfftw_destroy_plan(this%plan)
    flags=FFTW_ESTIMATE
    if(npatience==1) flags=FFTW_ESTIMATE_PATIENT
    if(npatience==2) flags=FFTW_MEASURE
    if(npatience==3) flags=FFTW_PATIENT
    if(npatience==4) flags=FFTW_EXHAUSTIVE
    call sfftw_plan_dft_1d(this%plan,length,this%cs,this%cs,FFTW_FORWARD,flags)
    call wsjt_tsan_release_fftw()
    !$omp end critical(fftw)
    this%plan_length=length
    this%planning=npatience
  end subroutine

  subroutine destroy(this)
    use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
    class(q65_workspace_type), intent(inout) :: this
    !$omp critical(fftw)
    call wsjt_tsan_acquire_fftw()
    if(this%plan/=0) call sfftw_destroy_plan(this%plan)
    call wsjt_tsan_release_fftw()
    !$omp end critical(fftw)
    this%plan=0
    if(allocated(this%c0)) deallocate(this%c0)
    if(allocated(this%s3)) deallocate(this%s3)
    if(allocated(this%cs)) deallocate(this%cs)
  end subroutine

  subroutine finalize(this)
    type(q65_workspace_type), intent(inout) :: this
    call this%destroy()
  end subroutine

end module q65_workspace
