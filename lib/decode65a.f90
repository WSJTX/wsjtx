subroutine decode65a(dd,npts,newdat,nqd,f0,nflip,mode65,ntrials,     &
     naggressive,ndepth,ntol,mycall,hiscall,hisgrid,nQSOProgress,    &
     ljt65apon,bVHF,sync2,a,dt,nft,nspecial,qual,nhist,nsmo,decoded)

! Apply AFC corrections to a candidate JT65 signal, then decode it.

  use jt65_mod
  use timer_module, only: timer

  parameter (NMAX=60*12000)          !Samples per 60 s
  real*4  dd(NMAX)                   !92 MB: raw data from Linrad timf2
  complex cx(NMAX/8)                 !Data at 1378.125 sps
  complex cx1(NMAX/8)                !Data at 1378.125 sps, offset by 355.3 Hz
  complex c5x(NMAX/32)               !Data at 344.53125 Hz
  complex c5a(512)
  real s2(66,126)
  real a(5)
  logical bVHF,first,ljt65apon
  character decoded*22,decoded_best*22
  character mycall*12,hiscall*12,hisgrid*6
  character*27 cr
  data first/.true./,jjjmin/1000/,jjjmax/-1000/,cr/'(C) 2016, Joe Taylor - K1JT'/
  save

! Mix sync tone to baseband, low-pass filter, downsample to 1378.125 Hz
  call timer('filbig  ',0)
  call filbig(dd,npts,f0,newdat,cx,n5,sq0)
  if(mode65.eq.4) call filbig(dd,npts,f0+355.297852,newdat,cx1,n5,sq0)
  call timer('filbig  ',1)
! NB: cx has sample rate 12000*77125/672000 = 1378.125 Hz

! Check for a shorthand message
  if(bVHF .and. mode65.ne.101) then
     call sh65(cx,n5,mode65,ntol,xdf,nspecial,sync2)
     if(nspecial.gt.0) then
        a=0.
        a(1)=xdf
        nflip=0
     endif
  endif
  if(nflip.eq.0) go to 900

! Find best DF, drift, curvature, and DT.  Start by downsampling to 344.53125 Hz
  call fil6521(cx,n5,c5x,n6)

  fsample=1378.125/4.

  call timer('afc65b  ',0)
! Best fit for DF, drift, and dt. fsample = 344.53125 S/s
  dtbest=dt
  call afc65b(c5x,n6,fsample,nflip,mode65,a,ccfbest,dtbest)
  call timer('afc65b  ',1)
  dtbest=dtbest+0.003628 !Remove decimation filter and coh. integrator delay
  dt=dtbest              !Return new, improved estimate of dt
  sync2=3.7e-4*ccfbest/sq0                    !Constant is empirical 
  if(mode65.eq.4) cx=cx1

! Apply AFC corrections to the time-domain signal
! Now we are back to using the 1378.125 Hz sample rate, enough to 
! accommodate the full JT65C bandwidth.
  a(3)=0
  call twkfreq65(cx,n5,a)

! Compute spectrum for each symbol.
  nsym=126
  nfft=512
  df=1378.125/nfft
  j=int(dtbest*1378.125)

  c5a=cmplx(0.0,0.0)
  do k=1,nsym
     do i=1,nfft
        j=j+1
        if(j.ge.1 .and. j.le.NMAX/8) then
           c5a(i)=cx(j)
        else
           c5a(i)=0.
        endif
     enddo
     call four2a(c5a,nfft,1,1,1)
     do i=1,512
        jj=i
        if(i.gt.256) jj=i-512
        s1(jj,k)=real(c5a(i))**2 + aimag(c5a(i))**2
     enddo
  enddo

  call timer('dec65b  ',0)
  qualbest=0.
  nftbest=0
  qual0=-1.e30
  minsmo=0
  maxsmo=0
  if(mode65.ge.2 .and. mode65.ne.101) then
     minsmo=nint(width/df)-1
     maxsmo=2*nint(width/df)
  endif
  nn=0
  do ismo=minsmo,maxsmo
     if(ismo.gt.0) then
        do j=1,126
           call smo121(s1(-255,j),512)
           if(j.eq.1) nn=nn+1
           if(nn.ge.4) then
              call smo121(s1(-255,j),512)
              if(j.eq.1) nn=nn+1
           endif
        enddo
     endif

     do i=1,66
        jj=i
        if(mode65.eq.2) jj=2*i-1
        if(mode65.eq.4) then
           ff=4*(i-1)*df - 355.297852
           jj=nint(ff/df)+1
        endif
        s2(i,1:126)=s1(jj,1:126)
     enddo
     nadd=ismo  !### ??? ###
     call decode65b(s2,nflip,nadd,mode65,ntrials,naggressive,ndepth,        &
          mycall,hiscall,hisgrid,nQSOProgress,ljt65apon,nqd,nft,qual,       &
          nhist,decoded)
     if(nft.eq.1) then
        nsmo=ismo
        param(9)=nsmo
        nsum=1
        exit
     else if(nft.eq.2) then
        if(qual.gt.qualbest) then
           decoded_best=decoded
           qualbest=qual
           nnbest=nn
           nsmobest=ismo
           nftbest=nft
        endif
     endif
     if(qual.lt.qual0) exit
     qual0=qual
  enddo

  if(nftbest.eq.2) then
     decoded=decoded_best
     qual=qualbest
     nsmo=nsmobest
     param(9)=nsmo
     nn=nnbest
     nft=nftbest
  endif

  call timer('dec65b  ',1)

900 return
end subroutine decode65a
