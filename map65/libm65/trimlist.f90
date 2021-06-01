subroutine trimlist(sig,km,ftol,indx,nsiz,nz)

  parameter (MAXMSG=1000)             !Size of decoded message list
  real sig(MAXMSG,30)
  integer indx(MAXMSG),nsiz(MAXMSG)

!    1      2     3    4    5    6     7     8
!  nfile  nutc  freq  snr  dt  ipol  flip  sync

  call indexx(sig(1,3),km,indx)            !Sort list by frequency

  n=1
  i0=1
  do i=2,km         
     j0=indx(i-1)
     j=indx(i)
     if(sig(j,3)-sig(j0,3).gt.ftol) then
        nsiz(n)=i-i0
        i0=i
        n=n+1
     endif
  enddo
  nz=n
  nsiz(nz)=km+1-i0
  nsiz(nz+1)=-1

  return
end subroutine trimlist
