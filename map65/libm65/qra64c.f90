subroutine qra64c(cx,cy,nutc,nqd,ikhz,nfqso,ntol,xpol,mycall_12,     &
     hiscall_12,hisgrid_6,mode64,nwrite_qra64)

  use packjt
  parameter (NFFT2=336000)               !56*6000 (downsampled by 1/16)
  parameter (NMAX=60*12000,LN=1152*63)

  character decoded*22
  character*12 mycall_12,hiscall_12
  character*6 mycall,hiscall,hisgrid_6,grid
  character*4 hisgrid
  character cp*1,cmode*2
  logical xpol,ltext
  complex cx(0:NFFT2-1),cy(0:NFFT2-1)
  complex c00(0:720000)                      !Complex spectrum of dd()
  complex c0(0:720000)                       !Complex data for dd()
  real a(3)
  real s3(LN)                                !Symbol spectra
  real s3a(LN)                               !Symbol spectra
  integer dat4(12)                           !Decoded message (as 12 integers)
  integer dat4x(12)
  integer nap(0:11)
  data nap/0,2,3,2,3,4,2,3,6,4,6,6/,cmode/'$'/
  data nc1z/-1/,nc2z/-1/,ng2z/-1/,maxaptypez/-1/
  save

! For now:
  nf1=-3000
  nf2=3000
  !  mode64=1
  minsync=-1
  ndepth=3
  emedelay=2.5
  
  irc=-1
  nwrite_qra64=0
  decoded='                      '
  nft=99
  mycall=mycall_12(1:6)
  hiscall=hiscall_12(1:6)
  hisgrid=hisgrid_6(1:4)
  call packcall(mycall,nc1,ltext)
  call packcall(hiscall,nc2,ltext)
  call packgrid(hisgrid,ng2,ltext)
  nSubmode=0
  if(mode64.eq.2) nSubmode=1
  if(mode64.eq.4) nSubmode=2
  if(mode64.eq.8) nSubmode=3
  if(mode64.eq.16) nSubmode=4
  cmode(2:2)=char(ichar('A')+nSubmode)
  b90=1.0
  nFadingModel=1
  maxaptype=4
  if(iand(ndepth,64).ne.0) maxaptype=5
  if(nc1.ne.nc1z .or. nc2.ne.nc2z .or. ng2.ne.ng2z .or.            &
     maxaptype.ne.maxaptypez) then
     do naptype=0,maxaptype
        if(naptype.eq.2 .and. maxaptype.eq.4) cycle
        call qra64_dec(s3,nc1,nc2,ng2,naptype,1,nSubmode,b90,      &
             nFadingModel,dat4,snr2,irc)
     enddo
     nc1z=nc1
     nc2z=nc2
     ng2z=ng2
     maxaptypez=maxaptype
  endif
  naptype=maxaptype
  npts2=NFFT2

  ipz=0
  if(xpol) ipz=3
  do ip=0,ipz
     if(ip.eq.0) c00(0:NFFT2-1)=conjg(cx)
     if(ip.eq.1) c00(0:NFFT2-1)=0.707*conjg(cx+cy)
     if(ip.eq.2) c00(0:NFFT2-1)=conjg(cy)
     if(ip.eq.3) c00(0:NFFT2-1)=0.707*conjg(cx-cy)

     call sync64(c00,nf1,nf2,nfqso,ntol,mode64,emedelay,dtx,f0,jpk0,sync,  &
          sync2,width)

     nfreq=nint(f0)
     if(mode64.eq.1 .and. minsync.ge.0 .and. (sync-7.0).lt.minsync) go to 900
     a=0.
     a(1)=-f0
     call twkfreq(c00,c0,npts2,6000.0,a)

     irc=-99
     s3lim=20.
     itryz=5
     itz=11
     if(mode64.eq.4) itz=9
     if(mode64.eq.2) itz=7
     if(mode64.eq.1) itz=5

     if(mode64.eq.1) then
        itz=0
        itryz=1
     endif

     LL=64*(mode64+2)
     NN=63
     napmin=99
     do itry0=1,itryz
        idt=itry0/2
        if(mod(itry0,2).eq.0) idt=-idt
        jpk=jpk0 + 750*idt
        call spec64(c0,npts2,mode64,jpk,s3a,LL,NN)
        call pctile(s3a,LL*NN,40,base)
        s3a=s3a/base
        where(s3a(1:LL*NN)>s3lim) s3a(1:LL*NN)=s3lim
        do iter=itz,0,-2
           b90=1.728**iter
           if(b90.gt.230.0) cycle
           if(b90.lt.0.15*width) exit
           s3(1:LL*NN)=s3a(1:LL*NN)
           call timer('qra64_de',0)
           call qra64_dec(s3,nc1,nc2,ng2,naptype,0,nSubmode,b90,      &
                nFadingModel,dat4,snr2,irc)
           call timer('qra64_de',1)
           if(irc.eq.0) go to 10
           if(irc.gt.0) call badmsg(irc,dat4,nc1,nc2,ng2)
           iirc=max(0,min(irc,11))
           if(irc.gt.0 .and. nap(iirc).lt.napmin) then
              dat4x=dat4
              b90x=b90
              snr2x=snr2
              napmin=nap(iirc)
              irckeep=irc
              dtxkeep=jpk/6000.0 - 1.0
              itry0keep=itry0
              iterkeep=iter
              npolkeep=ip*45
           endif
        enddo
        if(irc.eq.0) goto 5
     enddo
  enddo

