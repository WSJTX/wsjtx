subroutine decoder_ipc_fortran_probe(address, values) bind(C)
  use, intrinsic :: iso_c_binding, only: c_ptr, c_f_pointer, c_int, c_sizeof
  include 'jt9com.f90'

  type(c_ptr), value :: address
  integer(c_int), intent(out) :: values(10)
  type(shared_dec_data), pointer :: shared

  call c_f_pointer(address, shared)
  values(1) = c_sizeof(shared%control)
  values(2) = c_sizeof(shared%payload%params)
  values(3) = c_sizeof(shared%payload)
  values(4) = c_sizeof(shared)
  values(5) = DECODER_IPC_VERSION
  values(6) = DECODER_IPC_IDLE
  values(7) = DECODER_IPC_READY
  values(8) = DECODER_IPC_DECODING
  values(9) = DECODER_IPC_COMPLETE
  values(10) = DECODER_IPC_SHUTDOWN

  shared%control%generation = shared%control%generation + 1
  shared%payload%id2(1) = 1234
end subroutine decoder_ipc_fortran_probe
