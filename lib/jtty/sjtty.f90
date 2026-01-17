program sjtty

  ! Simulate received data for JTTY, a mode operationally similar to RTTY
  ! but providing much better performance and reliability.

  ! Messages are source-encoded into 32-bit blocks. A 10-bit CRC is
  ! added to create a 42 bit payload, which is then FEC-encoded using
  ! a systematic (80,42) block code to create 80-bit codewords.

  ! Modulation is 4FSK at 12000/NSPS = 31.25 baud. Each transmitted frame
  ! consists of 13 sync symbols followed by 40 codeword symbols. 

  use wavhdr
  use jtty_mod
  use jtty_fec

  parameter (NMAX=30*12000)         !Max size of .wav file
  parameter (MAX_TONES=53*16)       !Max number of channel symbols
  character*12 arg                  !Command line argument
  character*2 arg4                  !The 4th command-line argument
  character*80 umsg                 !User-formatted message 
  character*40 fname                !Output file name
  character*10 flags                !Single-character shorthand flags
  character*32 c32(16)
  complex cwave(0:NMAX-1)           !Complex generated waveform (12000 Hz)
  complex c0(0:NMAX-1)              !With propagation degradation
  complex c(0:NMAX-1)               !With propagation degradation
  real wave (NMAX)                  !Real generated waveform (12000 Hz)
  type(hdr) h                       !Header for .wav file
  integer itone(MAX_TONES)          !Array of tone frequencies for this message
  integer*2 iwave(0:NMAX-1)         !Data written to the *.wav file
  integer*1 message32(32)
  integer*1 codeword80(80)
  integer graymap(0:3)
  integer ib13(13)
  logical itu_model                 !True if fdop, delay are from an ITU model
  data flags/'!@#$%^&*()'/
  data graymap/0,1,3,2/
  data ib13/0,0,0,0,0,3,3,0,0,3,0,3,0/
!  data ib13/0,0,0,0,0,1,1,0,0,1,0,1,0/
!  data ib13/-1,-1,-1,-1,-1,5,5,-1,-1,5,-1,5,-1/
  nargs=iargc()
  if(nargs.ne.7) then
     print*,'Usage:   sjtty    message     f0   DT fdop del nfiles SNR'
     print*,'Example: sjtty "CQ K1ABC CQ" 1500 0.0  0.5  1    10   -10'
     print*,'ITU propagation models: set fdop to AW LQ LM LD MQ MM MD HQ HM HD'
     go to 999
  endif

  call getarg(1,umsg)                    !User message
  call getarg(2,arg)
  read(arg,*) f0                         !Frequency of lowest tone
  call getarg(3,arg)
  read(arg,*) xdt                        !Time offset (positive only)
  call getarg(4,arg)
  itu_model=.true.
  arg4=arg(1:2)
  if(arg(1:2).eq.'LQ') then              !ITU params for Low Latitude Quiet
     fspread=0.5
     delay=0.5
  else if(arg(1:2).eq.'LM') then         !Low Latitude Moderate
     fspread=1.5
     delay=2.0
  else if(arg(1:2).eq.'LD') then         !Low Latitude Disturbed
     fspread=10.0
     delay=6.0
  else if(arg(1:2).eq.'MQ') then         !Mid Latitude ... etc.
     fspread=0.1
     delay=0.5
  else if(arg(1:2).eq.'MM') then
     fspread=0.5
     delay=1.0
  else if(arg(1:2).eq.'MD') then
     fspread=1.0
     delay=2.0
  else if(arg(1:2).eq.'HQ') then
     fspread=0.5
     delay=1.0
  else if(arg(1:2).eq.'HM') then
     fspread=10.0
     delay=3.0
  else if(arg(1:2).eq.'HD') then
     fspread=30.0
     delay=7.0
  else if(arg(1:2).eq.'AW') then
     fspread=0.0
     delay=0.0
  else
     itu_model=.false.
     read(arg,*) fspread                 !Watterson frequency spread (Hz)
     call getarg(5,arg)
     read(arg,*) delay                   !Watterson delay (ms)
  endif
  call getarg(6,arg)
  read(arg,*) nfiles                     !Number of files
  call getarg(7,arg)
  read(arg,*) snrdb                      !SNR in 2500 Hz bandwidth

  fsample=12000.0
  dt=1.0/fsample
  twopi=8.0*atan(1.0)
  bandwidth_ratio=2500.0/(fsample/2.0)
  sig=sqrt(2*bandwidth_ratio) * 10.0**(0.05*snrdb)
  if(snrdb.gt.90.0) sig=1.0