5 if(napmin.ne.99) then
     dat4=dat4x
     b90=b90x
     snr2=snr2x
     irc=irckeep
     dtx=dtxkeep
     itry0=itry0keep
     iter=iterkeep
     npol=npolkeep
  endif
10 decoded='                      '

  if(irc.ge.0) then
     if(irc.eq.0) npol=ip*45
     call unpackmsg(dat4,decoded)           !Unpack the user message
     call fmtmsg(decoded,iz)
     if(index(decoded,"000AAA ").ge.1) then
        ! Suppress a certain type of garbage decode.
        decoded='                      '
        irc=-1
     endif
     nft=100 + irc
     nsnr=nint(snr2)
  else
     snr2=0.
  endif

900 if(irc.lt.0) then
     sy=max(1.0,sync)
     if(nSubmode.eq.0) nsnr=nint(10.0*log10(sy)-35.0)   !A
     if(nSubmode.eq.1) nsnr=nint(10.0*log10(sy)-34.0)   !B
     if(nSubmode.eq.2) nsnr=nint(10.0*log10(sy)-29.0)   !C
     if(nSubmode.eq.3) nsnr=nint(10.0*log10(sy)-29.0)   !D
     if(nSubmode.eq.4) nsnr=nint(10.0*log10(sy)-24.0)   !E
  endif

!  If Tx station's grid is in decoded message, compute optimum TxPol
  i1=index(decoded,' ')
  i2=index(decoded(i1+1:),' ') + i1
  grid='      '
  if(i2.ge.8 .and. i2.le.18) grid=decoded(i2+1:i2+4)//'mm'
  ntxpol=0
  cp=' '
  if(xpol) then
     if(grid(1:1).ge.'A' .and. grid(1:1).le.'R' .and.           &
          grid(2:2).ge.'A' .and. grid(2:2).le.'R' .and.         &
          grid(3:3).ge.'0' .and. grid(3:3).le.'9' .and.         &
          grid(4:4).ge.'0' .and. grid(4:4).le.'9') then                 
        ntxpol=mod(npol-nint(2.0*dpol(mygrid,grid))+720,180)
        if(nxant.eq.0) then
           cp='H'
           if(ntxpol.gt.45 .and. ntxpol.le.135) cp='V'
        else
           cp='/'
           if(ntxpol.ge.90 .and. ntxpol.lt.180) cp='\\'
        endif
     endif
  endif
  
  if(irc.ge.0) then
     write(*,1010) ikHz,nfreq,npol,nutc,dtx,nsnr,cmode(1:1),decoded,   &
          irc,ntxpol,cp
1010 format('!',i3,i5,i4,i6.4,f5.1,i5,1x,a1,1x,a22,i2,5x,i5,1x,a1)
     nwrite_qra64=nwrite_qra64+1
     freq=144.0 + 0.001*ikhz
     write(21,1014) freq,nfreq,dtx,npol,nsnr,nutc,decoded,cp,          &
          cmode(1:1),cmode(2:2)
1014 format(f8.3,i5,f5.1,2i4,i5.4,2x,a22,2x,a1,3x,a1,1x,a1)
     
     if(index(decoded,'CQ ').gt.0 .or. index(decoded,'QRZ ').gt.0 .or.     &
          index(decoded,'QRT ').gt.0 .or. index(decoded,'CQV ').gt.0 .or.  &
          index(decoded,'CQH ').gt.0) then
        write(19,1016) ikhz,nfreq,npol,nutc,dtx,nsnr,decoded,0,cmode
1016    format(i3,i5,i4,i5.4,f7.1,i4,2x,a22,i3,1x,a2)
        flush(19)
     endif
  else
     write(*,1010) ikHz,nfreq,npol,nutc,dtx,nsnr
     nwrite_qra64=nwrite_qra64+1
  endif

  return
end subroutine qra64c
