program test_interleave63

  implicit none

  integer, parameter :: interleaved(0:62) = [ &
       0,7,14,21,28,35,42,49,56, &
       1,8,15,22,29,36,43,50,57, &
       2,9,16,23,30,37,44,51,58, &
       3,10,17,24,31,38,45,52,59, &
       4,11,18,25,32,39,46,53,60, &
       5,12,19,26,33,40,47,54,61, &
       6,13,20,27,34,41,48,55,62 ]
  integer :: original(0:62), data(0:62), wanted(0:62)
  integer :: i, ntests
  external :: interleave63, move

  ntests=0
  do i=0,62
     original(i)=i
  enddo
  data=original
  call interleave63(data,1)
  call expect_vector(data,interleaved,'known interleaved vector')

  data=interleaved
  call interleave63(data,-1)
  call expect_vector(data,original,'known de-interleaved vector')

  do i=0,62
     original(i)=(-1)**i*(huge(0)-i*1234567)
  enddo
  original(0)=huge(0)
  original(1)=-huge(0)
  original(2)=0
  original(3)=1
  original(4)=-1
  original(5)=16777217
  original(6)=-16777217

  call move(original,data,63)
  call expect_vector(data,original,'integer move')

  do i=0,62
     wanted(i)=original(mod(i,9)*7+i/9)
  enddo
  data=original
  call interleave63(data,1)
  call expect_vector(data,wanted,'forward integer mapping')

  data=original
  call interleave63(data,0)
  call expect_vector(data,wanted,'zero direction forward mapping')

  do i=0,62
     wanted(i)=original(mod(i,7)*9+i/7)
  enddo
  data=original
  call interleave63(data,-1)
  call expect_vector(data,wanted,'inverse integer mapping')

  data=original
  call interleave63(data,1)
  call interleave63(data,-1)
  call expect_vector(data,original,'forward inverse round trip')

  data=original
  call interleave63(data,-1)
  call interleave63(data,1)
  call expect_vector(data,original,'inverse forward round trip')

  write(*,1000) ntests
1000 format('interleave63 integer tests passed: ',i0)

contains

  subroutine expect_vector(got,want,label)
    integer, intent(in) :: got(0:62), want(0:62)
    character(len=*), intent(in) :: label
    integer :: k

    do k=0,62
       if(got(k).ne.want(k)) then
          write(*,1010) trim(label), k, got(k), want(k)
1010      format('Interleave63 failure: ',a,' at ',i0,'; got ',i0,' wanted ',i0)
          error stop 1
       endif
    enddo
    ntests=ntests+1
  end subroutine expect_vector

end program test_interleave63
