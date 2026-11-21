program test_fst4_osd_distance
  implicit none
  external :: encode240_101, encode240_74, osd240_101, osd240_74
  integer :: failures

  failures=0
  call check_last_pivot(101,91,encode240_101,osd240_101)
  call check_last_pivot(74,66,encode240_74,osd240_74)
  call check_early_pivot()
  if (failures /= 0) error stop 'incorrect OSD candidate distance'

contains

  subroutine check_last_pivot(message_bits,k,encode,decode)
    integer, intent(in) :: message_bits,k
    external :: encode,decode
    integer, parameter :: n=240, weights(2)=[2,128]
    integer(kind=1) :: message(message_bits),row(n)
    integer :: i,profile
    real :: llr(n)

    message=0
    message(k)=1
    call encode(message,row)
    if (count(row(k:n)==0)<21) error stop 'fixture does not exhaust the pivot window'

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
       call check_candidate(message_bits,k,decode,llr,row,k,profile)
    enddo
  end subroutine check_last_pivot

  subroutine check_early_pivot()
    integer, parameter :: n=240, k=91, missed_row=78
    integer(kind=1) :: message(101),row(n)
    integer :: i,zero_column
    real :: llr(n)

    message=0
    message(missed_row)=1
    call encode240_101(message,row)
    if (count(row(k+1:n)==0)<21) error stop 'fixture does not exhaust the pivot window'

    ! Put a zero column at the first CRC row's pivot position. The
    ! identity columns that follow still give later rows their pivots.
    zero_column=k+minloc(row(k+1:n),dim=1)
    llr=merge(1.0,-128.0,row==1)
    do i=1,k
       llr(i)=-real(512-i)
    enddo
    llr(zero_column)=llr(missed_row)
    llr(missed_row)=-1.0

    call check_candidate(size(message),k,osd240_101,llr,row,missed_row,1)
  end subroutine check_early_pivot

  subroutine check_candidate(message_bits,k,decode,llr,expected,missed_row,profile)
    integer, intent(in) :: message_bits,k,missed_row,profile
    integer, parameter :: n=240
    real, intent(in) :: llr(n)
    integer(kind=1), intent(in) :: expected(n)
    external :: decode
    integer(kind=1) :: message(message_bits),cw(n),apmask(n)
    integer :: depth,nhard
    real :: dmin,actual,expected_distance
    logical :: mismatch(n)

    depth=3
    apmask=0
    call decode(llr,k,apmask,depth,message,cw,nhard,dmin)
    mismatch=(cw==1) .neqv. (llr>=0)
    actual=sum(abs(llr),mask=mismatch)
    if (all(cw==expected) .and. dmin==actual .and. abs(nhard)==count(mismatch)) return
    expected_distance=sum(abs(llr),mask=(expected==1) .neqv. (llr>=0))
    print *, 'message bits, missed row, profile, reported, actual, expected, hard errors:', &
         message_bits,missed_row,profile,dmin,actual,expected_distance,nhard
    failures=failures+1
  end subroutine check_candidate
end program test_fst4_osd_distance
