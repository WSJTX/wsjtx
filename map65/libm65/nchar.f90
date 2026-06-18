module nchar_mod
  implicit none
contains
  integer function nchar(c)
    implicit none
    character, intent(in) :: c
    integer :: n

    n=0                                    !Silence compiler warning
    if(c.ge.'0' .and. c.le.'9') then
       n=ichar(c)-ichar('0')
    else if(c.ge.'A' .and. c.le.'Z') then
       n=ichar(c)-ichar('A') + 10
    else if(c.ge.'a' .and. c.le.'z') then
       n=ichar(c)-ichar('a') + 10
    else if(c.ge.' ') then
       n=36
    else
      ! Print*,'Invalid character in callsign ',c,' ',ichar(c)
       stop 1
    endif
    nchar=n
  end function nchar
end module nchar_mod
