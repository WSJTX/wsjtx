subroutine unpacktext2(n1,ng,msg)

  character*22 msg
  real*8 dn
  character*41 c
  data c/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +./?'/

  msg='                      '
  dn=32768.d0*n1 + ng
  do i=8,1,-1
     j=mod(dn,41.d0)
     msg(i:i)=c(j+1:j+1)
     dn=dn/41.d0
  enddo

  return
end subroutine unpacktext2
