program jt49sim

! Generate simulated data for testing JT4 and JT9
! April 26, 2020: changed to Watterson channel simulator (K9AN)
!
  use wavhdr
  use packjt
  use jt4
  parameter (NMAX=60*12000)              ! = 648,000
  parameter (NFFT=10*65536,NH=NFFT/2)
  type(hdr) h                            !Header for .wav file
  integer*2 iwave(NMAX)                  !Generated waveform
  integer*4 itone(206)                   !Channel symbols (values 0-8)
  real*4 xnoise(NMAX)                    !Generated random noise
  real*4 dat(NMAX)                       !Generated real data
  complex cdat(NMAX)                     !Generated complex waveform
  complex c0(NMAX)                   !Waveform multipled by fading realization
  complex z
  real*8 f0,dt,twopi,phi,dphi,baud,fsample,freq,dnsps
  character message*22,fname*11,csubmode*2,arg*12
  character msgsent*22
  
  nargs=iargc()
  if(nargs.ne. 8) then
     print *, 'Usage:   jt49sim        "msg"     nA-nE Nsigs fDop delay DT Nfiles SNR'
     print *, 'Example: jt49sim "K1ABC W9XYZ EN37" 4G   10   0.2   1.0 0.0   1     0'
     print *, 'Example: jt49sim "K1ABC W9XYZ EN37" 9A    1   0.0   0.0 0.0   1    -20'
     print *, 'Use msg=@nnnn to generate a tone at nnnn Hz:'
     print *, 'Example: jt49sim "@1500"            9A    1  10.0   0.0 0.0   1    -20'
     print *, 'If Nsigs > 100, generate one signal with f0=Nsigs'
     print *, 'Example: jt49sim "K1ABC W9XYZ EN37" 4F  1800  0.2   0.0 0.0   1    -20'
     go to 999
  endif
  call getarg(1,message)
  call fmtmsg(message, iz)
  call getarg(2,csubmode)
  imode=ichar(csubmode(1:1)) - ichar('0')
  nsubmode=ichar(csubmode(2:2)) - ichar('A')
  if(imode.ne.4 .and. imode.ne.9) go to 999
  if(nsubmode.lt.0 .or. nsubmode.gt.7) go to 999
  call getarg(3,arg)
  read(arg,*) nsigs
  call getarg(4,arg)
  read(arg,*) fspread
  call getarg(5,arg)
  read(arg,*) delay 
  call getarg(6,arg)
  read(arg,*) xdt
  call getarg(7,arg)
  read(arg,*) nfiles
  call getarg(8,arg)
  read(arg,*) snrdb

  rms=100.
  fsample=12000.d0                   !Sample rate (Hz)
  dt=1.d0/fsample                    !Sample interval (s)
  twopi=8.d0*atan(1.d0)
  h=default_header(12000,NMAX)
  dfsig=2000.0/nsigs                 !Freq spacing between sigs in file (Hz)
  ichk=0
  nsym=0
  dnsps=0.
  baud=0.
  sig=0.

  if(imode.eq.4) then
     nsym=206                           !Number of channel symbols (JT4)
     dnsps=12000.d0/4.375d0
     baud=12000.d0/dnsps                !Keying rate = 1.7361111111
  else if(imode.eq.9) then
     nsym=85                            !Number of channel symbols (JT9)
     dnsps=6912.d0                      !Samples per symbol
     baud=12000.d0/dnsps                !Keying rate = 1.736...
  endif
  NZ=nsym*dnsps
 
  write(*,1000) 
1000 format('File  Sig    Freq  Mode   S/N   DT   fDop   delay    Message'/60('-'))

  do ifile=1,nfiles                  !Loop over requested number of files
     write(fname,1002) ifile         !Output filename
1002 format('000000_',i4.4)
     open(10,file=fname//'.wav',access='stream',status='unknown')
     xnoise=0.
     cdat=0.
     if(snrdb.lt.90) then
        do i=1,NMAX
           xnoise(i)=gran()          !Generate gaussian noise
        enddo
     endif

     do isig=1,nsigs                 !Generate requested number of sigs
        if(mod(nsigs,2).eq.0) f0=1500.0 + dfsig*(isig-0.5-nsigs/2)
        if(mod(nsigs,2).eq.1) f0=1500.0 + dfsig*(isig-(nsigs+1)/2)
        if(nsigs.eq.1) f0=1500.0
        if(nsigs.gt.100) f0=nsigs
        xsnr=snrdb
        if(snrdb.eq.0.0) xsnr=-20 - isig

        if(imode.eq.4) call gen4(message,ichk,msgsent,itone,itype)
        if(imode.eq.9) call gen9(message,ichk,msgsent,itone,itype)

        bandwidth_ratio=2500.0/6000.0
        sig=sqrt(2*bandwidth_ratio)*10.0**(0.05*xsnr)
        if(xsnr.gt.90.0) sig=1.0
        write(*,1020) ifile,isig,f0,csubmode,xsnr,xdt,fspread,delay,message
1020    format(i4,i4,f10.3,2x,a2,2x,f5.1,f6.2,f6.1,1x,f6.1,2x,a22)

        phi=0.d0
        dphi=0.d0
        k=(xdt+1.0)*12000                   !Start audio at t = xdt + 1.0 s
        isym0=-99        
        do i=1,NMAX                         !Add this signal into cdat()
           isym=i/dnsps + 1
           if(isym.gt.nsym) exit
           if(isym.ne.isym0) then
              if(message(1:1).eq.'@') then
                 read(message(2:),*) freq
              else 
                 if(imode.eq.4) freq=f0 + itone(isym)*baud*nch(1+nsubmode) !JT4
                 if(imode.eq.9) freq=f0 + itone(isym)*baud*(2**nsubmode)   !JT9
              endif
              dphi=twopi*freq*dt
              isym0=isym
           endif
           phi=phi + dphi
           if(phi.gt.twopi) phi=phi-twopi
           xphi=phi
           z=cmplx(cos(xphi),sin(xphi))
           k=k+1
           if(k.ge.1) cdat(k)=cdat(k) + z
        enddo
        if(nsigs.gt.100) exit
     enddo

     c0=cdat 
     if(fspread.ne.0 .or. delay.ne.0) then                  !Apply specified Doppler spread
        fs=12000.0
        call watterson(c0,NMAX,NZ,fs,delay,fspread) 
     endif

     dat=sig*aimag(c0) + xnoise                 !Add the generated noise
     fac=32767.0/nsigs
     if(snrdb.ge.90.0) iwave(1:NMAX)=nint(fac*dat(1:NMAX))
     if(snrdb.lt.90.0) iwave(1:NMAX)=nint(rms*dat(1:NMAX))
     write(10) h,iwave(1:NMAX)                !Save the .wav file
     close(10)
  enddo

999 end program jt49sim
