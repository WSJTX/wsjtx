subroutine packtext2(msg,n1,ng)

  character*8 msg
  real*8 dn
  character*41 c
  data c/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +./?'/

  dn=0.
  do i=1,8
     do j=1,41
        if(msg(i:i).eq.c(j:j)) go to 10
     enddo
     j=37
10   j=j-1                                !Codes should start at zero
     dn=41.d0*dn + j
  enddo

  ng=mod(dn,32768.d0)
  n1=(dn-ng)/32768.d0

  return
end subroutine packtext2
