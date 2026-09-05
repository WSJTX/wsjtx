subroutine jtty_spec(iwave,iz)

  parameter (NFFT=262144,NH=NFFT/2)
  integer*2 iwave(iz)
  real*4 x(NFFT)
  real*4 s(0:NH)
  complex c(0:NH)
  equivalence (x,c)
  
  x(1:iz)=iwave
  x(iz+1:)=0.
  call four2a(c,NFFT,1,-1,0)

  do i=0,NH
     c(i)=c(i)/NFFT
     s(i)=real(c(i))**2 + aimag(c(i))**2
  enddo
  do i=1,200
     call smo121(s,NH+1)
  enddo
  smax=maxval(s)
  s=s/(4.0*smax)
  tpmax=sum(s)

  df=12000.0/NFFT
  f1=0.
  f2=0.
  tp=0.
  do i=0,NH
     if(f1.eq.0.0 .and. db(s(i)).gt.-60.0) f1=i*df
     if(i*df.gt.1500.0 .and. db(s(i)).gt.-60.0) f2=i*df
     tp=tp+s(i)
     write(70,3070) i*df,db(s(i)),100.0*tp/tpmax,s(i)
3070 format(3f12.3,e12.3)
  enddo
  write(*,1000) f1,f2,f2-f1
1000 format(3f10.1)
  
  return
end subroutine jtty_spec
