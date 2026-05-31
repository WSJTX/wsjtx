module encode65_mod
  use packjt, only:packmsg
  use graycode_mod, only: graycode
  use rs_mod, only: rs_encode_
  use interleave63_mod, only:interleave63
  implicit none
contains

subroutine encode65(message, sent)
  
  !--------------------------------------------------------------------
  ! Arguments
  !--------------------------------------------------------------------
  character(len=22), intent(in)  :: message
  integer,          intent(out) :: sent(63)

  !--------------------------------------------------------------------
  ! Locals
  !--------------------------------------------------------------------
  integer :: dgen(12)
  integer :: itype

  !--------------------------------------------------------------------
  ! Encode message into 65-bit payload
  !--------------------------------------------------------------------
  call packmsg(message, dgen, itype)
  call rs_encode_(dgen, sent)
  call interleave63(sent, 1)
  call graycode(sent, 63, 1)

end subroutine encode65

end module encode65_mod
