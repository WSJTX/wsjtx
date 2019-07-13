program ft4sim_mult

! Generate simulated signals for experimental "FT4" mode 

  use wavhdr
  use packjt77
  include 'ft4_params.f90'               !FT4 protocol constants
  parameter (NWAVE=NN*NSPS)
  parameter (NZZ=72576)                  !Length of .wav file (21*3456)
  type(hdr) h                            !Header for .wav file
  character arg*12,fname*17,cjunk*4
  character msg37*37,msgsent37*37,c77*77
  complex cwave0((NN+2)*NSPS)
  real wave0((NN+2)*NSPS)
  real wave(NZZ)
  real tmp(NZZ)
  integer itone(NN)
  integer*1 msgbits(77)
  integer*2 iwave(NZZ)                  !Generated full-length waveform
  
! Get command-line argument(s)
  nargs=iargc()
  if(nargs.ne.2) then
     print*,'Usage:    ft4sim_mult nsigs nfiles'
     print*,'Example:  ft4sim_mult  20     8 '
     go to 999
  endif
  call getarg(1,arg)
  read(arg,*) nsigs               !Number of signals
  call getarg(2,arg)
  read(arg,*) nfiles              !Number of files

  twopi=8.0*atan(1.0)
  fs=12000.0                      !Sample rate (Hz)
  dt=1.0/fs                       !Sample interval (s)
  hmod=1.0                        !Modulation index (0.5 is MSK, 1.0 is FSK)
  tt=NSPS*dt                      !Duration of unsmoothed symbols (s)
  baud=1.0/tt                     !Keying rate (baud)
  txt=NZ*dt                       !Transmission length (s) without ramp up/down
  bandwidth_ratio=2500.0/(fs/2.0)
  txt=NN*NSPS/12000.0
  open(10,file='messages.txt',status='old',err=998)

  do ifile=1,nfiles
1    read(10,1001,end=999) cjunk,n
1001 format(a4,i2)
     if(cjunk.ne.'File' .or. n.ne.ifile) go to 1
     wave=0.
     write(fname,1002) ifile
1002 format('000000_',i6.6,'.wav')
     
     do isig=1,nsigs
        read(10,1003,end=100) cjunk,isnr,xdt0,ifreq,msg37
1003    format(a4,30x,i3,f5.1,i5,1x,a37)
        if(cjunk.eq.'File') go to 100
        if(isnr.lt.-17) isnr=-17
        f0=ifreq*960.0/576.0
        call random_number(r)
        xdt=r-0.5
! Source-encode, then get itone()
        i3=-1
        n3=-1
        call pack77(msg37,i3,n3,c77)
        call genft4(msg37,0,msgsent37,msgbits,itone)
        nwave0=(NN+2)*NSPS
        icmplx=0
        call gen_ft4wave(itone,NN,NSPS,12000.0,f0,cwave0,wave0,icmplx,nwave0)

        k0=nint((xdt+0.5)/dt)
        if(k0.lt.1) k0=1
        tmp(:k0-1)=0.0
        tmp(k0:k0+nwave0-1)=wave0
        tmp(k0+nwave0:)=0.0

 ! Insert this signal into wave() array
        sig=sqrt(2*bandwidth_ratio) * 10.0**(0.05*isnr)
        wave=wave + sig*tmp
        write(*,1100) fname(1:13),isig,isnr,xdt,nint(f0),msg37
1100    format(a13,i4,i5,f5.1,i6,2x,a37)
     enddo   ! isig
   
100  backspace 10

     do i=1,NZZ                   !Add gaussian noise at specified SNR
        xnoise=gran()
        wave(i)=wave(i) + xnoise
     enddo

     gain=30.0
     wave=gain*wave
     if(any(abs(wave).gt.32767.0)) print*,"Warning - data will be clipped."
     iwave=nint(wave)
     h=default_header(12000,NZZ)
     open(12,file=fname,status='unknown',access='stream')
     write(12) h,iwave                !Save to *.wav file
     close(12)
     print*,' '
  enddo      ! ifile
  go to 999

998 print*,'Cannot open file "messages.txt"'

999 end program ft4sim_mult
