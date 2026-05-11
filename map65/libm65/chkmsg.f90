module chkmsg_mod
  implicit none
contains

subroutine chkmsg(message, cok, nspecial, flip)
  implicit none

  !--------------------------------------------------------------------
  ! Arguments
  !--------------------------------------------------------------------
  character(len=22), intent(inout) :: message   ! modified if OOO/OO removed
  character(len=3),  intent(out)   :: cok
  integer,      intent(out)   :: nspecial
  real,         intent(out)   :: flip

  !--------------------------------------------------------------------
  ! Locals
  !--------------------------------------------------------------------
  integer :: i

  !--------------------------------------------------------------------
  ! Initialization
  !--------------------------------------------------------------------
  nspecial = 0
  flip     = 1.0d0
  cok      = '   '

  !--------------------------------------------------------------------
  ! Find last non‑blank character
  !--------------------------------------------------------------------
  do i = 22, 1, -1
     if (message(i:i) .ne. ' ') exit
  enddo
  if (i == 0) i = 22   ! defensive fallback

  !--------------------------------------------------------------------
  ! Detect trailing OOO / OO shorthand
  !--------------------------------------------------------------------
  if (i >= 11) then
     if ( (message(i-3:i) == ' OOO') .or. (message(20:22) == ' OO') ) then
        cok  = 'OOO'
        flip = -1.0d0

        if (message(20:22) == ' OO') then
           message = message(1:19)
        else
           message = message(1:i-4)
        endif
     endif
  endif

  !--------------------------------------------------------------------
  ! Detect leading shorthand messages
  !--------------------------------------------------------------------
  if (message(1:3) == 'RO ')  nspecial = 2
  if (message(1:4) == 'RRR ') nspecial = 3
  if (message(1:3) == '73 ')  nspecial = 4

end subroutine chkmsg

end module chkmsg_mod
