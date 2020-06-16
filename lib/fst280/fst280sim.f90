program fst280sim

! Generate simulated signals for experimental slow FT4  mode

   use wavhdr
   use packjt77
   include 'ft4s280_params.f90'               !Set various constants
   type(hdr) h                                !Header for .wav file
   character arg*12,fname*17
   character msg37*37,msgsent37*37,c77*77
   character tr_designator*1
   complex, allocatable :: c0(:)
   complex, allocatable :: c(:)
   real, allocatable :: wave(:)
   integer itone(NN)
   integer*1 msgbits(101)
   integer*2, allocatable :: iwave(:)        !Generated full-length waveform

! Get command-line argument(s)
   nargs=iargc()
   if(nargs.ne.9) then
      print*,'Usage:    fst280sim "message"         type      f0   DT   h  fdop  del nfiles snr'
      print*,'Examples: fst280sim "K1JT K9AN EN50"    C     1500  0.0  1.0  0.1  1.0   10   -15'
      print*,'A: 15 sec'
      print*,'B: 30 sec'
      print*,'C: 1 min'
      print*,'D: 2 min'
      print*,'E: 5 min'
      go to 999
   endif
   call getarg(1,msg37)                   !Message to be transmitted
   call getarg(2,arg)
   read(arg,*) tr_designator              !TR selector 
   call getarg(3,arg)
   read(arg,*) f0                         !Frequency (only used for single-signal)
   call getarg(4,arg)
   read(arg,*) xdt                        !Time offset from nominal (s)
   call getarg(5,arg)
   read(arg,*) hmod                       !Modulation index, h
   call getarg(6,arg)
   read(arg,*) fspread                    !Watterson frequency spread (Hz)
   call getarg(7,arg)
   read(arg,*) delay                      !Watterson delay (ms)
   call getarg(8,arg)
   read(arg,*) nfiles                     !Number of files
   call getarg(9,arg)
   read(arg,*) snrdb                      !SNR_2500

   nfiles=abs(nfiles)
   twopi=8.0*atan(1.0)
   fs=12000.0                             !Sample rate (Hz)
   dt=1.0/fs                              !Sample interval (s)
   baud=1.0/tt                            !Keying rate (baud)
   select case (tr_designator)
      case('A')
         nsps=800
         nmax=15*12000
      case('B')
         nsps=1680
         nmax=30*12000
      case('C')
         nsps=4000
         nmax=60*12000
      case('D')
         nsps=8400
         nmax=120*12000
      case('E')
         nsps=21504
         nmax=300*12000
   end select
   nz=nsps*NN
   nz2=nsps*NN2
   txt=nz2*dt                             !Transmission length (s)
   tt=nsps*dt                             !Duration of symbols (s)
   allocate( c0(0:nmax-1) )
   allocate( c(0:nmax-1) )
   allocate( wave(nmax) )
   allocate( iwave(nmax) )

   bandwidth_ratio=2500.0/(fs/2.0)
   sig=sqrt(2*bandwidth_ratio) * 10.0**(0.05*snrdb)
   if(snrdb.gt.90.0) sig=1.0

   i3=-1
   n3=-1
   call pack77(msg37,i3,n3,c77)
   call genfst280(msg37,0,msgsent37,msgbits,itone,iwspr)

   write(*,*)
   write(*,'(a9,a37)') 'Message: ',msgsent37
   write(*,1000) f0,xdt,hmod,txt,snrdb
1000 format('f0:',f9.3,'   DT:',f6.2,'   hmod:',f6.3,'   TxT:',f6.1,'   SNR:',f6.1)
   write(*,*)
   if(i3.eq.1) then
      write(*,*) '         mycall                         hiscall                    hisgrid'
      write(*,'(28i1,1x,i1,1x,28i1,1x,i1,1x,i1,1x,15i1,1x,3i1)') msgbits(1:77)
   else
      write(*,'(a14)') 'Message bits: '
      write(*,'(50i1,1x,24i1)') msgbits
   endif
   write(*,*)
   write(*,'(a17)') 'Channel symbols: '
   write(*,'(10i1)') itone
   write(*,*)

!   call sgran()

   fsample=12000.0
   icmplx=1
   call gen_fst280wave(itone,NN,nsps,nmax,fsample,hmod,f0,icmplx,c0,wave)
   k=nint((xdt+1.0)/dt)-nsps
   c0=cshift(c0,-k)
   if(k.gt.0) c0(0:k-1)=0.0
   if(k.lt.0) c0(nmax+k:nmax-1)=0.0

   do ifile=1,nfiles
      c=c0
      if(fspread.ne.0.0 .or. delay.ne.0.0) call watterson(c,nmax,NZ,fs,delay,fspread)
      c=sig*c
      wave=real(c)
      if(snrdb.lt.90) then
         do i=1,nmax                   !Add gaussian noise at specified SNR
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
      h=default_header(12000,nmax)
      write(fname,1102) ifile
1102  format('000000_',i6.6,'.wav')
      open(10,file=fname,status='unknown',access='stream')
      write(10) h,iwave                !Save to *.wav file
      close(10)
      write(*,1110) ifile,xdt,f0,snrdb,fname
1110  format(i4,f7.2,f8.2,f7.1,2x,a17)
   enddo

999 end program fst280sim
