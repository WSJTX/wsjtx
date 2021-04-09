program m65

! Decoder for map65.  Can run stand-alone, reading data from *.tf2 files;
! or as the back end of map65, with data placed in a shared memory region.

  parameter (NSMAX=60*96000)
  parameter (NFFT=32768)
  integer*2 i2(4,87)
  real*8 hsym
  real*4 ssz5a(NFFT)
  logical*1 lstrong(0:1023)
  common/tracer/limtrace,lu
  real*8 fc0,fcenter
  character*80 arg,infile
  character mycall*12,hiscall*12,mygrid*6,hisgrid*6,datetime*20
  common/datcom/dd(4,5760000),ss(4,322,NFFT),savg(4,NFFT),fc0,nutc0,junk(36)
  common/npar/fcenter,nutc,idphi,mousedf,mousefqso,nagain,                &
       ndepth,ndiskdat,neme,newdat,nfa,nfb,nfcal,nfshift,                 &
       mcall3,nkeep,ntol,nxant,nrxlog,nfsample,nxpol,nmode,               &
       nfast,nsave,mycall,mygrid,hiscall,hisgrid,datetime

  nargs=iargc()
  if(nargs.lt.1 .or. nargs.eq.2) then
     print*,'Usage: m65 <submode> <95238|96000> file1 [file2 ...]'
     print*,'  (Reads data from *.tf2 files.)'
     print*,''
     print*,'       m65 -s'
     print*,'  (Gets data from MAP65, via shared memory region.)'
     go to 999
  endif
  call getarg(1,arg)
  if(arg(1:2).eq.'-s') then
     call m65a
     call ftnquit
     go to 999
  endif
!### These need fixing:
  if(arg(1:1).eq.'A') nmode=1
  if(arg(1:1).eq.'B') nmode=2
  if(arg(1:1).eq.'C') nmode=3
!###
  nfsample=96000
  call getarg(2,arg)
  if(arg.eq.'95238') then
     nfsample=95238
     call getarg(3,arg)
  endif

  nxpol=1
  ifile1=3
  limtrace=0
  lu=12
  nfa=100
  nfb=162
  nfshift=6
  ndepth=2
  nfcal=344
  idphi=-50
  ntol=500
  nkeep=10

  call ftninit('.')

  do ifile=ifile1,nargs
     call getarg(ifile,infile)
     open(10,file=infile,access='stream',status='old',err=998)
     i1=index(infile,'.tf2')
     read(infile(i1-4:i1-1),*,err=1) nutc0
     go to 2
1    nutc0=0
2    hsym=2048.d0*96000.d0/11025.d0          !Samples per half symbol
     nhsym0=-999
     k=0
     fcenter=144.125d0
     mousedf=0
     mousefqso=125
     newdat=1
     mycall='K1JT'

     if(ifile.eq.ifile1) call timer('m65     ',0)
     do irec=1,9999999
        call timer('read_tf2',0)
        read(10) i2
        call timer('read_tf2',1)
        
        call timer('float   ',0)
        do i=1,87
           k=k+1
           dd(1,k)=i2(1,i)
           dd(2,k)=i2(2,i)
           dd(3,k)=i2(3,i)
           dd(4,k)=i2(4,i)
        enddo
        call timer('float   ',1)
        nhsym=(k-2048)/hsym
        if(nhsym.ge.1 .and. nhsym.ne.nhsym0) then
           ndiskdat=1
           nb=0
! Emit signal readyForFFT
           call timer('symspec ',0)
           fgreen=-13.0
           iqadjust=0
           iqapply=0
           nbslider=100
           gainx=0.9962
           gainy=1.0265
           phasex=0.01426
           phasey=-0.01195
           call symspec(k,nxpol,ndiskdat,nb,nbslider,idphi,nfsample,   &
                fgreen,iqadjust,iqapply,gainx,gainy,phasex,phasey,rejectx,   &
                rejecty,pxdb,pydb,ssz5a,nkhz,ihsym,nzap,slimit,lstrong)
           call timer('symspec ',1)
           nhsym0=nhsym
           if(ihsym.ge.278) go to 10
        endif
     enddo

10   continue
     if(iqadjust.ne.0) write(*,3002) rejectx,rejecty
3002 format('Image rejection:',2f7.1,' dB')
     nutc=nutc0
     nstandalone=1
     call decode0(dd,ss,savg,nstandalone,nfsample)
  enddo

  call timer('m65     ',1)
  call timer('m65     ',101)
  call ftnquit
  go to 999

998 print*,'Cannot open file:'
  print*,infile

999 end program m65