!  nsps=384                         !Samples per symbol at 12000 Hz
  bt=2.0                           !Default bt=2 (smaller ==> more smoothing)
  baud=fsample/nsps                !Symbol rate
  bw=4.0*baud                      !Signal bandwidth
  hmod=1.0                         !Modulation index

  call pack_jtty(umsg,c32,nframes)
  nsym=0
  do i=1,nframes
    read(c32(i),'(32i1)') message32(1:32) 
    call encode_80_32(message32,codeword80)
    ib=(i-1)*53+1   ! 53 tones per frame
    ie=ib+52       
    itone(ib:ib+12)=ib13
    do j = 1, 40
       is=codeword80(2*j) + 2*codeword80(2*j-1)
       itone(ib+12+j) = graymap(is)
    enddo
    nsym=nsym+53
  enddo

  txt=nsym*nsps*dt                            !Transmission length (s)
  numsg=len(trim(umsg))
  write(*,1012) trim(umsg)
1012 format('User message:  ',a)
  write(*,1013) txt,nsym,itone(1:nsym)
1013 format('Transmission length:',f5.1,' s,',i5,' channel symbols:'/  &
          (30i2))

  nwave=nsps*nsym                  !Length of i*2 data written to *.wav file
  icmplx=1
  call gen_jttywave(itone,nsym,nsps,bt,fsample,f0,cwave,wave,icmplx,nwave)


  write(*,1000) f0,xdt,txt,snrdb,bw
1000 format('f0:',f7.1,'   DT:',f6.2,'   TxT:',f6.1,'   SNR:',f6.1,'  BW:',f6.1)
  cps=baud/7.0
  cps_effective=numsg/txt
  write(*,1001) cps,cps_effective
1001 format('Raw character rate:'f5.1,' c/s   Effective character rate:',f5.1,' c/s')
  write(*,1002) fspread,delay
1002 format('Fspread:',f5.1,' Hz   Delay:',f5.1,' ms')
  if(itu_model) write(*,1003) arg4
1003 format('ITU propagation model: ',a2)
  write(*,*)  

!  call sgran()

  npts=2**(int(log(float(nwave)+xdt/dt)/log(2.0) + 0.9999))  !Round up to integer power of 2
  do ifile=1,nfiles
     c0=0.
     c0(0:nwave-1)=cwave(0:nwave-1)
     c0=cshift(c0,-nint(xdt/dt))
     if(fspread.ne.0.0 .or. delay.ne.0.0) then
        ! Apply channel propagation
        call watterson(c0,npts,nwave,fsample,delay,fspread)
     endif
     c=0.
     c=sig*c0      !Scale to specified SNR
     wave=0.
     wave=imag(c)    !Signal with SNR and prop degradation

     iz=nint(xdt/dt) + nwave + nsps*53              !Add one frame of noise at end
     if(snrdb.lt.90) then
        do i=1,iz                    !Add gaussian noise for specified SNR
           xnoise=gran()
           wave(i)=wave(i) + xnoise
        enddo
     endif

     gain=100.0
     if(snrdb.lt.90.0) then
       wave(1:iz)=gain*wave(1:iz)
     else
       datpk=maxval(abs(wave(1:iz)))
       fac=32766.9/datpk
       wave(1:iz)=fac*wave(1:iz)
     endif

     iwave(1:iz)=nint(wave(1:iz))
     h=default_header(12000,iz)
     write(fname,1102) ifile
1102 format('000000_',i6.6,'.wav')
     open(10,file=fname,status='unknown',access='stream')
     write(10) h,iwave(1:iz)                !Save to *.wav file
     close(10)
     write(*,1110) ifile,xdt,f0,snrdb,fname
1110 format(i4,f7.2,f8.2,f7.1,2x,a17)
  enddo

999 end program sjtty
