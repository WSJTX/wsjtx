subroutine synciscat(cdat,npts,nh,npct,s0,jsym,df,ntol,          &
     mousebutton,mode4,nafc,psavg,xsync,sig,ndf0,msglen,         &
     ipk,jpk,idf,df1)

! Synchronize an ISCAT signal
! cdat() is the downsampled analytic signal.  
! Sample rate = fsample = BW = 11025 * (9/32) = 3100.78125 Hz
! npts, nsps, etc., are all reduced by 9/32

  parameter (NMAX=30*3101)
  parameter (NSZ=4*1400)
  complex cdat(NMAX)
  complex c(288)
  real s0(288,NSZ)
  real fs0(288,96)                        !108 = 96 + 3*4
  real savg(288)
  real sref(288)
  real psavg(72)                          !Average spectrum of whole file
  integer icos(4)
  data icos/0,1,3,2/
  data nsync/4/,nlen/2/,ndat/18/

! Silence compiler warnings:
  sigbest=-20.0
  ndf0best=0
  msglenbest=0
  ipkbest=0
  jpkbest=0
  ipk2=0
  idfbest=mousebutton

  fsample=3100.78125                   !New sample rate
  nsps=144/mode4
  nsym=npts/nsps - 1
  nblk=nsync+nlen+ndat
  nfft=2*nsps                          !FFTs at twice the symbol length,

  kstep=nsps/4                         !  stepped by 1/4 symbol
  df=fsample/nfft
  fac=1.0/1000.0                       !Somewhat arbitrary
  savg=0.
  s0=0.

  ia=1-kstep
  do j=1,4*nsym                                   !Compute symbol spectra
     ia=ia+kstep
     ib=ia+nsps-1
     if(ib.gt.npts) exit
     c(1:nsps)=fac*cdat(ia:ib)
     c(nsps+1:nfft)=0.
     call four2a(c,nfft,1,-1,1)
     do i=1,nfft
        s0(i,j)=real(c(i))**2 + aimag(c(i))**2
        savg(i)=savg(i) + s0(i,j)                 !Accumulate avg spectrum
     enddo
     i0=40
  enddo

  jsym=4*nsym
  savg=savg/jsym

  do i=1,71                                   !Compute spectrum in dB, for plot
     if(mode4.eq.1) then
        psavg(i)=2*db(savg(4*i)+savg(4*i-1)+savg(4*i-2)+savg(4*i-3)) + 1.0
     else
        psavg(i)=2*db(savg(2*i)+savg(2*i-1)) + 7.0
     endif
  enddo

  do i=nh+1,nfft-nh
     call pctile(savg(i-nh),2*nh+1,npct,sref(i))
  enddo
  sref(1:nh)=sref(nh+11)
  sref(nfft-nh+1:nfft)=sref(nfft-nh)

  do i=1,nfft                                 !Normalize the symbol spectra
     fac=1.0/sref(i)
     if(i.lt.11) fac=1.0/savg(11)
     do j=1,jsym
        s0(i,j)=fac*s0(i,j)
     enddo
  enddo

  nfold=jsym/96
  jb=96*nfold

  ttot=npts/fsample                         !Length of record (s)
  df1=df/ttot                               !Step size for f1=fdot
  idf1=int(-25.0/df1)
  idf2=int(5.0/df1)
  if(nafc.eq.0) then
     idf1=0
     idf2=0
  else if(mod(-idf1,2).eq.1) then
     idf1=idf1-1
  endif

  xsyncbest=0.
  do idf=idf1,idf2                         !Loop over fdot
     fs0=0.
     do j=1,jb                             !Fold s0 into fs0, modulo 4*nblk 
        k=mod(j-1,4*nblk)+1
        ii=nint(idf*float(j-jb/2)/float(jb))
        ia=max(1-ii,1)
        ib=min(nfft-ii,nfft)
        do i=ia,ib
           fs0(i,k)=fs0(i,k) + s0(i+ii,j)
        enddo
     enddo
     ref=nfold*4

     i0=27
     if(mode4.eq.1) i0=95
     ia=i0-nint(ntol/df)
     ib=i0+nint(ntol/df)
     if(ia.lt.1) ia=1
     if(ib.gt.nfft-3) ib=nfft-3

     smax=0.
     ipk=1
     jpk=1
     do j=0,4*nblk-1                            !Find sync pattern: lags 0-95
        do i=ia,ib                              !Search specified freq range
           ss=0.
           do n=1,4                             !Sum over 4 sync tones
              k=j+4*n-3
              if(k.gt.96) k=k-96
              ss=ss + fs0(i+2*icos(n),k)
           enddo
           if(ss.gt.smax) then
              smax=ss
              ipk=i                             !Frequency offset, DF
              jpk=j+1                           !Time offset, DT
           endif
        enddo
     enddo

     xsync=smax/ref - 1.0
     if(nfold.lt.26) xsync=xsync * sqrt(nfold/26.0)
     xsync=xsync-0.5                           !Empirical

     sig=db(smax/ref - 1.0) - 15.0
     if(mode4.eq.1) sig=sig-5.0
!     if(sig.lt.-20 .or. xsync.lt.1.0) sig=-20.0
!     if(sig.lt.-20) sig=-20.0
     ndf0=nint(df*(ipk-i0))

     smax=0.
     ja=jpk+16
     if(ja.gt.4*nblk) ja=ja-4*nblk
     jj=jpk+20
     if(jj.gt.4*nblk) jj=jj-4*nblk
     do i=ipk,ipk+60,2                         !Find User's message length
        ss=fs0(i,ja) + fs0(i+10,jj)
        if(ss.gt.smax) then
           smax=ss
           ipk2=i
        endif
     enddo

     msglen=(ipk2-ipk)/2
     if(msglen.lt.2 .or. msglen.gt.29) cycle

     if(xsync.ge.xsyncbest) then
        xsyncbest=xsync
        sigbest=sig
        ndf0best=ndf0
        msglenbest=msglen
        ipkbest=ipk
        jpkbest=jpk
        idfbest=idf
     endif
  enddo

  xsync=xsyncbest
  sig=sigbest
  ndf0=ndf0best
  msglen=msglenbest
  ipk=ipkbest
  jpk=jpkbest
  idf=idfbest
  if(nafc.eq.0) idf=0

  return
end subroutine synciscat
