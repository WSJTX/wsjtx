module pctile_mod
   implicit none
contains

subroutine pctile(x, npts, npct, xpct)
   use shell_mod
   implicit none

   !==== Dummy arguments =====================================================
   integer, intent(in)  :: npts, npct
   real,    intent(in)  :: x(npts)
   real,    intent(out) :: xpct

   !==== Local variables =====================================================
   real, allocatable :: tmp(:)
   integer :: j

   !==== Input validation ====================================================
   if (npts < 1 .or. npct < 0 .or. npct > 100) then
      xpct = 1.0
      return
   endif

   allocate(tmp(npts))
   tmp = x

   call shell(npts, tmp)

   j = nint(npts * 0.01 * npct)
   if (j < 1) j = 1
   if (j > npts) j = npts

   xpct = tmp(j)

   deallocate(tmp)
end subroutine pctile

end module pctile_mod
