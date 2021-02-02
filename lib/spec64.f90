subroutine spec64(c0,nsps,mode_q65,jpk,s3,LL,NN)

  parameter (MAXFFT=20736)
!### Fix this:
  complex c0(0:1800000-1)                    !Complex spectrum of dd()
  complex cs(0:MAXFFT-1)                     !Complex symbol spectrum
  real s3(LL,NN)                             !Synchronized symbol spectra
  real xbase0(LL),xbase(LL)
!  integer ipk1(1)
  integer isync(22)                          !Indices of sync symbols
  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/

  nfft=nsps
  j=0
  n=1
  do k=1,84
     if(k.eq.isync(n)) then
        n=n+1
        cycle
     endif
     j=j+1
     ja=(k-1)*nsps + jpk
     jb=ja+nsps-1
     cs(0:nfft-1)=c0(ja:jb)
     call four2a(cs,nsps,1,-1,1)             !c2c FFT to frequency
     do ii=1,LL
        i=ii-65+mode_q65      !mode_q65 = 1 2 4 8 16 for Q65 A B C D E
        if(i.lt.0) i=i+nsps
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
