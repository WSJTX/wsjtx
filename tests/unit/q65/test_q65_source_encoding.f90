program test_q65_source_encoding

  use q65_encoding, only: get_q65_tones
  implicit none

  external :: genq65

  integer :: ntests

  ntests=0

  call expect_shared_vector('K1ABC W9XYZ FN42', &
       [2,27,55,35,20,6,5,9,55,0,40,25,34,53,28,53,32,3,23,8, &
        41,41,7,11,8,59,50,60,47,21,21,9,45,59,19,4,2,3,10,40, &
        22,4,53,46,55,0,0,28,25,28,32,0,31,40,30,30,28,49,27,37, &
        23,32,29,40,9], &
       [0,3,28,56,36,21,7,6,0,10,56,0,0,1,0,41,26,35,54,33, &
        4,0,0,24,9,0,0,42,42,8,12,9,0,60,0,51,61,0,48,22, &
        22,10,46,60,20,0,5,3,4,0,11,41,23,5,0,54,47,56,1,0, &
        1,0,29,26,29,0,33,1,0,32,41,31,31,0,29,0,50,28,38,24, &
        33,30,41,10,0])

  call expect_shared_vector('K1ABC W9XYZ RR73', &
       [2,27,55,35,20,6,5,9,55,1,62,36,50,51,52,51,38,5,17,14, &
        50,51,39,19,16,35,42,36,4,62,46,52,16,6,56,47,41,40,7,53, &
        11,25,40,51,23,32,1,53,48,53,41,9,29,20,34,18,16,57,19,45, &
        13,58,55,2,35], &
       [0,3,28,56,36,21,7,6,0,10,56,0,0,2,0,63,37,51,52,39, &
        6,0,0,18,15,0,0,51,52,40,20,17,0,36,0,43,37,0,5,63, &
        47,53,17,7,57,0,48,42,41,0,8,54,12,26,0,41,52,24,33,0, &
        2,0,54,49,54,0,42,10,0,30,21,35,19,0,17,0,58,20,46,14, &
        59,56,3,36,0])

  call expect_legacy_vector('K1ABC W9XYZ FN42', &
       [0,3,28,56,36,21,7,6,0,10,56,0,0,1,0,41,26,35,54,33, &
        4,0,0,24,9,0,0,42,42,8,12,9,0,60,0,51,61,0,48,22, &
        22,10,46,60,20,0,5,3,4,0,11,41,23,5,0,54,47,56,1,0, &
        1,0,29,26,29,0,33,1,0,32,41,31,31,0,29,0,50,28,38,24, &
        33,30,41,10,0])

  call expect_legacy_vector('K1ABC W9XYZ RR73', &
       [0,3,28,56,36,21,7,6,0,10,56,0,0,2,0,63,37,51,52,39, &
        6,0,0,18,15,0,0,51,52,40,20,17,0,36,0,43,37,0,5,63, &
        47,53,17,7,57,0,48,42,41,0,8,54,12,26,0,41,52,24,33,0, &
        2,0,54,49,54,0,42,10,0,30,21,35,19,0,17,0,58,20,46,14, &
        59,56,3,36,0])

  call expect_invalid_shared_message()
  call expect_invalid_legacy_message()

  write(*,'(a,i0)') 'Q65 source encoding tests passed: ', ntests

contains

  subroutine expect_shared_vector(input,want_codeword,want_tones)
    character(len=*), intent(in) :: input
    integer, intent(in) :: want_codeword(65), want_tones(85)
    character(len=37) :: msg37,msgsent,want_msg
    integer :: codeword(65), itone(85)
    integer :: sync(22),i,k
    logical :: success

    msg37=' '
    msg37=input
    want_msg=' '
    want_msg=input
    call get_q65_tones(msg37,codeword,itone,msgsent,success)
    if(.not.success) error stop 'shared Q65 encoder rejected a vector'
    call assert_message('shared Q65 encoder',msgsent,want_msg)
    call assert_vector('shared Q65 codeword',codeword,want_codeword)
    call assert_vector('shared Q65 tones',itone,want_tones)

    sync=[1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85]
    k=0
    do i=1,85
       if(any(sync.eq.i)) then
          if(itone(i).ne.0) error stop 'Q65 sync tone is not zero'
       else
          k=k+1
          if(itone(i).ne.want_codeword(merge(k,k+2,k.le.13))+1) then
             error stop 'Q65 tone does not match shortened codeword'
          endif
       endif
    enddo
    if(k.ne.63) error stop 'Q65 shortened codeword length changed'

    ntests=ntests+1
  end subroutine expect_shared_vector

  subroutine expect_legacy_vector(input,want_tones)
    character(len=*), intent(in) :: input
    integer, intent(in) :: want_tones(85)
    character(len=37) :: msg37,msgsent,want_msg
    integer :: itone(85),i3,n3

    msg37=' '
    msg37=input
    want_msg=' '
    want_msg=input
    itone=-1
    i3=-1
    n3=-1
    call genq65(msg37,0,msgsent,itone,i3,n3)
    if(i3.lt.0 .or. n3.lt.0) error stop 'legacy Q65 encoder rejected a vector'
    call assert_message('legacy Q65 encoder',msgsent,want_msg)
    call assert_vector('legacy Q65 tones',itone,want_tones)
    ntests=ntests+1
  end subroutine expect_legacy_vector

  subroutine expect_invalid_shared_message()
    character(len=37) :: msg37,msgsent
    integer :: codeword(65),itone(85)
    logical :: success

    msg37='HELLO@WORLD'
    call get_q65_tones(msg37,codeword,itone,msgsent,success)
    if(success) error stop 'shared Q65 encoder accepted invalid input'
    call assert_message('shared Q65 invalid message',msgsent, &
         '*** bad message ***                  ')
    if(any(codeword.ne.0) .or. any(itone.ne.0)) then
       error stop 'shared Q65 invalid output was not cleared'
    endif
    ntests=ntests+1
  end subroutine expect_invalid_shared_message

  subroutine expect_invalid_legacy_message()
    character(len=37) :: msg37,msgsent
    integer :: itone(85),i3,n3

    msg37='HELLO@WORLD'
    call genq65(msg37,0,msgsent,itone,i3,n3)
    if(i3.ne.-1 .or. n3.ne.-1) error stop 'legacy Q65 encoder accepted invalid input'
    call assert_message('legacy Q65 invalid message',msgsent, &
         '*** bad message ***                  ')
    if(any(itone.ne.0)) error stop 'legacy Q65 invalid tones were not cleared'
    ntests=ntests+1
  end subroutine expect_invalid_legacy_message

  subroutine assert_message(label,got,want)
    character(len=*), intent(in) :: label,got,want
    if(got.ne.want) then
       write(*,'(a,": got [",a,"] wanted [",a,"]")') trim(label),got,want
       error stop 1
    endif
  end subroutine assert_message

  subroutine assert_vector(label,got,want)
    character(len=*), intent(in) :: label
    integer, intent(in) :: got(:),want(:)
    integer :: i

    if(size(got).ne.size(want)) error stop 'vector size changed'
    do i=1,size(got)
       if(got(i).ne.want(i)) then
          write(*,'(a,": index ",i0,": got ",i0," wanted ",i0)') &
               trim(label),i,got(i),want(i)
          error stop 1
       endif
    enddo
  end subroutine assert_vector

end program test_q65_source_encoding
