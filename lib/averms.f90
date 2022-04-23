subroutine averms(x,n,nskip,ave,rms)
  real x(n)
  integer ipk(1)

  ns=0
  s=0.
  sq=0.
  ipk=maxloc(x)
  do i=1,n
     if((nskip.lt.0) .or. (abs(i-ipk(1)).gt.nskip)) then
        s=s + x(i)
        sq=sq + x(i)**2
        ns=ns+1
     endif
  enddo
  ave=s/ns
  rms=sqrt(sq/ns - ave*ave)
 
  return
end subroutine averms
