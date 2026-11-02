module jt9_downsample
  use, intrinsic :: iso_c_binding
  use FFTW3
  implicit none
  private
  public :: jt9_downsample_workspace, downsam9

  integer, parameter :: NFFT1=653184, NFFT2=1512

  type :: jt9_downsample_workspace
     private
     type(c_ptr) :: plan=c_null_ptr, storage=c_null_ptr
     real(c_float), pointer :: samples(:)=>null()
     complex(c_float_complex), allocatable :: spectrum(:)
     real, allocatable :: power(:)
     logical :: valid=.false.
     integer :: sample_count=0
     integer :: plan_patience=-1, plan_threads=0
   contains
     procedure :: initialize
     procedure :: invalidate
     procedure :: clear
     final :: finalize
  end type jt9_downsample_workspace

contains

  subroutine initialize(this)
    use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
    class(jt9_downsample_workspace), intent(inout) :: this
    integer :: npatience,nthreads,nflags
    common/patience/npatience,nthreads

    if(c_associated(this%plan)) then
       if(this%plan_patience==npatience.and.this%plan_threads==nthreads) return
       call this%clear()
    endif
    nflags=FFTW_ESTIMATE
    if(npatience.eq.1) nflags=FFTW_ESTIMATE_PATIENT
    if(npatience.eq.2) nflags=FFTW_MEASURE
    if(npatience.eq.3) nflags=FFTW_PATIENT
    if(npatience.eq.4) nflags=FFTW_EXHAUSTIVE
    allocate(this%spectrum(0:NFFT1/2),this%power(5000))
    !$omp critical(fftw)
    call wsjt_tsan_acquire_fftw()
    this%storage=fftwf_alloc_real(int(NFFT1,c_size_t))
    call c_f_pointer(this%storage,this%samples,[NFFT1])
    this%samples(0:NFFT1-1) => this%samples
    call fftwf_plan_with_nthreads(nthreads)
    this%plan=fftwf_plan_dft_r2c_1d(NFFT1,this%samples,this%spectrum,nflags)
    call fftwf_plan_with_nthreads(1)
    call wsjt_tsan_release_fftw()
    !$omp end critical(fftw)
    this%plan_patience=npatience
    this%plan_threads=nthreads
  end subroutine initialize

  subroutine invalidate(this)
    class(jt9_downsample_workspace), intent(inout) :: this
    this%valid=.false.
  end subroutine invalidate

  subroutine clear(this)
    use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
    class(jt9_downsample_workspace), intent(inout) :: this
    !$omp critical(fftw)
    call wsjt_tsan_acquire_fftw()
    if(c_associated(this%plan)) call fftwf_destroy_plan(this%plan)
    if(c_associated(this%storage)) call fftwf_free(this%storage)
    call wsjt_tsan_release_fftw()
    !$omp end critical(fftw)
    this%plan=c_null_ptr
    this%storage=c_null_ptr
    nullify(this%samples)
    if(allocated(this%spectrum)) deallocate(this%spectrum,this%power)
    this%valid=.false.
    this%sample_count=0
  end subroutine clear

  subroutine finalize(this)
    type(jt9_downsample_workspace), intent(inout) :: this
    call this%clear()
  end subroutine finalize

subroutine downsam9(workspace,id2,npts8,fpk,c2)

! Downsample by 432 to 16 samples per JT9 symbol, mixing fpk to zero frequency.

  use timer_module, only: timer
  implicit none

  type(jt9_downsample_workspace), intent(inout) :: workspace
  integer, intent(in) :: npts8
  integer(c_short), intent(in) :: id2(0:*)
  real, intent(in) :: fpk
  complex, intent(out) :: c2(0:NFFT2-1)
  integer :: npts, nadd, i, j, n, nh2, nf, i0, nw, ia, ib
  real :: df1, avenoise, fac

  df1=12000.0/NFFT1
  npts=8*npts8
  npts=min(npts,NFFT1)

  call workspace%initialize()

  if(.not.workspace%valid .or. workspace%sample_count.ne.npts) then
     workspace%samples(0:npts-1)=id2(0:npts-1)
     workspace%samples(npts:NFFT1-1)=0.
     call timer('FFTbig9 ',0)
     call fftwf_execute_dft_r2c(workspace%plan,workspace%samples,workspace%spectrum)
     call timer('FFTbig9 ',1)

     nadd=int(1.0/df1)
     workspace%power=0.
     do i=1,5000
        j=int((i-1)/df1)
        do n=1,nadd
           j=j+1
           workspace%power(i)=workspace%power(i)+real(workspace%spectrum(j))**2 + aimag(workspace%spectrum(j))**2
        enddo
     enddo
     workspace%valid=.true.
     workspace%sample_count=npts
  endif

  nh2=NFFT2/2
  nf=nint(fpk)
  i0=int(fpk/df1)

  nw=100
  ia=max(1,nf-nw)
  ib=min(5000,nf+nw)
  call pctile(workspace%power(ia),ib-ia+1,40,avenoise)

  if(avenoise.le.0.) then
     c2=0.
     return
  endif
  fac=sqrt(1.0/avenoise)
  do i=0,NFFT2-1
     j=i0+i
     if(i.gt.nh2) j=j-NFFT2
     c2(i)=0.
     if(j.ge.0 .and. j.le.NFFT1/2) c2(i)=fac*workspace%spectrum(j)
  enddo
  call four2a(c2,NFFT2,1,1,1)              !FFT back to time domain

  return
end subroutine downsam9

end module jt9_downsample
