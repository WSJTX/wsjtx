module shmem
  ! external routines wrapping the Qt QSharedMemory class
  interface
     function shmem_create (size) bind(C, name="shmem_create")
       use iso_c_binding, only: c_bool, c_int
       logical(c_bool) :: shmem_create
       integer(c_int), value, intent(in) :: size
     end function shmem_create

     subroutine shmem_setkey (key) bind(C, name="shmem_setkey")
       use iso_c_binding, only: c_bool, c_char
       character(kind=c_char), intent(in) :: key(*)
     end subroutine shmem_setkey

     function shmem_attach () bind(C, name="shmem_attach")
       use iso_c_binding, only: c_bool
       logical(c_bool) :: shmem_attach
     end function shmem_attach

     function shmem_address() bind(C, name="shmem_address")
       use, intrinsic :: iso_c_binding, only: c_ptr
       type(c_ptr) :: shmem_address
     end function shmem_address

     function shmem_size() bind(C, name="shmem_size")
       use, intrinsic :: iso_c_binding, only: c_int
       integer(c_int) :: shmem_size
     end function shmem_size

     function shmem_lock () bind(C, name="shmem_lock")
       use iso_c_binding, only: c_bool
       logical(c_bool) :: shmem_lock
     end function shmem_lock

     function shmem_unlock () bind(C, name="shmem_unlock")
       use iso_c_binding, only: c_bool
       logical(c_bool) :: shmem_unlock
     end function shmem_unlock

     function shmem_detach () bind(C, name="shmem_detach")
       use iso_c_binding, only: c_bool
       logical(c_bool) :: shmem_detach
     end function shmem_detach
  end interface
end module shmem
