program test_q65_set_list_suffix
! The full-AP candidate list must contain the exact codewords of the messages
! a station sends.  A standard callsign with a /R or /P suffix is still a
! standard callsign (Type 1 message, ipa/ipb flag), so the list must hold the
! suffixed Type 1 messages, not nonstandard <hashed> forms.

  implicit none
  external :: genq65,q65_set_list,q65_stdcall
  integer, parameter :: MAX_NCW=206
  integer :: codewords(63,MAX_NCW)
  integer :: ncw,nfail
  logical :: std

  nfail=0
  call check_stdcall('DL3WDG',.true.)
  call check_stdcall('DL3WDG/R',.true.)
  call check_stdcall('DL3WDG/P',.true.)
  call check_stdcall('W3SZ/R',.true.)
  call check_stdcall('K1ABC/P',.true.)
  call check_stdcall('PJ4/K1ABC',.false.)
  call check_stdcall('K1ABC/QRP',.false.)
  call check_stdcall('/R',.false.)

  call check_list('DL3WDG')
  call check_list('DL3WDG/R')
  call check_list('DL3WDG/P')
  call check_list('K1ABC/R')

  if(nfail.ne.0) then
     print *, 'Q65 set_list suffix tests FAILED:',nfail
     error stop 1
  endif
  print *, 'Q65 set_list suffix tests passed'

contains

  subroutine check_stdcall(call0,expected)
    character(len=*), intent(in) :: call0
    logical, intent(in) :: expected
    character(len=12) :: c12
    c12=call0
    call q65_stdcall(c12,std)
    if(std.neqv.expected) then
       print *, 'stdcall(',trim(call0),') =',std,' expected ',expected
       nfail=nfail+1
    endif
  end subroutine check_stdcall

  subroutine check_list(dxcall)
    character(len=*), intent(in) :: dxcall
    character(len=12) :: mycall,hiscall
    character(len=6) :: hisgrid
    character(len=37) :: msg(8)
    integer :: k
    mycall='K1JT'
    hiscall=dxcall
    hisgrid='JN68'
    call q65_set_list(mycall,hiscall,hisgrid,codewords,ncw)
    msg(1)='K1JT '//trim(dxcall)
    msg(2)='K1JT '//trim(dxcall)//' RRR'
    msg(3)='K1JT '//trim(dxcall)//' RR73'
    msg(4)='K1JT '//trim(dxcall)//' 73'
    msg(5)='CQ '//trim(dxcall)//' JN68'
    msg(6)='K1JT '//trim(dxcall)//' JN68'
    msg(7)='K1JT '//trim(dxcall)//' -10'
    msg(8)='K1JT '//trim(dxcall)//' R-10'
    do k=1,8
       call expect_in_list(msg(k))
    enddo
  end subroutine check_list

  subroutine expect_in_list(msg)
    character(len=37), intent(in) :: msg
    character(len=37) :: msgsent
    integer :: itone(85),ref(63),isync(22),i0,j,k,i,i3,n3
    data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/
    call genq65(msg,0,msgsent,itone,i3,n3,0)
    i0=1
    j=0
    do k=1,85
       if(k.eq.isync(i0)) then
          i0=min(i0+1,22)
          cycle
       endif
       j=j+1
       ref(j)=itone(k)-1
    enddo
    do i=1,ncw
       if(all(codewords(:,i).eq.ref)) return
    enddo
    print *, 'not in list: ',trim(msg)
    nfail=nfail+1
  end subroutine expect_in_list

end program test_q65_set_list_suffix
