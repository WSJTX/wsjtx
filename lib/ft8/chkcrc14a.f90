subroutine chkcrc14a(decoded,nbadcrc)
  use, intrinsic :: iso_fortran_env, only: int8
  use crc, only: crc14
  implicit none
  integer(int8), intent(in) :: decoded(91)
  integer, intent(out) :: nbadcrc
  integer(int8) :: crc_bits(96)

  ! CRC division includes 5 zero padding bits between the payload and check bits.
  crc_bits=0
  crc_bits(1:77)=decoded(1:77)
  crc_bits(83:96)=decoded(78:91)
  nbadcrc=merge(0,1,crc14(crc_bits).eq.0)
end subroutine chkcrc14a
