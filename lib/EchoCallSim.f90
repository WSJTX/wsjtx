program EchoCallSim

! Generate simulated transmissions for echo_call mode.

  use wavhdr
  parameter (NSPS=4096,NZ=3*12000)
  parameter (NMAX=6*NSPS)                !Samples in .wav file, 2.2*12000
  parameter (DFTONE=10.0)                !Tone spacing, Hz)
  type(hdr) h                            !Header for .wav file
  integer*2 iwave(NZ)                    !Generated waveform
  integer itone(6)                       !Channel symbols, values 0-37
  real*4 xnoise(NMAX)                    !Generated random noise
  real*4 dat(NMAX)                       !Generated real data
  complex cdat(NMAX)                     !Generated complex waveform
  complex z
  real*8 f0,dt,twopi,phi,dphi,fsample,freq
  character callsign*6,fname*17,arg*8
!  character*37 c

  equivalence (nDop0,iwave(1))
  equivalence (nDopAudio0,iwave(3))
  equivalence (nfrit0,iwave(5))
  equivalence (f10,iwave(7))
  equivalence (fspread0,iwave(9))
  
!  data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
  
  nargs=iargc()
  if(nargs.ne.6) then
     print*,'Usage:   EchoCallSim callsign f0 fdop fspread nfiles snrdb'
     print*,'Example: EchoCallSim   K1JT  1500 0.0  10.0     10    -15'
     go to 999
  endif
  call getarg(1,callsign)
  call getarg(2,arg)
  read(arg,*) f0
  call getarg(3,arg)
  read(arg,*) fdop
  call getarg(4,arg)
  read(arg,*) fspread
  call getarg(5,arg)
  read(arg,*) nfiles
  call getarg(6,arg)
  read(arg,*) snrdb

  write(*,1000)
1000 format('   N   f0     fDop fSpread  SNR   File name'/58('-'))

  call gen_echocall(callsign,itone)

  twopi=8.d0*atan(1.d0)
  rms=100.
  fsample=12000.d0                   !Sample rate (Hz)
  npts=NMAX                          !Total samples in .wav file
  nfft=npts
  nh=nfft/2
  dt=1.d0/fsample                    !Sample interval (s)
  df=12000.0/NSPS
  
  do ifile=1,nfiles  !Loop over requested number of files
     xnoise=0.
     if(snrdb.lt.90) then
        do i=1,npts
           xnoise(i)=gran()          !Generate gaussian noise
        enddo
     endif

     bandwidth_ratio=2500.0/6000.0
     sig=sqrt(2*bandwidth_ratio)*10.0**(0.05*snrdb)
     if(snrdb.gt.90.0) sig=1.0

     phi=0.d0
     dphi=0.d0
     k=0
     do j=1,6
        freq=f0 + fdop + itone(j)*DFTONE
        dphi=twopi*freq*dt
        do i=1,NSPS
           phi=phi + dphi
           if(phi.gt.twopi) phi=phi-twopi
           xphi=phi
           z=cmplx(cos(xphi),sin(xphi))
           k=k+1
           cdat(k)=sig*z
        enddo
     enddo

     if(fspread.gt.0.0) call fspread_lorentz(cdat,fspread)

     dat=aimag(cdat) + xnoise                 !Add generated AWGN noise
     fac=32767.0
     if(snrdb.ge.90.0) iwave(1:npts)=nint(fac*dat(1:npts))
     if(snrdb.lt.90.0) iwave(1:npts)=nint(rms*dat(1:npts))
     iwave(npts+1:)=0

     nDop0=nint(fdop)
     nDopAudio0=0
     nfrit0=0
     f10=f0 + fdop
     fspread0=fspread

     h=default_header(12000,NZ)
     n=6*(ifile-1)
     ihr=n/3600
     imin=(n-3600*ihr)/60
     isec=mod(n,60)
     write(fname,1102) ihr,imin,isec
1102 format('000000_',3i2.2,'.wav')
     open(10,file=trim(fname),access='stream',status='unknown')
     write(10) h,iwave                        !Save the .wav file
     close(10)

     write(*,1110) ifile,f0,fdop,fspread,snrdb,fname
1110 format(i4,4f7.1,2x,a17)

  enddo

999 end program EchoCallSim


