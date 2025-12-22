subroutine packname(name,len,n1,n2)

  character*9 name
  real*8 dn

  dn=0
  do i=1,len
     n=ichar(name(i:i))
     if(n.ge.97 .and. n.le.122) n=n-32
     dn=27*dn + n-64
  enddo
  if(len.lt.9) then
     do i=len+1,9
        dn=27*dn
     enddo
  endif

  n2=mod(dn,32768.d0)
  dn=dn/32768.d0
  n1=dn

  return
end subroutine packname
