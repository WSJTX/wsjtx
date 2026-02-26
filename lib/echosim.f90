program echosim

! Generate simulated echo-mode files -- self-echo or "measure" 

  use wavhdr
  parameter (NWAVE=27648,NMAX=32768,NZ=36000)
  type(hdr) h                            !Header for .wav file
  character arg*12,fname*17
  complex c0(0:NMAX-1)
  complex c(0:NMAX-1)
  real*4 level_1,level_2
  real*8 f0,dt,twopi,phi,dphi
  real wave(NZ)
  integer*2 iwave(NZ)                  !Generated full-length waveform
  equivalence (nDop0,iwave(1))
  equivalence (nDopAudio0,iwave(3))
  equivalence (nfrit0,iwave(5))
  equivalence (f10,iwave(7))
  equivalence (fspread0,iwave(9))

! Get command-line argument(s)
  nargs=iargc()
  if(nargs.ne.3 .and. nargs.ne.5) then
     print*,'Usage 1:  echosim   f0   fdop fspread nfiles snr'
     print*,'Example:  echosim  1500   0.0   4.0     10   -22'
     print*,'Usage 2:  echosim level_1 level_2 nfiles'
     print*,'Example:  echosim   30.0    40.0   100'
     go to 999
  endif

  call getarg(1,arg)
  read(arg,*) f0                         !Tone frequency
  call getarg(2,arg)
  read(arg,*) fdop                       !Doppler shift (Hz)
  call getarg(3,arg)
  read(arg,*) fspread             !Frequency spread (Hz) (JHT Lorentzian model)

  if(nargs.eq.3) then
     level_1=f0
     level_2=fdop
     nfiles=fspread
     snrdb=0.
     go to 10
  endif
  
  call getarg(4,arg)
  read(arg,*) nfiles                     !Number of files
  call getarg(5,arg)
  read(arg,*) snrdb                      !SNR_2500

10 twopi=8.d0*atan(1.d0)
  fs=12000.0                             !Sample rate (Hz)
  dt=1.d0/fs                              !Sample interval (s)
  bandwidth_ratio=2500.0/(fs/2.0)
  sig=sqrt(2*bandwidth_ratio) * 10.0**(0.05*snrdb)
  if(snrdb.gt.90.0) sig=1.0
  dphi=twopi*(f0+fdop)*dt

  write(*,1000)
1000 format('   N   f0     fDop fSpread   SNR  File name'/51('-'))

  do ifile=1,nfiles
     wave=0.

     if(nargs.eq.5) then
        phi=0.d0
        do i=0,NWAVE-1
           phi=phi + dphi
           if(phi.gt.twopi) phi=phi-twopi
           xphi=phi
           c0(i)=cmplx(cos(xphi),sin(xphi))
        enddo
        c0(NWAVE:)=0.
        if(fspread.gt.0.0) call fspread_lorentz(c0,fspread)
        c=sig*c0
        wave(1:NWAVE)=imag(c(1:NWAVE))
        peak=maxval(abs(wave))
     endif

     if(snrdb.lt.90) then
        do i=1,NWAVE                   !Add gaussian noise at specified SNR
           xnoise=gran()
           wave(i)=wave(i) + xnoise
        enddo
        do i=NWAVE+1,NZ
           xnoise=gran()
           wave(i)=xnoise
        enddo
     endif

     gain=100.0
     if(nargs.eq.3) then
        gain=10.0**(0.05*level_1)
        if(mod((ifile-1)/10,2).eq.1) gain=10.0**(0.05*level_2)
     endif
     if(snrdb.lt.90.0) then
       wave=gain*wave
     else
       datpk=maxval(abs(wave))
       fac=32766.9/datpk
       wave=fac*wave
     endif
     if(any(abs(wave).gt.32767.0)) print*,"Warning - data will be clipped."
     iwave=nint(wave)

     nDop0=nint(fdop)
     nDopAudio0=0
     nfrit0=0
     f10=f0 + fdop
     fspread0=fspread
     
     h=default_header(12000,NMAX)
     n=3*(ifile-1)
     ihr=n/3600
     imin=(n-3600*ihr)/60
     isec=mod(n,60)
     write(fname,1102) ihr,imin,isec
1102 format('000000_',3i2.2,'.wav')
     open(10,file=fname,status='unknown',access='stream')
     write(10) h,iwave                !Save to *.wav file
     close(10)
     write(*,1110) ifile,f0,fdop,fspread,snrdb,fname
1110 format(i4,4f7.1,2x,a17)
  enddo

999 end program echosim
