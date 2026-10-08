module datcom_ptrs_mod
  use iso_c_binding
  use debug_log, only: dbg, itoa
  use npar_ptrs_mod, only: snapshot_fill_begin, snapshot_fill_end
  implicit none

  ! Fortran pointer targets
  real(c_float), pointer, contiguous :: dd(:,:) => null()      ! [4, 5760000]
  real(c_float), pointer, contiguous :: ss(:,:,:) => null()     ! [4, 322, NFFT]
  real(c_float), pointer, contiguous :: savg(:,:) => null()     ! [4, NFFT]

  ! Two decoder snapshot sets, one per automatic pass (1 = early, nhsym1=280;
  ! 2 = final, nhsym2=302). Each is filled by snapshot_buffers() at the moment
  ! its pass is triggered, before the live buffers can be reset for the next
  ! cycle, so a late-starting pass still decodes complete data.
  type :: snap_set
     real(c_float), pointer, contiguous :: dd(:,:)     => null()   ! [4, nsmax]
     real(c_float), pointer, contiguous :: ss(:,:,:)   => null()   ! [4, 322, NFFT]
     real(c_float), pointer, contiguous :: savg(:,:)   => null()   ! [4, NFFT]
  end type snap_set
  type(snap_set), save :: snap(2)

  ! Snapshot selected for the pass now being decoded (set by decode0)
  real(c_float), pointer, contiguous :: dd_old(:,:) => null()
  real(c_float), pointer, contiguous :: ss_old(:,:,:) => null()
  real(c_float), pointer, contiguous :: savg_old(:,:) => null()

  ! Pointers used by decode0 / narrowband decoder
  real(c_float), pointer, contiguous :: dd_use(:,:)  => null()
  real(c_float), pointer, contiguous :: ss_use(:,:,:)=> null()
  real(c_float), pointer, contiguous :: savg_use(:,:)=> null()

  ! Scalar values
  integer, parameter :: NFFT=32768              !Length of FFTs
  integer(c_int) :: junk1 = 0, junk2 = 0, quitid = 0
  
contains

  ! Copy the live buffers into snapshot set idx (1 or 2). Called from dataSink()
  ! on the GUI thread, which is also the thread that runs symspec, so ss and
  ! savg cannot change during the copy. The sound-in thread writes dd only
  ! forward of ksamp, so dd(:,1:ksamp+lookahead) is stable.
  subroutine snapshot_buffers(idx, ksamp, nhsym_snap) bind(C, name="snapshot_buffers")
    integer(c_int), value :: idx, ksamp, nhsym_snap
    integer :: ncopy, ncol
    logical :: ok
    integer, parameter :: lookahead = 512      ! live_lookahead_samples in symspec
    if (idx < 1 .or. idx > 2) return
    call snapshot_fill_begin(idx, ok)
    if (.not. ok) then
      call dbg('snapshot ' // itoa(idx) // ' skipped: slot still in use by the running pass')
      return
    endif
    if (.not. associated(snap(idx)%dd) .or. .not. associated(dd)) return
    ncopy = max(0, min(size(dd,2), ksamp + lookahead))
    ncol  = max(0, min(size(ss,2), nhsym_snap))
    snap(idx)%dd(:,1:ncopy) = dd(:,1:ncopy)
    snap(idx)%dd(:,ncopy+1:) = 0.
    snap(idx)%ss(:,1:ncol,:) = ss(:,1:ncol,:)
    snap(idx)%ss(:,ncol+1:,:) = 0.
    snap(idx)%savg(:,:) = savg(:,:)
    call snapshot_fill_end(idx)
  end subroutine

  ! Helper subroutine to copy fixed-length Fortran string to C buffer
  subroutine move_chars(dest, dest_len, src)
    integer(c_int), intent(in) :: dest_len
    character(kind=c_char), intent(out) :: dest(dest_len)
    character(len=*), intent(in) :: src
    integer :: i, n

    n = min(len(src), dest_len)
    do i = 1, n
      dest(i) = src(i:i)
    end do
  end subroutine move_chars
  
  subroutine set_dd_ptr(ptr, dim1, dim2) bind(C, name="set_dd_ptr")
    use iso_c_binding
    implicit none
    type(c_ptr), value :: ptr
    integer(c_int), value :: dim1, dim2
    call c_f_pointer(ptr, dd, [dim1, dim2])
    print *, "Fortran: dd pointer associated with dimensions", dim1, dim2
  end subroutine

  subroutine set_ss_ptr(ptr, dim1, dim2, dim3) bind(C, name="set_ss_ptr")
    use iso_c_binding
    implicit none
    type(c_ptr), value :: ptr
    integer(c_int), value :: dim1, dim2, dim3
    call c_f_pointer(ptr, ss, [dim1, dim2, dim3])
    print *, "Fortran: ss pointer associated with dimensions", dim1, dim2, dim3
  end subroutine

  subroutine set_savg_ptr(ptr, dim1, dim2) bind(C, name="set_savg_ptr")
    use iso_c_binding
    implicit none
    type(c_ptr), value :: ptr
    integer(c_int), value :: dim1, dim2
    call c_f_pointer(ptr, savg, [dim1, dim2])
    print *, "Fortran: savg pointer associated with dimensions", dim1, dim2
  end subroutine 
  
  subroutine set_snap_ptr(idx, dd_ptr, ss_ptr, savg_ptr, ndd, nfft_s) bind(C, name="set_snap_ptr")
    use iso_c_binding
    implicit none
    integer(c_int), value :: idx, ndd, nfft_s
    type(c_ptr), value :: dd_ptr, ss_ptr, savg_ptr
    if (idx < 1 .or. idx > 2) return
    call c_f_pointer(dd_ptr,   snap(idx)%dd,   [4, ndd])
    call c_f_pointer(ss_ptr,   snap(idx)%ss,   [4, 322, nfft_s])
    call c_f_pointer(savg_ptr, snap(idx)%savg, [4, nfft_s])
  end subroutine 
  
  function get_junk1() bind(C, name="get_junk1")
    integer(c_int) :: get_junk1
    get_junk1 = junk1
  end function

  function get_junk2() bind(C, name="get_junk2")
    integer(c_int) :: get_junk2
    get_junk2 = junk2
  end function
  
subroutine set_quitid(val) bind(C, name="set_quitid")
    use iso_c_binding
    integer(c_long_long), value :: val
    quitid = val
    ! print*,'DATCOM_PTRS_MOD QUITID is:',quitid
  end subroutine

  subroutine set_junk1(val) bind(C, name="set_junk1")
    use iso_c_binding
    integer(c_int), value :: val
    junk1 = val
  end subroutine

  subroutine set_junk2(val) bind(C, name="set_junk2")
    use iso_c_binding
    integer(c_int), value :: val
    junk2 = val
  end subroutine

end module datcom_ptrs_mod
