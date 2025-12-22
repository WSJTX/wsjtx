subroutine freqcal(id2,k,nkhz,noffset,ntol,line)

  parameter (NZ=30*12000,NFFT=55296,NH=NFFT/2)
  integer*2 id2(0:NZ-1)
  complex sp,sn
  real x(0:NFFT-1)
  real xi(0:NFFT-1)
  real w(0:NFFT-1)                      !Window function
  real s(0:NH)
  character line*80,cflag*1
  logical first
  complex cx(0:NH)
  equivalence (x,cx)
  data n/0/,k0/9999999/,first/.true./
  save n,k0,w,first,pi,fs,xi

  if(first) then
     pi=4.0*atan(1.0)
     fs=12000.0
     do i=0,NFFT-1
        ww=sin(i*pi/NFFT)
        w(i)=ww*ww/NFFT
        xi(i)=2.0*pi*i
     enddo
     first=.false.
  endif
  
  if(k.lt.NFFT) go to 900
  if(k.lt.k0) n=0
  k0=k
     
  x=w*id2(k-NFFT:k-1)              !Apply window
  call four2a(x,NFFT,1,-1,0)       !Compute spectrum, r2c
  df=fs/NFFT
  if (ntol.gt.noffset) then
     ia=0
     ib=nint((noffset*2)/df)
  else
     ia=nint((noffset-ntol)/df)
     ib=nint((noffset+ntol)/df)
  endif
  smax=0.
  s=0.
  ipk=-99
  do i=ia,ib
     s(i)=real(cx(i))**2 + aimag(cx(i))**2
     if(s(i).gt.smax) then
        smax=s(i)
        ipk=i
     endif
  enddo

  if(ipk.ge.1) then
    call peakup(s(ipk-1),s(ipk),s(ipk+1),dx)
    fpeak=df * (ipk+dx)
    ap=(fpeak/fs+1.0/(2.0*NFFT))
    an=(fpeak/fs-1.0/(2.0*NFFT))
    sp=sum(id2((k-NFFT):k-1)*cmplx(cos(xi*ap),-sin(xi*ap)))
    sn=sum(id2((k-NFFT):k-1)*cmplx(cos(xi*an),-sin(xi*an)))
    fpeak=fpeak+fs*(abs(sp)-abs(sn))/(abs(sp)+abs(sn))/(2*NFFT)
    xsum=0.
    nsum=0
    do i=ia,ib
       if(abs(i-ipk).gt.10) then
          xsum=xsum+s(i)
          nsum=nsum+1
       endif
    enddo
    ave=xsum/nsum
    snr=db(smax/ave)
    pave=db(ave) + 8.0
  else
    snr=-99.9
    pave=-99.9
    fpeak=-99.9
    ferr=-99.9
  endif
  cflag=' '
  if(snr.lt.20.0) cflag='*'
  n=n+1
  nsec=mod(time(),86400)
  nhr=nsec/3600
  nmin=mod(nsec/60,60)
  nsec=mod(nsec,60)
  ncal=1
  ferr=fpeak-noffset
  write(line,1100)  nhr,nmin,nsec,nkhz,ncal,noffset,fpeak,ferr,pave,   &
            snr,cflag,char(0)
1100 format(i2.2,':',i2.2,':',i2.2,i7,i3,i6,2f10.3,2f7.1,2x,a1,a1)

900 return
end subroutine freqcal
