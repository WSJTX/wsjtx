subroutine spec66(c0,nsps,s3,LL,NN)

! Compute synchronized symbol spectra.  

  complex c0(0:85*nsps-1)                !Synchronized complex data at 6000 S/s
  complex cs(0:nsps-1)                   !Complex symbol spectrum
  real s3(LL,NN)                         !Synchronized symbol spectra
  real xbase0(LL),xbase(LL)              !Work arrays

  fac=1.0/nsps
  ja=-nsps
  do j=1,NN
     ja=ja+nsps
     if(mod(ja/nsps,4).eq.0) ja=ja+nsps
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
end subroutine spec66
