module q65_workspace

  implicit none

  integer, parameter :: Q65_NN=63
  integer, parameter :: Q65_MAXFFT=20736

  type :: q65_workspace_type
     complex, allocatable :: c0(:)
     real, allocatable :: s3(:)
     complex, allocatable :: cs(:)
   contains
     procedure :: ensure => ensure_q65_workspace
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

end module q65_workspace
