program qra65sim

! Generate simulated QRA65 data for testing the decoder.

  use wavhdr
  use packjt
  parameter (NMAX=300*12000)             !Total samples in .wav file
  type(hdr) h                            !Header for .wav file
  integer*2 iwave(NMAX)                  !Generated waveform
  integer*4 itone(85)                    !Channel symbols (values 0-65)
  real*4 xnoise(NMAX)                    !Generated random noise
  real*4 dat(NMAX)                       !Generated real data
  complex cdat(NMAX)                     !Generated complex waveform
  complex cspread(0:NMAX-1)              !Complex amplitude for Rayleigh fading
  complex z
  real*8 f0,dt,twopi,phi,dphi,baud,fsample,freq
  character msg*22,fname*17,csubmode*1,arg*12,cd*1
  character msgsent*22
  logical lsync
  data lsync/.false./
  
  nargs=iargc()
  if(nargs.ne.9) then
     print *, 'Usage:   qra65sim         "msg"     A-E freq fDop DT TRp Nfiles Sync SNR'
     print *, 'Example: qra65sim "K1ABC W9XYZ EN37" A  1500 0.0 0.0  60   1      T  -26'
     print*,'Sync = T to include sync test.'
     go to 999
  endif
  call getarg(1,msg)
  call getarg(2,csubmode)
  mode65=2**(ichar(csubmode)-ichar('A'))
  call getarg(3,arg)
  read(arg,*) f0
  call getarg(4,arg)
  read(arg,*) fspread
  call getarg(5,arg)
  read(arg,*) xdt
  call getarg(6,arg)
  read(arg,*) ntrperiod
  call getarg(7,arg)
  read(arg,*) nfiles
  call getarg(8,arg)
  if(arg(1:1).eq.'T' .or. arg(1:1).eq.'1') lsync=.true.
  call getarg(9,arg)
  read(arg,*) snrdb

  if(nfiles.lt.0) then
     nfiles=-nfiles
     lsync=.true.
  endif

  if(ntrperiod.eq.15) then
     nsps=1800
  else if(ntrperiod.eq.30) then
     nsps=3600
  else if(ntrperiod.eq.60) then
     nsps=7680
  else if(ntrperiod.eq.120) then
     nsps=16000
  else if(ntrperiod.eq.300) then
     nsps=41472
  else
     print*,'Invalid TR period'
     go to 999
  endif

  rms=100.
  fsample=12000.d0                   !Sample rate (Hz)
  npts=fsample*ntrperiod             !Total samples in .wav file
  nfft=npts
  nh=nfft/2
  dt=1.d0/fsample                    !Sample interval (s)
  twopi=8.d0*atan(1.d0)
  nsym=85                            !Number of channel symbols
  mode65=2**(ichar(csubmode) - ichar('A'))

  ichk=65                            !Flag sent to genqra64
  call genqra64(msg,ichk,msgsent,itone,itype)
  write(*,1001) itone
1001 format('Channel symbols:'/(20i3))

  baud=12000.d0/nsps                 !Keying rate (6.67 baud fot 15-s sequences)
  h=default_header(12000,npts)

  write(*,1000) 
1000 format('File    TR   Freq Mode  S/N   DT    Dop  Message'/60('-'))

  nsync=0
  do ifile=1,nfiles                  !Loop over requested number of files
     if(ntrperiod.lt.60) then
        write(fname,1002) ifile         !Output filename
1002    format('000000_',i6.6,'.wav')
     else
        write(fname,1104) ifile
1104    format('000000_',i4.4,'.wav')
     endif

     open(10,file=trim(fname),access='stream',status='unknown')
     xnoise=0.
     cdat=0.
     if(snrdb.lt.90) then
        do i=1,npts
           xnoise(i)=gran()          !Generate gaussian noise
        enddo
     endif

     bandwidth_ratio=2500.0/6000.0
     sig=sqrt(2*bandwidth_ratio)*10.0**(0.05*snrdb)
     if(snrdb.gt.90.0) sig=1.0
     write(*,1020) ifile,ntrperiod,f0,csubmode,snrdb,xdt,fspread,msgsent
