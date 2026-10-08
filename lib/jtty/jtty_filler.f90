! SPDX-License-Identifier: GPL-3.0-or-later
! A port of the GUI's rule for live-entry filler in received JTTY text
! (Jtty::stripJttyFillerText, widgets/JttyMessages.hpp): each run of one or
! more '<<<<<' blocks, with the blanks before, between and after them,
! becomes one space; then the text is trimmed and every inner run of
! whitespace becomes one space. As in Qt, a blank in the first step is HT,
! LF, VT, FF, CR or space, and whitespace in the second also includes
! Latin-1 NEL and NBSP. test_jtty_filler holds the port to the GUI's output.
module jtty_filler
  use iso_c_binding, only: c_char, c_int
  implicit none
  private

  public :: strip_filler_text, jtty_strip_filler_text

contains

  ! stripped(1:length) is text with the filler rule applied; it is never
  ! longer than text.
  pure subroutine strip_filler_text(text,stripped,length)
    character(len=*), intent(in) :: text
    character(len=*), intent(out) :: stripped
    integer, intent(out) :: length
    character(len=len(text)) :: collapsed
    integer :: i,k,n
    logical :: separate

    n=0
    i=1
    do while(i.le.len(text))
       k=i
       do while(k.le.len(text))
          if(.not.is_blank(text(k:k))) exit
          k=k+1
       enddo
       if(is_filler(text,k)) then
          do while(is_filler(text,k))
             k=k+5
             do while(k.le.len(text))
                if(.not.is_blank(text(k:k))) exit
                k=k+1
             enddo
          enddo
          n=n+1
          collapsed(n:n)=' '
       else
          k=max(k,i+1)
          collapsed(n+1:n+k-i)=text(i:k-1)
          n=n+k-i
       endif
       i=k
    enddo

    stripped=' '
    length=0
    separate=.false.
    do i=1,n
       if(is_whitespace(collapsed(i:i))) then
          separate=length.gt.0
       else
          if(separate) then
             length=length+1
             stripped(length:length)=' '
             separate=.false.
          endif
          length=length+1
          stripped(length:length)=collapsed(i:i)
       endif
    enddo
  end subroutine strip_filler_text

  ! strip_filler_text for C callers: stripped holds at least length
  ! characters; returns the stripped length.
  integer(c_int) function jtty_strip_filler_text(text,length,stripped) &
       bind(C,name='jtty_strip_filler_text')
    integer(c_int), value :: length
    character(kind=c_char), intent(in) :: text(*)
    character(kind=c_char), intent(out) :: stripped(*)
    character(len=max(length,0)) :: input,output
    integer :: i,n

    do i=1,len(input)
       input(i:i)=text(i)
    enddo
    call strip_filler_text(input,output,n)
    do i=1,n
       stripped(i)=output(i:i)
    enddo
    jtty_strip_filler_text=n
  end function jtty_strip_filler_text

  pure logical function is_filler(text,k)
    character(len=*), intent(in) :: text
    integer, intent(in) :: k

    is_filler=.false.
    if(k+4.gt.len(text)) return
    is_filler=text(k:k+4).eq.'<<<<<'
  end function is_filler

  pure logical function is_blank(c)
    character, intent(in) :: c

    select case(iachar(c))
    case(9:13,32)
       is_blank=.true.
    case default
       is_blank=.false.
    end select
  end function is_blank

  pure logical function is_whitespace(c)
    character, intent(in) :: c

    select case(iachar(c))
    case(9:13,32,133,160)
       is_whitespace=.true.
    case default
       is_whitespace=.false.
    end select
  end function is_whitespace

end module jtty_filler
