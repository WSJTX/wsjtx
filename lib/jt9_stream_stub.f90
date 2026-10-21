! SPDX-License-Identifier: GPL-3.0-or-later
! Reject stream mode in jt9; wsjt-codec is the stream host.
subroutine jt9_stream(shared_data, mode, TRperiod)
  use, intrinsic :: iso_fortran_env, only: error_unit
  include 'jt9com.f90'
  type(dec_data) :: shared_data
  integer        :: mode
  real(8)        :: TRperiod
  write(error_unit,'(a)') 'jt9: --stream is not available in jt9. '// &
       'Use `wsjt-codec --stream`.'
  stop 2
end subroutine jt9_stream

logical function jt9_stream_available()
  jt9_stream_available = .false.
end function jt9_stream_available
