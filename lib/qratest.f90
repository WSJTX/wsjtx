program qratest

  parameter (NMAX=60*12000) 
  real dd(NMAX)
  character arg*8,mycall*12,hiscall*12,hisgrid*6,decoded*22
  character c*1
  logical loop

  nargs=iargc()
  if(nargs.lt.1 .or. nargs.gt.4) then
     print*,'Usage: qratest nfile [sync f0 fTol]'
     go to 999
  endif
  call getarg(1,arg)
  read(arg,*) nfile
  loop=arg(1:1).eq.'+'
  minsync0=-1
  nfqso0=-1
  ntol0=-1
  if(nargs.gt.1) then
     call getarg(2,arg)
     read(arg,*) minsync0
     call getarg(3,arg)
     read(arg,*) nfqso0
     call getarg(4,arg)
     read(arg,*) ntol0
  endif
  ndepth=3
  nft=99
  
  open(60,file='qra64_data.bin',access='stream')

  do ifile=1,999
     read(60,end=999) dd,npts,nutc,nf1,nf2,nfqso,ntol,mode64,minsync,ndepth, &
          mycall,hiscall,hisgrid
     if(ifile.lt.nfile) cycle
     
     if(minsync0.ne.-1) minsync=minsync0
     if(nfqso0.ne.-1) nfqso=nfqso0
     if(ntol0.ne.-1) ntol=ntol0

     call qra64a(dd,npts,nf1,nf2,nfqso,ntol,mode64,minsync,ndepth,      &
          mycall,hiscall,hisgrid,sync,nsnr,dtx,nfreq,decoded,nft)
     c='a'
     if(mode64.eq.2) c='b'
     if(mode64.eq.4) c='c'
     if(mode64.eq.8) c='d'
     if(mode64.eq.16) c='e'
     write(*,1000) ifile,c,nutc,nsnr,dtx,nfreq,decoded,nft-100,sync-3.4
1000 format(i4,1x,a1,1x,i4.4,i4,f6.2,i5,1x,a22,i3,f6.2)
     if(ifile.eq.nfile .and. (.not.loop)) exit
  enddo

999 end program qratest
