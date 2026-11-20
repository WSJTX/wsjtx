program test_osd174_91_distance
  use osd174_91var_mod, only: osd174_91var,osd_pair_workspace
  implicit none
  integer, parameter :: n=174, k=91
  integer*1 :: unit(k), row(n), cw(n), message(k), apmask(n)
  integer, parameter :: weights(2)=[2,128]
  integer :: i, profile, weight, depth, nhard, failures
  real :: llr(n), dmin
  type(osd_pair_workspace), allocatable :: ws

  ! Keep the first 90 identity columns in order. Rank all remaining
  ! columns with a zero in row 91 ahead of every possible last pivot.
  unit=0
  unit(k)=1
  call encode174_91_nocrc(unit,row)
  if (count(row(k:n)==0)<21) error stop 'fixture does not exhaust the pivot window'

  apmask=0
  failures=0
  depth=2
  allocate(ws)
  ! At weight 2, charging for the input flip misreports the distance.
  ! At weight 128, the same charge can keep a better candidate from winning.
  do profile=1,size(weights)
     weight=weights(profile)
     llr=merge(1.0,-real(weight),row==1)
     do i=1,k-1
        llr(i)=-real(512-i)
     enddo
     llr(k)=-1.0

     ! Flipping message bit 91 reaches row. In the second profile,
     ! also reverse hard decision 90 so reaching row takes a pair of flips.
     if (profile==2) llr(k-1)=real(weight+1)

     call osd174_91var(llr,apmask,depth,message(1:77),cw,nhard,dmin,1,0,ws)
     call check_distance('var')
     call osd174_91(llr,k,apmask,depth,message,cw,nhard,dmin)
     call check_distance('packed')
     call osd174_91_slow(llr,k,apmask,depth,message,cw,nhard,dmin)
     call check_distance('slow')
  enddo
  if (failures/=0) error stop 'incorrect OSD candidate distance'

contains

  subroutine check_distance(decoder)
    character(*), intent(in) :: decoder
    logical :: mismatch(n)
    real :: actual, challenger

    mismatch=(cw==1) .neqv. (llr>=0)
    actual=sum(abs(llr),mask=mismatch)
    challenger=sum(abs(llr),mask=(row==1) .neqv. (llr>=0))
    ! All weights and sums are exactly representable integers.
    if (dmin==actual .and. actual<=challenger .and. abs(nhard)==count(mismatch)) return
    print *,decoder, ' weight, reported, actual, challenger, hard errors:',weight,dmin,actual,challenger,nhard
    failures=failures+1
  end subroutine check_distance
end program test_osd174_91_distance
