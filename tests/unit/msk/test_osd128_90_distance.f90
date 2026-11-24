program test_osd128_90_distance
  implicit none
  integer, parameter :: k=90, n=128
  ! Parity bits of the LDPC word with only message bit 1 set.
  integer, parameter :: parity_ones(*)=[91,93,94,95,96,97,101,103,105,106,112,113,120,121,122,124,125]
  integer, parameter :: weights(*)=[512,4096]
  integer*1 :: apmask(n), cw(n), message(77), expected(n)
  real :: llr(n), dmin, actual
  logical :: mismatch(n)
  integer :: i, profile, nhard, failures

  apmask=0
  expected=0
  expected(1)=1
  expected(parity_ones)=1
  failures=0
  do profile=1,size(weights)
     ! The first 110 columns are zero in row 1: parity bit 92, the other
     ! 89 identity columns, then the remaining 20 zero parity columns.
     llr=-256
     llr(parity_ones)=128
     llr(1)=-128
     do i=2,k
        llr(i)=-real(512-i)
     enddo
     llr(92)=-real(weights(profile))

     call osd128_90(llr,apmask,1,message,cw,nhard,dmin)
     mismatch=(cw==1) .neqv. (llr>=0)
     actual=sum(abs(llr),mask=mismatch)
     if (all(cw==expected) .and. dmin==actual .and. abs(nhard)==count(mismatch)) cycle
     print *, 'weight, reported distance, actual distance, matches expected word, hard errors:', &
          weights(profile),dmin,actual,all(cw==expected),nhard
     failures=failures+1
  enddo
  if (failures/=0) error stop 'incorrect (128,90) OSD candidate distance'
end program test_osd128_90_distance
