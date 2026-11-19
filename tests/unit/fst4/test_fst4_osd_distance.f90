program test_fst4_osd_distance
  implicit none
  external :: encode240_101, osd240_101
  integer :: failures

  failures=0
  call check_distance(101,91,encode240_101,osd240_101)
  if (failures /= 0) error stop 'incorrect OSD candidate distance'

contains

  subroutine check_distance(message_bits,k,encode,decode)
    integer, intent(in) :: message_bits,k
    external :: encode,decode
    integer, parameter :: n=240, weights(2)=[2,128]
    integer(kind=1) :: message(message_bits),row(n),cw(n),apmask(n)
    integer :: i,profile,nhard
    real :: llr(n),dmin,actual,challenger
    logical :: mismatch(n)

    message=0
    message(k)=1
    call encode(message,row)
    if (count(row(k:n)==0)<21) error stop 'fixture does not exhaust the pivot window'

    apmask=0
    do profile=1,size(weights)
       ! Keep the first k-1 identity columns in order, then rank the
       ! zero columns of row k ahead of every possible last pivot.
       llr=merge(1.0,-real(weights(profile)),row==1)
       do i=1,k-1
          llr(i)=-real(512-i)
       enddo
       llr(k)=-1.0

       ! A single input flip reaches row. Reversing hard decision k-1
       ! makes it a two-flip candidate that an incorrect score can reject.
       if (profile==2) llr(k-1)=real(weights(profile)+1)
       call decode(llr,k,apmask,2,message,cw,nhard,dmin)

       mismatch=(cw==1) .neqv. (llr>=0)
       actual=sum(abs(llr),mask=mismatch)
       challenger=sum(abs(llr),mask=(row==1) .neqv. (llr>=0))
       ! All weights and sums are exactly representable integers.
       if (dmin==actual .and. actual<=challenger .and. abs(nhard)==count(mismatch)) cycle
       print *, 'message bits, profile, reported, actual, challenger, hard errors:', &
            message_bits,profile,dmin,actual,challenger,nhard
       failures=failures+1
    enddo
  end subroutine check_distance
end program test_fst4_osd_distance
