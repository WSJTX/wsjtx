module cacb_mod
   implicit none
   complex, allocatable :: ca(:), cb(:)  ! FFTs of input

contains
   subroutine init_cacb(n)
      use iso_fortran_env
      integer, intent(in) :: n
      if (.not. allocated(ca)) allocate (ca(n))
      if (.not. allocated(cb)) allocate (cb(n))
   end subroutine init_cacb

end module cacb_mod
