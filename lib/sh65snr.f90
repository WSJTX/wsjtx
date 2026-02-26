subroutine sh65snr(x,nz,snr)

  real x(nz)

  ipk=0 !Shut up compiler warnings. -db
  smax=-1.e30
  do i=1,nz
     if(x(i).gt.smax) then
        ipk=i
        smax=x(i)
     endif
     s=s+x(i)
  enddo

  s=0.
  ns=0
  do i=1,nz
     if(abs(i-ipk).ge.3) then
        s=s+x(i)
        ns=ns+1
     endif
  enddo
  ave=s/ns

  sq=0.
  do i=1,nz
     if(abs(i-ipk).ge.3) then
        sq=sq+(x(i)-ave)**2
        ns=ns+1
     endif
  enddo
  rms=sqrt(sq/(nz-2))
  snr=(smax-ave)/rms

  return
end subroutine sh65snr
