subroutine spec64(c0,npts2,mode64,jpk,s3,LL,NN)

  parameter (NSPS=3456)                      !Samples per symbol at 6000 Hz
  complex c0(0:360000)                       !Complex spectrum of dd()
  complex cs(0:NSPS-1)                       !Complex symbol spectrum
  real s3(LL,NN)                             !Synchronized symbol spectra
  real xbase0(LL),xbase(LL)

  nfft=nsps
  fac=1.0/nfft
  do j=1,NN
     jj=j+7                                  !Skip first Costas array
     if(j.ge.33) jj=j+14                     !Skip middle Costas array
     ja=jpk + (jj-1)*nfft
     jb=ja+nfft-1
     cs(0:nfft-1)=fac*c0(ja:jb)
     call four2a(cs,nfft,1,-1,1)
     do ii=1,LL
        i=ii-65
        if(i.lt.0) i=i+nfft
        s3(ii,j)=real(cs(i))**2 + aimag(cs(i))**2
     enddo
  enddo

  df=6000.0/nfft
  do i=1,LL
     call pctile(s3(i,1:NN),NN,45,xbase0(i)) !Get baseline for passband shape
  enddo
  
  nh=25
  xbase(1:nh-1)=sum(xbase0(1:nh-1))/(nh-1.0)
  xbase(LL-nh+1:LL)=sum(xbase0(LL-nh+1:LL))/(nh-1.0)
  do i=nh,LL-nh
     xbase(i)=sum(xbase0(i-nh+1:i+nh))/(2*nh+1)  !Smoothed passband shape
  enddo
  
  do i=1,LL
     s3(i,1:NN)=s3(i,1:NN)/(xbase(i)+0.001) !Apply frequency equalization
  enddo

  return
end subroutine spec64
