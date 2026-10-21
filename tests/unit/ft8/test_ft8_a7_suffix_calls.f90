program test_ft8_a7_suffix_calls
! The FT8 a7/a8 candidate list must hold the Type 1 messages a station
! actually sends.  A standard call with a /R or /P suffix is still standard,
! so getmsg must not turn it into a <hashed> nonstandard form.

  use ft8_a7, only: getmsg
  implicit none
  integer :: nfail

  nfail=0
  call check_calls('DL3WDG')
  call check_calls('DL3WDG/R')
  call check_calls('DL3WDG/P')
  call check_calls('K1ABC/R')
  call check_calls('W3SZ/R')

  if(nfail.ne.0) then
     print *, 'FT8 a7 suffix call tests FAILED:',nfail
     error stop 1
  endif
  print *, 'FT8 a7 suffix call tests passed'

contains

  subroutine check_calls(dx)
    character(len=*), intent(in) :: dx
    character(len=12) :: mycall,hiscall
    character(len=6) :: hisgrid
    character(len=37) :: msg
    integer :: n
    logical :: sfx
    mycall='K1JT'
    hiscall=dx
    hisgrid='JN68'
    call getmsg(1,mycall,hiscall,hisgrid,msg)
    call expect(msg,'K1JT '//trim(dx))
    call getmsg(2,mycall,hiscall,hisgrid,msg)
    call expect(msg,'K1JT '//trim(dx)//' RRR')
    call getmsg(3,mycall,hiscall,hisgrid,msg)
    call expect(msg,'K1JT '//trim(dx)//' RR73')
    call getmsg(4,mycall,hiscall,hisgrid,msg)
    call expect(msg,'K1JT '//trim(dx)//' 73')
    call getmsg(5,mycall,hiscall,hisgrid,msg)
    call expect(msg,'CQ '//trim(dx)//' JN68')
    call getmsg(6,mycall,hiscall,hisgrid,msg)
    call expect(msg,'K1JT '//trim(dx)//' JN68')
    call getmsg(47,mycall,hiscall,hisgrid,msg)
    call expect(msg,'K1JT '//trim(dx)//' -30')
! Candidates 207-211: old-path forms for suffixed calls only
    n=len_trim(dx)
    sfx=dx(n-1:n).eq.'/R' .or. dx(n-1:n).eq.'/P'
    call getmsg(207,mycall,hiscall,hisgrid,msg)
    if(sfx) then
       call expect(msg,'CQ '//trim(dx))
    else
       call expect(msg,'')
    endif
    call getmsg(208,mycall,hiscall,hisgrid,msg)
    if(sfx) then
       call expect(msg,'<K1JT> '//trim(dx))
    else
       call expect(msg,'')
    endif
    call getmsg(209,mycall,hiscall,hisgrid,msg)
    if(sfx) then
       call expect(msg,'<K1JT> '//trim(dx)//' RRR')
    else
       call expect(msg,'')
    endif
    call getmsg(210,mycall,hiscall,hisgrid,msg)
    if(sfx) then
       call expect(msg,'<K1JT> '//trim(dx)//' RR73')
    else
       call expect(msg,'')
    endif
    call getmsg(211,mycall,hiscall,hisgrid,msg)
    if(sfx) then
       call expect(msg,'<K1JT> '//trim(dx)//' 73')
    else
       call expect(msg,'')
    endif
! Candidates 212-411: MyCall <HisCall> report / R-report, same sequence as 7-206
    call getmsg(212,mycall,hiscall,hisgrid,msg)
    call expect(msg,'')                              !-50 is beyond the +/-30 limit
    call getmsg(252,mycall,hiscall,hisgrid,msg)
    if(sfx) then
       call expect(msg,'K1JT <'//trim(dx)//'> -30')
    else
       call expect(msg,'')
    endif
    call getmsg(253,mycall,hiscall,hisgrid,msg)
    if(sfx) then
       call expect(msg,'K1JT <'//trim(dx)//'> R-30')
    else
       call expect(msg,'')
    endif
    call getmsg(282,mycall,hiscall,hisgrid,msg)
    if(sfx) then
       call expect(msg,'K1JT <'//trim(dx)//'> -15')
    else
       call expect(msg,'')
    endif
    call getmsg(283,mycall,hiscall,hisgrid,msg)
    if(sfx) then
       call expect(msg,'K1JT <'//trim(dx)//'> R-15')
    else
       call expect(msg,'')
    endif
  end subroutine check_calls

  subroutine expect(got,want)
    character(len=*), intent(in) :: got,want
    if(trim(got).ne.trim(want)) then
       print *, 'got "',trim(got),'" expected "',trim(want),'"'
       nfail=nfail+1
    endif
  end subroutine expect

end program test_ft8_a7_suffix_calls
