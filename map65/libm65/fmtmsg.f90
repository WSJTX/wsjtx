module fmtmsg_mod
  implicit none
contains

subroutine fmtmsg(msg, iz)
  implicit none

  !--------------------------------------------------------------------
  ! Arguments
  !--------------------------------------------------------------------
  character(len=22), intent(inout) :: msg
  integer,          intent(out)    :: iz

  !--------------------------------------------------------------------
  ! Locals
  !--------------------------------------------------------------------
  integer :: i, iter, ib2

  !--------------------------------------------------------------------
  ! Convert all letters to upper case and track last nonblank
  !--------------------------------------------------------------------
  iz = 22
  do i = 1, 22
     if (msg(i:i) >= 'a' .and. msg(i:i) <= 'z') then
        msg(i:i) = char(ichar(msg(i:i)) + ichar('A') - ichar('a'))
     end if
     if (msg(i:i) /= ' ') iz = i
  end do

  !--------------------------------------------------------------------
  ! Collapse multiple blanks into one (up to 5 passes)
  !--------------------------------------------------------------------
  do iter = 1, 5
     ib2 = index(msg(1:iz), '  ')
     if (ib2 < 1) exit
     msg = msg(1:ib2) // msg(ib2+2:)
     iz = iz - 1
  end do

end subroutine fmtmsg

end module fmtmsg_mod
