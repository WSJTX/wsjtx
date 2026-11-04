module jt65_mod

  use, intrinsic :: iso_c_binding
  use FFTW3

  save

  integer param(0:9)
  integer mrs(63)
  integer mrs2(63)
  integer mdat(126),mref(126,2),mdat2(126),mref2(126,2)    !From prcom

  real s1(-255:256,126)
  real s3a(64,63)
  real pr(126)
  real width

  integer, parameter :: jt65_max_calls=10000
  type jt65_average_entry
    integer :: utc=-1,frequency=0,polarity=0
    real :: sync=0.,dt=0.
    logical :: used=.false.
  end type

  type jt65_workspace
    real, allocatable :: audio(:),spectra(:,:),big_input(:),filter(:)
    complex, allocatable :: filter_fft(:),filtered(:)
    complex, allocatable :: cx(:),cx1(:),c5x(:),integral(:)
    complex, allocatable :: cref(:),camp(:),cfilt(:),cw(:)
    real, allocatable :: history1(:,:,:),history3(:,:,:)
    type(jt65_average_entry), allocatable :: averages(:)
    integer(c_int64_t), allocatable :: reception_ids(:)
    character(len=12), allocatable :: calls(:)
    character(len=4), allocatable :: grids(:)
    integer(c_int8_t), allocatable :: symbols2(:,:)
    character(len=22), allocatable :: messages(:)
    integer :: utc0=-999,frequency0=-999,saved=0,nsum=0
    integer :: submode=-1,profile=-1,average_count=0
    integer :: patience=-1,threads=-1,sub_patience=-1,sub_threads=-1
    integer :: call_revision=0,hint_revision=-1,hint_count=0
    integer(c_int64_t) :: input_id=0,last_average_input=0
    character(len=6) :: hint_mycall='',hint_hiscall='',hint_grid=''
    logical :: clear_average=.true.,afc_valid=.false.,filter_valid=.false.
    real :: afc_parameters(3)=0.
    type(c_ptr) :: big_plan=c_null_ptr,short_plan=c_null_ptr,filter_plan=c_null_ptr
    type(c_ptr) :: subtract_forward=c_null_ptr,subtract_inverse=c_null_ptr,window_plan=c_null_ptr
  end type
  ! DSP helpers share the active owner; JT65 calls must remain serialized.
  type(jt65_workspace), pointer :: jt65_work=>null()
  type(jt65_workspace), target :: fallback_workspace
  integer :: jt65_ap_epoch=0

contains

  subroutine ensure_jt65_workspace()
    if(.not.associated(jt65_work)) jt65_work=>fallback_workspace
  end subroutine

  subroutine clear_jt65_averages(work)
    type(jt65_workspace), intent(inout) :: work
    work%utc0=-999
    work%frequency0=-999
    work%saved=0
    work%nsum=0
    work%last_average_input=0
    work%average_count=0
    work%clear_average=.true.
    if(allocated(work%averages)) work%averages%utc=-1
  end subroutine

  subroutine destroy_jt65_plans(work)
    use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
    type(jt65_workspace), intent(inout) :: work
    !$omp critical(fftw)
    call wsjt_tsan_acquire_fftw()
    if(c_associated(work%big_plan)) call fftwf_destroy_plan(work%big_plan)
    if(c_associated(work%short_plan)) call fftwf_destroy_plan(work%short_plan)
    if(c_associated(work%filter_plan)) call fftwf_destroy_plan(work%filter_plan)
    if(c_associated(work%subtract_forward)) call fftwf_destroy_plan(work%subtract_forward)
    if(c_associated(work%subtract_inverse)) call fftwf_destroy_plan(work%subtract_inverse)
    if(c_associated(work%window_plan)) call fftwf_destroy_plan(work%window_plan)
    call wsjt_tsan_release_fftw()
    !$omp end critical(fftw)
    work%big_plan=c_null_ptr
    work%short_plan=c_null_ptr
    work%filter_plan=c_null_ptr
    work%subtract_forward=c_null_ptr
    work%subtract_inverse=c_null_ptr
    work%window_plan=c_null_ptr
    work%filter_valid=.false.
  end subroutine

end module jt65_mod
