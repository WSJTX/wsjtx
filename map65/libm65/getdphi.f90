subroutine getdphi(qphi)

  real qphi(12)

  s=0.
  c=0.
  do i=1,12
     th=i*30/57.2957795
     s=s+qphi(i)*sin(th)
     c=c+qphi(i)*cos(th)
  enddo

  dphi=57.2957795*atan2(s,c)
  write(*,1010) nint(dphi)
1010 format('!Best-fit Dphi =',i4,' deg')

  return
  end
