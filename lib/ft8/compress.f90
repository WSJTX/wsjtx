subroutine compress(c)

  parameter (NMAX=15*12000)             !Samples in iwave (180,000)
  complex c(0:NMAX-1)
  real xr(0:NMAX-1),xi(0:NMAX-1)

  xr=real(c)
  call wavestats(xr,NMAX,rms,pk,pwr_pk,pwr_ave)
  xr=xr/rms
  xi=aimag(c)/rms

  do i=0,NMAX-1
     c(i)=rms*cmplx(h1(xr(i)),h1(xi(i)))
  enddo
  
!  par=pwr_pk/pwr_ave
!  write(*,1010) 5,rms,pk,pwr_pk,pwr_ave,par
!1010 format(i3,2f10.3,3f10.2)
!  call wavestats(xi,NMAX,rms,pk,pwr_pk,pwr_ave)
!  par=pwr_pk/pwr_ave
!  write(*,1010) 5,rms,pk,pwr_pk,pwr_ave,par

  return
end subroutine compress

subroutine wavestats(x,kz,rms,pk,pwr_pk,pwr_ave)

  real x(kz)

  sumsq=dot_product(x,x)
  rms=sqrt(sumsq/kz)
  pk=max(maxval(x),-minval(x))
  pwr_pk=pk*pk
  pwr_ave=sumsq/kz
  
  return
end subroutine wavestats
