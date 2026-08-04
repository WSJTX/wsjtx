module decoder_ipc_atomic
  use, intrinsic :: iso_c_binding, only: c_int
  implicit none

  integer(c_int), parameter :: DECODER_IPC_CLAIM_INVALID = -2
  integer(c_int), parameter :: DECODER_IPC_CLAIM_INCOMPATIBLE = -1
  integer(c_int), parameter :: DECODER_IPC_CLAIM_NONE = 0
  integer(c_int), parameter :: DECODER_IPC_CLAIMED = 1
  integer(c_int), parameter :: DECODER_IPC_CLAIM_SHUTDOWN = 2

  interface
     function decoder_ipc_control_try_claim(generation, state, version, &
          claimed_generation) bind(C)
       import c_int
       integer(c_int), intent(inout) :: generation
       integer(c_int), intent(inout) :: state
       integer(c_int), intent(in) :: version
       integer(c_int), intent(out) :: claimed_generation
       integer(c_int) :: decoder_ipc_control_try_claim
     end function decoder_ipc_control_try_claim

     function decoder_ipc_control_finish(generation, state, version, &
          request_generation) bind(C)
       import c_int
       integer(c_int), intent(in) :: generation
       integer(c_int), intent(inout) :: state
       integer(c_int), intent(in) :: version
       integer(c_int), value :: request_generation
       integer(c_int) :: decoder_ipc_control_finish
     end function decoder_ipc_control_finish
  end interface
end module decoder_ipc_atomic
