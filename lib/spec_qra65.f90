subroutine spec_qra65(c0,nsps,s3,LL,NN)

! Compute synchronized symbol spectra.  

  complex c0(0:85*nsps-1)                !Synchronized complex data at 6000 S/s
  complex, allocatable :: cs(:)          !Complex symbol spectrum
  real s3(LL,NN)                         !Synchronized symbol spectra
  real xbase0(LL),xbase(LL)              !Work arrays
  integer isync(22)                      !Indices of sync symbols
  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/

  allocate(cs(0:nsps-1))
  fac=1.0/nsps
  j=0
  n=1
  do k=1,84
     if(k.eq.isync(n)) then
        n=n+1
        cycle
     endif
     j=j+1
     ja=(k-1)*nsps
     jb=ja+nsps-1
     cs=fac*c0(ja:jb)
     call four2a(cs,nsps,1,-1,1)             !c2c FFT to frequency
     do ii=1,LL
        i=ii-65
        if(i.lt.0) i=i+nsps
        s3(ii,j)=real(cs(i))**2 + aimag(cs(i))**2
     enddo
  enddo

  df=6000.0/nsps
  do i=1,LL
     call pctile(s3(i,1:NN),NN,45,xbase0(i)) !Get baseline for passband shape
  enddo
  
  nh=9
  xbase(1:nh-1)=sum(xbase0(1:nh-1))/(nh-1.0)
  xbase(LL-nh+1:LL)=sum(xbase0(LL-nh+1:LL))/(nh-1.0)
  do i=nh,LL-nh
     xbase(i)=sum(xbase0(i-nh+1:i+nh))/(2*nh+1)  !Smoothed passband shape
  enddo
  
  do i=1,LL
     s3(i,1:NN)=s3(i,1:NN)/(xbase(i)+0.001) !Apply frequency equalization
  enddo

  return
end subroutine spec_qra65
