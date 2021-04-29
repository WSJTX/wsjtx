program m65

! Decoder for map65.  Can run stand-alone, reading data from *.tf2 files;
! or as the back end of map65, with data placed in a shared memory region.

  use timer_module, only: timer
  use timer_impl, only: init_timer, fini_timer

  parameter (NSMAX=60*96000)
  parameter (NFFT=32768)
  parameter (NREAD=2048)
  integer*2 i2(NREAD)
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
  if(nargs.ne.1 .and. nargs.lt.5) then
     print*,'Usage:    m65 Jsub Qsub Xpol <95238|96000> file1 [file2 ...]'
     print*,'Examples: m65   B   A    X       96000     *.tf2'
     print*,'          m65   C   C    N       96000     *.iq'
     print*,''
     print*,'          m65 -s'
     print*,'  (Gets data from MAP65, via shared memory region.)'
     go to 999
  endif
  call getarg(1,arg)
  if(arg(1:2).eq.'-s') then
     call m65a
     call ftnquit
     go to 999
  endif
  n=1
  if(arg(1:1).eq.'0') n=0
  if(arg(1:1).eq.'A') n=1
  if(arg(1:1).eq.'B') n=2
  if(arg(1:1).eq.'C') n=3

  call getarg(2,arg)
  m=1
  if(arg(1:1).eq.'0') m=0
  if(arg(1:1).eq.'A') m=1
  if(arg(1:1).eq.'B') m=2
  if(arg(1:1).eq.'C') m=3
  if(arg(1:1).eq.'D') m=4
  if(arg(1:1).eq.'E') m=5
  nmode=10*m + n

  call getarg(3,arg)
  nxpol=0
  if(arg(1:1).eq.'X')   nxpol=1

  call getarg(4,arg)
  nfsample=96000
  if(arg.eq.'95238') nfsample=95238

  ifile1=5
  limtrace=0
  lu=12

! Some default parameters for command-line execution, in early testing.
  mycall='K1JT'
  mygrid='FN20QI'
  hiscall='K9AN'
  hisgrid='EN50'
  nfa=100
  nfb=162
  ntol=100
  nkeep=10
  mousefqso=141
  mousedf=-201
  nfcal=-720

  if(nxpol.eq.0) then
     nfa=55
     nfb=143
     mousefqso=70
  endif

  call ftninit('.')
  call init_timer('timer.out')
  call timer('m65     ',0)
        
  do ifile=ifile1,nargs
     call getarg(ifile,infile)
     open(10,file=infile,access='stream',status='old',err=998)
     i1=index(infile,'.tf2')
     if(i1.lt.1) i1=index(infile,'.iq')
     read(infile(i1-4:i1-1),*,err=1) nutc0
     go to 2
1    nutc0=0
2    hsym=2048.d0*96000.d0/11025.d0          !Samples per half symbol
     read(10) fcenter
     newdat=1
     nhsym0=-999
     k=0

     nch=2
     if(nxpol.eq.1) nch=4

!     if(ifile.eq.ifile1) call timer('m65     ',0)
     do irec=1,9999999
        read(10,end=10) i2
        do i=1,NREAD,nch
           k=k+1
           if(k.gt.60*96000) exit
           dd(1,k)=i2(i)
           dd(2,k)=i2(i+1)
           if(nxpol.eq.1) then
              dd(3,k)=i2(i+2)
              dd(4,k)=i2(i+3)
           endif
        enddo
        nhsym=(k-2048)/hsym
        if(nhsym.ge.1 .and. nhsym.ne.nhsym0) then
           ndiskdat=1
           nb=0
! Emit signal readyForFFT
           fgreen=-13.0
           iqadjust=0
           iqapply=0
           nbslider=100
           gainx=0.9962
           gainy=1.0265
           phasex=0.01426
           phasey=-0.01195
           call timer('symspec ',0)
           call symspec(k,nxpol,ndiskdat,nb,nbslider,idphi,nfsample,   &
                fgreen,iqadjust,iqapply,gainx,gainy,phasex,phasey,rejectx,   &
                rejecty,pxdb,pydb,ssz5a,nkhz,ihsym,nzap,slimit,lstrong)
           call timer('symspec ',1)
           nhsym0=nhsym
!           if(ihsym.ge.278) go to 10
        endif
     enddo

10   continue
     if(iqadjust.ne.0) write(*,3002) rejectx,rejecty
3002 format('Image rejection:',2f7.1,' dB')
     nutc=nutc0
     nstandalone=1
     call decode0(dd,ss,savg,nstandalone)
  enddo

  call timer('m65     ',1)
  call timer('m65     ',101)
  go to 999

998 print*,'Cannot open file:'
  print*,infile

999 call fini_timer()
  call ftnquit

end program m65
