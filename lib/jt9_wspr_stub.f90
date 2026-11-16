! SPDX-License-Identifier: GPL-3.0-or-later
! Reject WSPR in jt9; wsjt-codec decodes WSPR files (lib/codec_wspr_file.c).
function wsjt_codec_wspr_file(infile, data_dir, dial_mhz, depth, decode_finished) &
     bind(C, name='wsjt_codec_wspr_file')
  use, intrinsic :: iso_c_binding, only: c_char, c_double, c_int
  use, intrinsic :: iso_fortran_env, only: error_unit
  character(kind=c_char), intent(in) :: infile(*), data_dir(*)
  real(c_double), value :: dial_mhz
  integer(c_int), value :: depth, decode_finished
  integer(c_int) :: wsjt_codec_wspr_file
  wsjt_codec_wspr_file = 2
  write(error_unit,'(a)') 'jt9: WSPR is not available in jt9. Use `wsjt-codec`.'
  stop 2
end function wsjt_codec_wspr_file

function wsjt_codec_wspr_data_dir(data_dir) bind(C, name='wsjt_codec_wspr_data_dir')
  use, intrinsic :: iso_c_binding, only: c_char, c_int
  use, intrinsic :: iso_fortran_env, only: error_unit
  character(kind=c_char), intent(in) :: data_dir(*)
  integer(c_int) :: wsjt_codec_wspr_data_dir
  wsjt_codec_wspr_data_dir = 2
  write(error_unit,'(a)') 'jt9: WSPR is not available in jt9. Use `wsjt-codec`.'
  stop 2
end function wsjt_codec_wspr_data_dir
