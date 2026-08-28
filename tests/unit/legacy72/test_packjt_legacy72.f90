program test_packjt_legacy72

  use packjt, only: packmsg, unpackmsg
  implicit none

  integer :: ntests

  ntests=0
  call expect_control_suffix_falls_back(ntests)
  call expect_type2_suffix_round_trip(ntests)
  call expect_rr73_is_encoded_as_grid(ntests)

  write(*,1000) ntests
1000 format('Legacy 72-bit codec tests passed: ',i0)

contains

  subroutine expect_control_suffix_falls_back(ntests)
    integer, intent(inout) :: ntests
    character(len=22) :: decoded,message
    integer :: itype,symbols(12)

    message=' '
    message(1:1)='/'
    message(2:2)=achar(1)

    call packmsg(message,symbols,itype)
    call unpackmsg(symbols,decoded)

    if(itype.ne.6) then
       write(*,1010) itype
1010   format('Control suffix returned type ',i0,' instead of free text')
       error stop 1
    endif
    if(any(symbols.lt.0) .or. any(symbols.gt.63)) then
       write(*,1020)
1020   format('Control suffix produced an out-of-range symbol')
       error stop 1
    endif
    if(trim(decoded).ne.'/') then
       write(*,1030) trim(decoded)
1030   format('Control suffix decoded as "',a,'" instead of "/"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_control_suffix_falls_back

  subroutine expect_type2_suffix_round_trip(ntests)
    integer, intent(inout) :: ntests
    character(len=*), parameter :: expected='CQ KA1ABC/VE6 -22'
    character(len=22) :: decoded,message
    integer :: itype,symbols(12)

    message=expected
    call packmsg(message,symbols,itype)
    call unpackmsg(symbols,decoded)

    if(itype.ne.5) then
       write(*,1040) itype
1040   format('Type 2 suffix returned type ',i0,' instead of 5')
       error stop 1
    endif
    if(any(symbols.lt.0) .or. any(symbols.gt.63)) then
       write(*,1050)
1050   format('Type 2 suffix produced an out-of-range symbol')
       error stop 1
    endif
    if(trim(decoded).ne.expected) then
       write(*,1060) trim(decoded),expected
1060   format('Type 2 suffix decoded as "',a,'" instead of "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_type2_suffix_round_trip

  subroutine expect_rr73_is_encoded_as_grid(ntests)
    integer, intent(inout) :: ntests
    character(len=*), parameter :: expected='K1ABC W9XYZ RR73'
    character(len=22) :: decoded,message
    integer :: itype,symbols(12)

    message=expected
    call packmsg(message,symbols,itype)
    call unpackmsg(symbols,decoded)

    if(itype.ne.1) then
       write(*,1070) itype
1070   format('RR73 returned type ',i0,' instead of a standard message')
       error stop 1
    endif
    if(any(symbols.lt.0) .or. any(symbols.gt.63)) then
       write(*,1080)
1080   format('RR73 produced an out-of-range symbol')
       error stop 1
    endif
    if(trim(decoded).ne.expected) then
       write(*,1090) trim(decoded),expected
1090   format('RR73 decoded as "',a,'" instead of "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_rr73_is_encoded_as_grid

end program test_packjt_legacy72
