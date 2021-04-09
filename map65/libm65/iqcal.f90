subroutine iqcal(nn,c,nfft,gain,phase,zsum,ipk,reject)

  complex c(0:nfft-1)
  complex z,zsum,zave

  if(nn.eq.0) then
     zsum=0.
  endif
  nn=nn+1
  smax=0.
  ipk=1
  do i=1,nfft-1                       !Find strongest signal
     s=real(c(i))**2 + aimag(c(i))**2
     if(s.gt.smax) then
        smax=s
        ipk=i
     endif
  enddo
  pimage=real(c(nfft-ipk))**2 + aimag(c(nfft-ipk))**2
  p=smax + pimage
  z=c(ipk)*c(nfft-ipk)/p              !Synchronous detection of image
  zsum=zsum+z
  zave=zsum/nn
  tmp=sqrt(1.0 - (2.0*real(zave))**2)
  phase=asin(2.0*aimag(zave)/tmp)    !Estimate phase
  gain=tmp/(1.0-2.0*real(zave))      !Estimate gain
  reject=10.0*log10(pimage/smax)

  return
end subroutine iqcal
