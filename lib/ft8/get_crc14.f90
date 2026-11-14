subroutine get_crc14(mc,len,ncrc)
  use, intrinsic :: iso_fortran_env, only: int8
  use crc, only: crc14
  implicit none
  integer, intent(in) :: len
  integer(int8), intent(in) :: mc(len)
  integer, intent(out) :: ncrc

  ncrc=crc14(mc)
end subroutine get_crc14
