program test_q65_workspace

  use q65_workspace, only: Q65_MAXFFT, Q65_NN, q65_workspace_type

  implicit none

  integer, parameter :: npts2=400
  integer, parameter :: ll=256
  type(q65_workspace_type) :: workspace

  call workspace%ensure(npts2,ll)
  call assert_workspace_sizes(workspace,npts2,ll)

  workspace%c0(0)=cmplx(3.0,4.0)
  workspace%s3(1)=7.0
  workspace%cs(0)=cmplx(5.0,6.0)

  call workspace%ensure(npts2/2,ll/2)
  call assert_workspace_sizes(workspace,npts2/2,ll/2)
  if(workspace%c0(0).ne.cmplx(3.0,4.0)) error stop 'c0 was reallocated'
  if(workspace%s3(1).ne.7.0) error stop 's3 was reallocated'
  if(workspace%cs(0).ne.cmplx(5.0,6.0)) error stop 'cs was reallocated'

  call workspace%ensure(2*npts2,2*ll)
  call assert_workspace_sizes(workspace,2*npts2,2*ll)

contains

  subroutine assert_workspace_sizes(workspace,npts2,ll)
    type(q65_workspace_type), intent(in) :: workspace
    integer, intent(in) :: npts2,ll

    if(.not.allocated(workspace%c0)) error stop 'c0 is not allocated'
    if(.not.allocated(workspace%s3)) error stop 's3 is not allocated'
    if(.not.allocated(workspace%cs)) error stop 'cs is not allocated'
    if(lbound(workspace%c0,1).ne.0) error stop 'c0 lower bound changed'
    if(lbound(workspace%cs,1).ne.0) error stop 'cs lower bound changed'
    if(lbound(workspace%s3,1).ne.1) error stop 's3 lower bound changed'
    if(size(workspace%c0).lt.npts2) error stop 'c0 capacity is too small'
    if(size(workspace%s3).lt.ll*Q65_NN) error stop 's3 capacity is too small'
    if(size(workspace%cs).ne.Q65_MAXFFT) error stop 'cs size changed'
  end subroutine assert_workspace_sizes

end program test_q65_workspace