1020    format(i4,i6,f7.1,2x,a1,2x,f5.1,f6.2,f6.1,2x,a22)
     phi=0.d0
     dphi=0.d0
     k=(xdt+0.5)*12000                   !Start audio at t=xdt+0.5 s (TR=15 and 30 s)
     if(ntrperiod.ge.60) k=(xdt+1.0)*12000   !TR 60+ at t = xdt + 1.0 s
     isym0=-99
     do i=1,npts                         !Add this signal into cdat()
        isym=i/nsps + 1
        if(isym.gt.nsym) exit
        if(isym.ne.isym0) then
           freq=f0 + itone(isym)*baud*mode65
           dphi=twopi*freq*dt
           isym0=isym
        endif
        phi=phi + dphi
        if(phi.gt.twopi) phi=phi-twopi
        xphi=phi
        z=cmplx(cos(xphi),sin(xphi))
        k=k+1
        if(k.ge.1) cdat(k)=cdat(k) + sig*z
     enddo
     
     if(fspread.ne.0) then                  !Apply specified Doppler spread
        df=12000.0/nfft
        cspread(0)=1.0
        cspread(nh)=0.
        b=6.0                       !Use truncated Lorenzian shape for fspread
        do i=1,nh
           f=i*df
           x=b*f/fspread
           z=0.
           a=0.
           if(x.lt.3.0) then                          !Cutoff beyond x=3
              a=sqrt(1.111/(1.0+x*x)-0.1)             !Lorentzian amplitude
              phi1=twopi*rran()                       !Random phase
              z=a*cmplx(cos(phi1),sin(phi1))
           endif
           cspread(i)=z
           z=0.
           if(x.lt.3.0) then                !Same thing for negative freqs
              phi2=twopi*rran()
              z=a*cmplx(cos(phi2),sin(phi2))
           endif
           cspread(nfft-i)=z
        enddo

        call four2a(cspread,nfft,1,1,1)             !Transform to time domain

        sum=0.
        do i=0,nfft-1
           p=real(cspread(i))**2 + aimag(cspread(i))**2
           sum=sum+p
        enddo
        avep=sum/nfft
        fac=sqrt(1.0/avep)
        cspread=fac*cspread                   !Normalize to constant avg power
        cdat=cspread*cdat                     !Apply Rayleigh fading

!        do i=0,nfft-1
!           p=real(cspread(i))**2 + aimag(cspread(i))**2
!           write(14,3010) i,p,cspread(i)
!3010       format(i8,3f12.6)
!        enddo
     endif

     dat=aimag(cdat) + xnoise                 !Add generated AWGN noise
     fac=32767.0
     if(snrdb.ge.90.0) iwave(1:npts)=nint(fac*dat(1:npts))
     if(snrdb.lt.90.0) iwave(1:npts)=nint(rms*dat(1:npts))
     write(10) h,iwave(1:npts)                !Save the .wav file
     close(10)

     if(lsync) then
        cd=' '
        if(ifile.eq.nfiles) cd='d'
        nfqso=nint(f0)
        ntol=100
        call sync_qra65(iwave,npts,mode65,nsps,nfqso,ntol,xdt2,f02,snr2)
        terr=1.01/(8.0*baud)
        ferr=1.01*mode65*baud
        if(abs(xdt2-xdt).lt.terr .and. abs(f02-f0).lt.ferr) nsync=nsync+1
        open(40,file='sync65.out',status='unknown',position='append')
        write(40,1030) ifile,65,csubmode,snrdb,fspread,xdt2-xdt,f02-f0,   &
             snr2,nsync,cd
1030    format(i4,i3,1x,a1,2f7.1,f7.2,2f8.1,i5,1x,a1)
        close(40)
     endif
  enddo
  if(lsync) write(*,1040) snrdb,nfiles,nsync
1040 format('SNR:',f6.1,'   nfiles:',i5,'   nsynced:',i5)

999 end program qra65sim
