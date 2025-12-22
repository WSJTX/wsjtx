subroutine packprop(k,muf,ccur,cxp,n1)

! Pack propagation indicators into a 21-bit number.

! k      k-index, 0-9; 10="N/A"
! muf    muf, 2-60 MHz; 0=N/A, 1="none", 61=">60 MHz"
! ccur   up to two current events, each indicated by single
!        or double letter.
! cxp    zero or one expected event, indicated by single or
!        double letter

  character ccur*4,cxp*2

  j=ichar(ccur(1:1))-64
  if(j.lt.0) j=0
  n1=j
  do i=2,4
     if(ccur(i:i).eq.' ') go to 10
     if(ccur(i:i).eq.ccur(i-1:i-1)) then
        n1=n1+26
     else
        j=ichar(ccur(i:i))-64
        if(j.lt.0) j=0
        n1=53*n1 + j
     endif
  enddo

10 j=ichar(cxp(1:1))-64
  if(j.lt.0) j=0
  if(cxp(2:2).eq.cxp(1:1)) j=j+26
  n1=53*n1 + j
  n1=11*n1 + k
  n1=62*n1 + muf

  return
end subroutine packprop
