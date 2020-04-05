program wsprcpmsim

! Generate simulated data for a 2-minute "WSPR-LF" mode.  Output is saved
! to a *.c2 or *.wav file.

   use wavhdr
   include 'wsprcpm_params.f90'            !Set various constants
   parameter (NMAX=120*12000)
   type(hdr) hwav                          !Header for .wav file
   character arg*12,fname*16
   character msg*22,msgsent*22
   complex c0(0:NMAX/NDOWN-1)
   complex c(0:NMAX/NDOWN-1)
   real*8 fMHz
   integer itone(NN)
   integer*2 iwave(NMAX)                  !Generated full-length waveform

! Get command-line argument(s)
   nargs=iargc()
   if(nargs.ne.9) then
      print*,'Usage:   wsprcpmsim "message"       f0  DT fsp del  nwav nfiles snr  h'
      print*,'Example: wsprcpmsim "K1ABC FN42 30" 50 1.0 0.1 1.0  1      10   -32  1.0'
      go to 999
   endif
   call getarg(1,msg)                     !Message to be transmitted
   call getarg(2,arg)
   read(arg,*) f0                         !Freq relative to WSPR-band center (Hz)
   call getarg(3,arg)
   read(arg,*) xdt                        !Time offset from nominal (s)
   call getarg(4,arg)
   read(arg,*) fspread                    !Watterson frequency spread (Hz)
   call getarg(5,arg)
   read(arg,*) delay                      !Watterson delay (ms)
   call getarg(6,arg)
   read(arg,*) nwav                       !1 for *.wav file, 0 for *.c2 file
   call getarg(7,arg)
   read(arg,*) nfiles                     !Number of files
   call getarg(8,arg)
   read(arg,*) snrdb                      !SNR_2500
   call getarg(9,arg)
   read(arg,*) h                          !h

   twopi=8.0*atan(1.0)
   fs=12000.0/NDOWN                       !
   dt=1.0/fs                              !Sample interval (s)
   tt=NSPS*dt                             !Duration of "itone" symbols (s)
   baud=1.0/tt                            !Keying rate for "itone" symbols (baud)
   txt=NZ*dt                              !Transmission length (s)
   bandwidth_ratio=2500.0/(fs/2.0)
   sig=sqrt(bandwidth_ratio) * 10.0**(0.05*snrdb)
   if(snrdb.gt.90.0) sig=1.0
   txt=NN*NSPS0/12000.0

   call genwsprcpm(msg,msgsent,itone)       !Encode the message, get itone
   write(*,1000) f0,xdt,txt,snrdb,fspread,delay,nfiles,msgsent
1000 format('f0:',f9.3,'   DT:',f6.2,'   txt:',f6.1,'   SNR:',f6.1,    &
      '   fspread:',f6.1,'   delay:',f6.1,'  nfiles:',i3,2x,a22)

   c0=0.
   k=-1 + nint(xdt/dt)
   do j=1,NN
write(*,*) j,itone(j)
      dp=twopi*(f0+itone(j)*(h/2.0)*baud)*dt
      do i=1,NSPS
         k=k+1
         phi=mod(phi+dp,twopi)
         if(k.ge.0 .and. k.lt.NMAX/NDOWN) c0(k)=cmplx(cos(phi),sin(phi))
      enddo
   enddo
   call sgran()
   do ifile=1,nfiles
      c=c0
      if(nwav.eq.0) then
         if( fspread .ne. 0.0 .or. delay .ne. 0.0 ) then
            ntot=NMAX/NDOWN
            nsig=NN*NSPS
            call watterson(c,ntot,nsig,fs,delay,fspread)
         endif
         c=c*sig
         if(snrdb.lt.90) then
            do i=0,NMAX/NDOWN-1                   !Add gaussian noise at specified SNR
               xnoise=gran()
               ynoise=gran()
               c(i)=c(i) + cmplx(xnoise,ynoise)
            enddo
         endif
         write(fname,1100) ifile
1100     format('000000_',i4.4,'.c2')
         open(10,file=fname,status='unknown',access='stream')
         fMHz=10.1387d0
         nmin=2
         write(10) fname,nmin,fMHz,c      !Save to *.c2 file
         close(10)
      else
         call wsprcpm_wav(baud,xdt,h,f0,itone,snrdb,iwave)
         hwav=default_header(12000,NMAX)
         write(fname,1102) ifile
1102     format('000000_',i4.4,'.wav')
         open(10,file=fname,status='unknown',access='stream')
         write(10) hwav,iwave                !Save to *.wav file
         close(10)
      endif
      write(*,1110) ifile,xdt,f0,snrdb,fname
1110  format(i4,f7.2,f8.2,f7.1,2x,a16)
   enddo

999 end program wsprcpmsim
