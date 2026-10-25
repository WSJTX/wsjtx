! SPDX-License-Identifier: GPL-3.0-or-later
! Reject stream mode in jt9; wsjt-codec is the stream host.
subroutine jt9_stream(shared_data, mode, TRperiod)
  use, intrinsic :: iso_fortran_env, only: error_unit
  include 'jt9com.f90'
  type(dec_data) :: shared_data
  integer        :: mode
  real(8)        :: TRperiod
  write(error_unit,'(a)') 'jt9: --stream is not available in jt9. Use `wsjt-codec`.'
  stop 2
end subroutine jt9_stream

logical function jt9_stream_available()
  jt9_stream_available = .false.
end function jt9_stream_available

! lib/jt9.f90 asks only when jt9_stream_available() is true.
function stream_stdin_is_terminal() bind(C, name='stream_stdin_is_terminal')
  use, intrinsic :: iso_c_binding, only: c_int
  integer(c_int) :: stream_stdin_is_terminal
  stream_stdin_is_terminal = 0
end function stream_stdin_is_terminal
