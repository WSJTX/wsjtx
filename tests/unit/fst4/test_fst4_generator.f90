program test_fst4_generator

  implicit none

  integer :: ntests

  ntests=0

  call expect_good_fst4w('K1ABC FN42 37',ntests)
  call expect_bad_fst4w('<PJ4/K1ABC> FK52A!',ntests)
  call expect_bad_fst4w('K1ABC FN42 55',ntests)
  call expect_bad_fst4w('K1ABC FN42 03',ntests)
  call expect_bad_fst4w('K1ABC FN42 5!',ntests)
  call expect_fst4_lossy_free_text('ABCDEFGHIJKLMN','ABCDEFGHIJKLM',ntests)

  write(*,1000) ntests
1000 format('FST4 generator tests passed: ',i0)

contains

  subroutine expect_good_fst4w(input,ntests)
    character(len=*), intent(in) :: input
    integer, intent(inout) :: ntests
    character(len=37) :: msg,msgsent
    integer*1 :: msgbits(101)
    integer*4 :: itone(160)
    integer :: iwspr

    msg='                                     '
    msg=input
    msgsent='sentinel'
    msgbits=0
    itone=0
    iwspr=1

    call genfst4(msg,0,msgsent,msgbits,itone,iwspr)

    if(trim(msgsent).ne.input) then
       write(*,1080) trim(input), trim(msgsent)
1080   format('FST4W valid input "',a,'" returned "',a,'"')
       error stop 1
    endif
    if(iwspr.ne.1) then
       write(*,1090) trim(input), iwspr
1090   format('FST4W valid input "',a,'" changed iwspr to ',i0)
       error stop 1
    endif
    if(all(itone.eq.0)) then
       write(*,1100) trim(input)
1100   format('FST4W valid input "',a,'" produced all-zero tones')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_good_fst4w

  subroutine expect_bad_fst4w(input,ntests)
    character(len=*), intent(in) :: input
    integer, intent(inout) :: ntests
    character(len=37) :: msg,msgsent
    integer*1 :: msgbits(101)
    integer*4 :: itone(160)
    integer :: iwspr

    msg='                                     '
    msg=input
    msgsent='sentinel'
    msgbits=1
    itone=3
    iwspr=1

    call genfst4(msg,0,msgsent,msgbits,itone,iwspr)

    if(trim(msgsent).ne.'*** bad message ***') then
       write(*,1010) trim(input), trim(msgsent)
1010   format('FST4W invalid input "',a,'" returned "',a,'"')
       error stop 1
    endif
    if(any(msgbits.ne.0)) then
       write(*,1020) trim(input)
1020   format('FST4W invalid input "',a,'" left nonzero message bits')
       error stop 1
    endif
    if(any(itone.ne.0)) then
       write(*,1030) trim(input)
1030   format('FST4W invalid input "',a,'" left nonzero tones')
       error stop 1
    endif
    if(iwspr.ne.1) then
       write(*,1040) trim(input), iwspr
1040   format('FST4W invalid input "',a,'" changed iwspr to ',i0)
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_bad_fst4w

  subroutine expect_fst4_lossy_free_text(input,expected,ntests)
    character(len=*), intent(in) :: input
    character(len=*), intent(in) :: expected
    integer, intent(inout) :: ntests
    character(len=37) :: msg,msgsent
    integer*1 :: msgbits(101)
    integer*4 :: itone(160)
    integer :: iwspr

    msg='                                     '
    msg=input
    msgsent='sentinel'
    msgbits=0
    itone=0
    iwspr=0

    call genfst4(msg,0,msgsent,msgbits,itone,iwspr)

    if(trim(msgsent).eq.'*** bad message ***') then
       write(*,1050) trim(input)
1050   format('FST4 lossy input "',a,'" was rejected')
       error stop 1
    endif
    if(trim(msgsent).ne.expected) then
       write(*,1055) trim(input), trim(msgsent), trim(expected)
1055   format('FST4 lossy input "',a,'" returned "',a,'" expected "',a,'"')
       error stop 1
    endif
    if(iwspr.ne.0) then
       write(*,1060) trim(input), iwspr
1060   format('FST4 lossy input "',a,'" changed iwspr to ',i0)
       error stop 1
    endif
    if(all(itone.eq.0)) then
       write(*,1070) trim(input)
1070   format('FST4 lossy input "',a,'" produced all-zero tones')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_fst4_lossy_free_text

end program test_fst4_generator
