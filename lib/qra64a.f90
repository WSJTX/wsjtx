subroutine qra64a(dd,npts,nf1,nf2,nfqso,ntol,mode64,minsync,ndepth,   &
     emedelay,mycall_12,hiscall_12,hisgrid_6,sync,nsnr,dtx,nfreq,decoded,nft)

  use packjt
  use timer_module, only: timer
  
  parameter (NMAX=60*12000,LN=1152*63)
  character decoded*22
  character*12 mycall_12,hiscall_12
  character*6 mycall,hiscall,hisgrid_6
  character*4 hisgrid
  logical ltext
  complex c00(0:720000)                      !Analytic signal for dd()
  real dd(NMAX)                              !Raw data sampled at 12000 Hz
  integer dat4(12)                           !Decoded message (as 12 integers)
  data nc1z/-1/,nc2z/-1/,ng2z/-1/,maxaptypez/-1/
  save

  call timer('qra64a  ',0)
  irc=-1
  decoded='                      '
  nft=99
  if(nfqso.lt.nf1 .or. nfqso.gt.nf2) go to 900

  mycall=mycall_12(1:6)                     !### May need fixing? ###
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
  b90=1.0
  nFadingModel=1
  maxaptype=4
  if(iand(ndepth,64).ne.0) maxaptype=5
  call qra_params(ndepth,maxaptype,idfmax,idtmax,ibwmin,ibwmax,maxdist)
  if(nc1.ne.nc1z .or. nc2.ne.nc2z .or. ng2.ne.ng2z .or.            &
     maxaptype.ne.maxaptypez) then
     do naptype=0,maxaptype
        if(naptype.eq.2 .and. maxaptype.eq.4) cycle
        call qra64_dec(s3dummy,nc1,nc2,ng2,naptype,1,nSubmode,b90,      &
             nFadingModel,dat4,snr2,irc)
     enddo
     nc1z=nc1
     nc2z=nc2
     ng2z=ng2
     maxaptypez=maxaptype
  endif
  naptype=maxaptype

  call ana64(dd,npts,c00)
  
  call timer('sync64  ',0)
  call sync64(c00,nf1,nf2,nfqso,ntol,minsync,mode64,emedelay,dtx,f0,  &
       jpk0,sync,sync2,width)
  call timer('sync64  ',1)
  nfreq=nint(f0)
  if(mode64.eq.1 .and. minsync.ne.-1 .and. (sync-7.0).lt.minsync) go to 900

  nsps=6912
  call timer('qraloops',0)
  call qra_loops(c00,npts/2,nsps,64,mode64,nsubmode,nFadingModel,         &
       ndepth,nc1,nc2,ng2,naptype,jpk0,dtx,f0,width,snr2,irc,dat4)
  call timer('qraloops',1)
  
  decoded='                      '
  if(irc.ge.0) then
     call unpackmsg(dat4,decoded)               !Unpack the user message
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
  nfreq=nint(f0)

900 if(irc.lt.0) then
     sy=max(1.0,sync)
     if(nSubmode.eq.0) nsnr=nint(10.0*log10(sy)-35.0)   !A
     if(nSubmode.eq.1) nsnr=nint(10.0*log10(sy)-34.0)   !B
     if(nSubmode.eq.2) nsnr=nint(10.0*log10(sy)-29.0)   !C
     if(nSubmode.eq.3) nsnr=nint(10.0*log10(sy)-29.0)   !D
     if(nSubmode.eq.4) nsnr=nint(10.0*log10(sy)-24.0)   !E
  endif
  call timer('qra64a  ',1)

  return
end subroutine qra64a

subroutine qra_params(ndepth,maxaptype,idf0max,idt0max,ibwmin,ibwmax,maxdist)

! If file qra_params is present in CWD, read decoding params from it.

  integer iparam(7)
  logical first,ex
! data iparam/3,5,11,11,0,11,60/     !Maximum effort
  data iparam/3,5,7,7,0,4,15/        !Default values
  data first/.true./
  save first,iparam

  if(first) then
     inquire(file='qra_params',exist=ex)
     if(ex) then
        open(29,file='qra_params',status='old')
        read(29,*) iparam
        close(29)
     endif
     first=.false.
  endif
  ndepth=iparam(1)
  maxaptype=iparam(2)
  idf0max=iparam(3)
  idt0max=iparam(4)
  ibwmin=iparam(5)
  ibwmax=iparam(6)
  maxdist=iparam(7)

  return
end subroutine qra_params
