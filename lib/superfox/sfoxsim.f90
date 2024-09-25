program sfoxsim

! - Generate complex-valued SuperFox waveform, cdat.
! - Pass cdat through Watterson channel simulator
! - Add noise to the imaginary part of cdat and write to wav file.

  use wavhdr
  use qpc_mod
  use sfox_mod

  type(hdr) h                            !Header for .wav file
  logical*1 bMoreCQs                     !Include a CQ when space available?
  logical*1 bSendMsg                     !Send a Free text message
  integer*2 iwave(NMAX)                  !Generated i*2 waveform
  integer isync(24)                      !Indices of sync symbols
  integer itone(151)                     !Symbol values, data and sync
  integer*1 xin(0:49)
  integer*1 y(0:127)
  real*4 xnoise(NMAX)                    !Random noise
  real*4 dat(NMAX)                       !Generated real data
  complex cdat(NMAX)                     !Generated complex waveform
  complex crcvd(NMAX)                    !Signal as received
  real, allocatable :: s3(:,:)           !Symbol spectra: will be s3(NQ,NN)
  integer, allocatable :: msg0(:)        !Information symbols
  integer, allocatable :: chansym(:)     !Encoded data, 7-bit integers
  character fname*17,arg*12,channel*2,foxcall*11
  character*10 ckey
  character*26 text_msg
  character*120 line                     !SuperFox message pieces
  character*40 cmsg(5)
  data ckey/'0000000000'/
  data cmsg/'W0AAA RR73; W5FFF <K1JT> -18',  &
            'W1BBB RR73; W6GGG <K1JT> -15',  &
            'W2CCC RR73; W7HHH <K1JT> -12',  &
            'W3DDD RR73; W8III <K1JT> -09',  &
            'W4EEE RR73; W9JJJ <K1JT> -06'/
  data text_msg/'0123456789ABCDEFGHIJKLMNOP'/
  data isync/1,2,4,7,11,16,22,29,37,39,42,43,45,48,52,57,63,70,78,80,  &
              83,84,86,89/

  nargs=iargc()
  if(nargs.ne.10) then
     print*,'Usage:   sfoxsim   f0  DT Chan FoxC H1 H2 CQ FT nfiles snr'
     print*,'Example: sfoxsim  750 0.0  MM  K1JT  5  1  0  0   10   -15'
     print*,'         f0=0 to dither f0 and DT'
     print*,'         Chan Channel type AW LQ LM LD MQ MM MD HQ HM HD'
     print*,'         FoxC Fox callsign'
     print*,'         key'
     print*,'         H1 number of Hound calls with RR73'
     print*,'         H2 number of Hound calls with reports'
     print*,'         CQ=1 to include a CQ message'
     print*,'         FT=1 to include a Free Text message'
     go to 999
  endif
  call getarg(1,arg)
  read(arg,*) f0
  call getarg(2,arg)
  read(arg,*) xdt
  call getarg(3,channel)
  call getarg(4,foxcall)
  call getarg(5,arg)
  read(arg,*) nh1
  call getarg(6,arg)
  read(arg,*) nh2
  call getarg(7,arg)
  read(arg,*) ncq
  bMoreCQs=ncq.ne.0
  call getarg(8,arg)
  read(arg,*) nft
  bSendMsg=nft.ne.0
  call getarg(9,arg)
  read(arg,*) nfiles
  call getarg(10,arg)
  read(arg,*) snr

  fspread=0.0
  delay=0.0
  fsample=12000.0                   !Sample rate (Hz)  
  call sfox_init(7,127,50,channel,fspread,delay,fsample,24)
  txt=(NN+NS)*NSPS/fsample
  write(*,1000) f0,xdt,channel,snr
1000 format('sfoxsim: f0=  ',f5.1,'  dt= ',f4.2,'   Channel: ',a2,'   snr: ',f5.1,' dB')

! Allocate storage for arrays that depend on code parameters.
  allocate(s3(0:NQ-1,0:NN-1))
  allocate(msg0(1:KK))
  allocate(chansym(0:NN-1))

  if(nft.ne.0) then
     open(10,file='text_msg.txt',status='old',err=2)
     read(10,*) text_msg
  endif

2 idum=-1
  rms=100.
  baud=fsample/nsps                 !Keying rate, 11.719 baud for nsps=1024
  bandwidth_ratio=2500.0/fsample
  do i=1,5
     cmsg(i)=cmsg(i)(1:19)//trim(foxcall)//cmsg(i)(24:28)
     if(i.gt.nh1 .and. i.gt.nh2) then
        cmsg(i)=''
     elseif(i.gt.nh1) then
        cmsg(i)=cmsg(i)(13:18)//trim(foxcall)//cmsg(i)(25:28)
     elseif(i.gt.nh2) then
        cmsg(i)=cmsg(i)(1:6)//trim(foxcall)//' RR73'
     endif
!  write(*,*) 'Debug ',cmsg(i)
  enddo

  if((nh1+nh2).eq.0 .and. bMoreCQs) cmsg(1)='CQ '//trim(foxcall)//' FN20'

! Generate a SuperFox message
  nslots=5
  call foxgen2(nslots,cmsg,line,foxcall)      !Parse old-style Fox messages
  call sfox_pack(line,ckey,bMoreCQs,bSendMsg,text_msg,xin)
  call qpc_encode(y,xin)

  y=cshift(y,1)
  y(127)=0
  chansym=y(0:126)

  sig=sqrt(2*bandwidth_ratio)*10.0**(0.05*snr)
  sigr=sqrt(2.)*sig
  if(snr.gt.90.0) sig=1.0

  do ifile=1,nfiles
     xnoise=0.
     if(snr.lt.90) then
        do i=1,NMAX
           xnoise(i)=gran()                 !Gaussian noise
        enddo
     endif

     f1=f0
     if(f0.eq.0.0) then
        f1=750 + 20.0*(ran1(idum)-0.5)
        xdt=ran1(idum)-0.5
     endif
! Generate cdat, the SuperFox waveform
     call sfox_gen_gfsk(chansym,f1,isync,itone,cdat)

     crcvd=0.
     crcvd(1:NMAX)=cshift(cdat(1:NMAX),-nint((0.5+xdt)*fsample))
     if(fspread.ne.0 .or. delay.ne.0) call watterson(crcvd,NMAX,NZ,fsample,&
          delay,fspread)

     dat=aimag(sigr*crcvd(1:NMAX)) + xnoise    !Add generated AWGN noise
     fac=32767.0
     if(snr.ge.90.0) iwave(1:NMAX)=nint(fac*dat(1:NMAX))
     if(snr.lt.90.0) iwave(1:NMAX)=nint(rms*dat(1:NMAX))

     h=default_header(12000,NMAX)
     fname='000000_000001.wav'
     nsec=(ifile-1)*30
     nhr=nsec/3600
     nmin=(nsec-nhr*3600)/60
     nsec=mod(nsec,60)
     write(fname(8:13),'(3i2.2)') nhr,nmin,nsec
     open(10,file=trim(fname),access='stream',status='unknown')
     write(10) h,iwave(1:NMAX)                !Save the .wav file
     close(10)
  enddo  ! ifile

999 end program sfoxsim
