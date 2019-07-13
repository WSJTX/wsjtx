program ft2sim

! Generate simulated signals for experimental "FT2" mode 

  use wavhdr
  use packjt77
  include 'ft2_params.f90'               !Set various constants
  parameter (NWAVE=NN*NSPS)
  type(hdr) h                            !Header for .wav file
  character arg*12,fname*17
  character msg37*37,msgsent37*37
  character c77*77
  complex c0(0:NMAX-1)
  complex c(0:NMAX-1)
  real wave(NMAX)
  real dphi(0:NMAX-1)
  real pulse(480)               
  integer itone(NN)
  integer*1 msgbits(77)
  integer*2 iwave(NMAX)                  !Generated full-length waveform
  
! Get command-line argument(s)
  nargs=iargc()
  if(nargs.ne.8) then
     print*,'Usage:    ft2sim "message"                 f0     DT fdop del width nfiles snr'
     print*,'Examples: ft2sim "K1ABC W9XYZ EN37"       1500.0 0.0  0.1 1.0   0     10   -18'
     print*,'          ft2sim "WA9XYZ/R KA1ABC/R FN42" 1500.0 0.0  0.1 1.0   0     10   -18'
     print*,'          ft2sim "K1ABC RR73; W9XYZ <KH1/KH7Z> -11" 300 0 0 0 25 1 -10'
     go to 999
  endif
  call getarg(1,msg37)                   !Message to be transmitted
  call getarg(2,arg)
  read(arg,*) f0                         !Frequency (only used for single-signal)
  call getarg(3,arg)
  read(arg,*) xdt                        !Time offset from nominal (s)
  call getarg(4,arg)
  read(arg,*) fspread                    !Watterson frequency spread (Hz)
  call getarg(5,arg)
  read(arg,*) delay                      !Watterson delay (ms)
  call getarg(6,arg)
  read(arg,*) width                      !Filter transition width (Hz)
  call getarg(7,arg)
  read(arg,*) nfiles                     !Number of files
  call getarg(8,arg)
  read(arg,*) snrdb                      !SNR_2500

  nfiles=abs(nfiles)
  twopi=8.0*atan(1.0)
  fs=12000.0                             !Sample rate (Hz)
  dt=1.0/fs                              !Sample interval (s)
  hmod=0.800                               !Modulation index (0.5 is MSK, 1.0 is FSK)
  tt=NSPS*dt                             !Duration of symbols (s)
  baud=1.0/tt                            !Keying rate (baud)
  txt=NZ*dt                              !Transmission length (s)

  bandwidth_ratio=2500.0/(fs/2.0)
  sig=sqrt(2*bandwidth_ratio) * 10.0**(0.05*snrdb)
  if(snrdb.gt.90.0) sig=1.0
  txt=NN*NSPS/12000.0

  ! Source-encode, then get itone()
  i3=-1
  n3=-1
  call pack77(msg37,i3,n3,c77)
  read(c77,'(77i1)') msgbits
  call genft2(msg37,0,msgsent37,itone,itype)
  write(*,*)  
  write(*,'(a9,a37,3x,a7,i1,a1,i1)') 'Message: ',msgsent37,'i3.n3: ',i3,'.',n3
  write(*,1000) f0,xdt,txt,snrdb
1000 format('f0:',f9.3,'   DT:',f6.2,'   TxT:',f6.1,'   SNR:',f6.1)
  write(*,*)  
  if(i3.eq.1) then
    write(*,*) '         mycall                         hiscall                    hisgrid'
    write(*,'(28i1,1x,i1,1x,28i1,1x,i1,1x,i1,1x,15i1,1x,3i1)') msgbits(1:77) 
  else
    write(*,'(a14)') 'Message bits: '
    write(*,'(77i1)') msgbits
  endif
  write(*,*) 
  write(*,'(a17)') 'Channel symbols: '
  write(*,'(79i1)') itone
  write(*,*)  

  call sgran()

! The filtered frequency pulse 
  do i=1,480
     tt=(i-240.5)/160.0
     pulse(i)=gfsk_pulse(1.0,tt)
  enddo

! Define the instantaneous frequency waveform
  dphi_peak=twopi*(hmod/2.0)/real(NSPS)
  dphi=0.0 
  do j=1,NN         
     ib=(j-1)*160
     ie=ib+480-1
     dphi(ib:ie)=dphi(ib:ie)+dphi_peak*pulse*(2*itone(j)-1)
  enddo

  phi=0.0
  c0=0.0
  dphi=dphi+twopi*f0*dt
  do j=0,NMAX-1
     c0(j)=cmplx(cos(phi),sin(phi))
     phi=mod(phi+dphi(j),twopi)
  enddo 
 
  c0(0:159)=c0(0:159)*(1.0-cos(twopi*(/(i,i=0,159)/)/320.0) )/2.0
  c0(145*160:145*160+159)=c0(145*160:145*160+159)*(1.0+cos(twopi*(/(i,i=0,159)/)/320.0 ))/2.0
  c0(146*160:)=0.

  k=nint((xdt+0.25)/dt)
  c0=cshift(c0,-k)
  ia=k
  
  do ifile=1,nfiles
     c=c0
     if(fspread.ne.0.0 .or. delay.ne.0.0) call watterson(c,NMAX,NWAVE,fs,delay,fspread)
     c=sig*c
  
     ib=k
     wave=real(c)
     peak=maxval(abs(wave(ia:ib)))
     nslots=1
     if(width.gt.0.0) call filt8(f0,nslots,width,wave)
   
     if(snrdb.lt.90) then
        do i=1,NMAX                   !Add gaussian noise at specified SNR
           xnoise=gran()
           wave(i)=wave(i) + xnoise
        enddo
     endif

     gain=100.0
     if(snrdb.lt.90.0) then
       wave=gain*wave
     else
       datpk=maxval(abs(wave))
       fac=32766.9/datpk
       wave=fac*wave
     endif
     if(any(abs(wave).gt.32767.0)) print*,"Warning - data will be clipped."
     iwave=nint(wave)
     h=default_header(12000,NMAX)
     write(fname,1102) ifile
1102 format('000000_',i6.6,'.wav')
     open(10,file=fname,status='unknown',access='stream')
     write(10) h,iwave                !Save to *.wav file
     close(10)
     write(*,1110) ifile,xdt,f0,snrdb,fname
1110 format(i4,f7.2,f8.2,f7.1,2x,a17)
  enddo    
999 end program ft2sim
