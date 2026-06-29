module packjt77_test_helpers

  use packjt77
  implicit none

contains

  subroutine clear_standard_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    calls10=''
    calls12=''
    calls22=''
    recent_calls=''
    ihash22=-1
    nzhash=0
    mycall13='             '
    dxcall13='             '
    mycall13=mycall
    dxcall13=dxcall
  end subroutine clear_standard_state

  subroutine clear_var_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    calls10var=''
    calls12var=''
    calls22var=''
    txcalls10var=''
    txcalls12var=''
    txcalls22var=''
    recent_callsvar=''
    last_callsvar=''
    ihash22var=-1
    itxhash22var=-1
    nlast_callsvar=0
    nzhashvar=0
    nztxhashvar=0
    mycall13var='             '
    dxcall13var='             '
    mycall13_0var='             '
    dxcall13_0var='             '
    mycall13_setvar=.false.
    dxcall13_setvar=.false.
    hashmy10var=-1
    hashmy12var=-1
    hashmy22var=-1
    hashdx10var=-1
    lcommonft8b=.false.

    mycall13var=mycall
    dxcall13var=dxcall
    if(len_trim(mycall).gt.2) then
       mycall13_setvar=.true.
       mycall13_0var=mycall13var
       hashmy10var=ihashcall(mycall13var,10)
       hashmy12var=ihashcall(mycall13var,12)
       hashmy22var=ihashcall(mycall13var,22)
    endif
    if(len_trim(dxcall).gt.2) then
       dxcall13_setvar=.true.
       dxcall13_0var=dxcall13var
       hashdx10var=ihashcall(dxcall13var,10)
    endif
  end subroutine clear_var_state

  subroutine clear_all_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    call clear_standard_state(mycall,dxcall)
    call clear_var_state(mycall,dxcall)
  end subroutine clear_all_state

  subroutine normalize_call(callsign,c13)
    character(len=*), intent(in) :: callsign
    character(len=13), intent(out) :: c13
    integer :: i

    c13='             '
    c13=callsign
    if(c13(1:1).eq.'<') c13=c13(2:)
    i=index(c13,'>')
    if(i.gt.0) c13(i:)='             '
  end subroutine normalize_call

  subroutine assert_message_type(label,input,want_i3,want_n3,got_i3,got_n3)
    character(len=*), intent(in) :: label, input
    integer, intent(in) :: want_i3, want_n3, got_i3, got_n3

    if(got_i3.ne.want_i3 .or. got_n3.ne.want_n3) then
       write(*,1000) trim(label), trim(input), want_i3, want_n3, got_i3, got_n3
1000   format(a,' message type failure for "',a,'"; wanted ',i0,'.',i0, &
              ' got ',i0,'.',i0)
       error stop 1
    endif
  end subroutine assert_message_type

  subroutine assert_binary_payload(label,input,c77)
    character(len=*), intent(in) :: label, input
    character(len=77), intent(in) :: c77

    if(.not.is_binary_payload(c77)) then
       write(*,1010) trim(label), trim(input), c77
1010   format(a,' emitted non-binary payload for "',a,'": ',a)
       error stop 1
    endif
  end subroutine assert_binary_payload

  logical function is_binary_payload(c77)
    character(len=77), intent(in) :: c77
    integer :: i

    is_binary_payload=.true.
    do i=1,77
       if(c77(i:i).ne.'0' .and. c77(i:i).ne.'1') then
          is_binary_payload=.false.
          return
       endif
    enddo
  end function is_binary_payload

  subroutine assert_decode(label,input,expected,decoded,ok)
    character(len=*), intent(in) :: label, input, expected, decoded
    logical, intent(in) :: ok

    if(.not.ok) then
       write(*,1020) trim(label), trim(input)
1020   format(a,' unpack failure for "',a,'"')
       error stop 1
    endif
    if(trim(decoded).ne.expected) then
       write(*,1030) trim(label), trim(input), trim(expected), trim(decoded)
1030   format(a,' decode failure for "',a,'"; expected "',a,'"; got "',a,'"')
       error stop 1
    endif
  end subroutine assert_decode

  subroutine assert_text_equal(label,left,right)
    character(len=*), intent(in) :: label, left, right

    if(trim(left).ne.trim(right)) then
       write(*,1040) trim(label), trim(left), trim(right)
1040   format(a,' mismatch; left "',a,'" right "',a,'"')
       error stop 1
    endif
  end subroutine assert_text_equal

  subroutine assert_call(label,got,expected)
    character(len=*), intent(in) :: label, got, expected

    if(trim(got).ne.expected) then
       write(*,1050) trim(label), trim(expected), trim(got)
1050   format(a,' failure; expected "',a,'"; got "',a,'"')
       error stop 1
    endif
  end subroutine assert_call

  subroutine assert_int(label,expected,got)
    character(len=*), intent(in) :: label
    integer, intent(in) :: expected, got

    if(got.ne.expected) then
       write(*,1060) trim(label), expected, got
1060   format(a,' failure; expected ',i0,' got ',i0)
       error stop 1
    endif
  end subroutine assert_int

  subroutine assert_true(label,condition)
    character(len=*), intent(in) :: label
    logical, intent(in) :: condition

    if(.not.condition) then
       write(*,1070) trim(label)
1070   format(a,' failure')
       error stop 1
    endif
  end subroutine assert_true

end module packjt77_test_helpers
