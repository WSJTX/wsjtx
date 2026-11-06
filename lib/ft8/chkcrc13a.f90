subroutine chkcrc13a(decoded,nbadcrc)
  use, intrinsic :: iso_fortran_env, only: int8
  use crc, only: crc13
  implicit none
  integer(int8), intent(in) :: decoded(90)
  integer, intent(out) :: nbadcrc
  integer(int8) :: crc_bits(96)

  ! CRC division includes 6 zero padding bits between the payload and check bits.
  crc_bits=0
  crc_bits(1:77)=decoded(1:77)
  crc_bits(84:96)=decoded(78:90)
  nbadcrc=merge(0,1,crc13(crc_bits).eq.0)
end subroutine chkcrc13a
