program q65sim

! Generate simulated Q65 data for testing the decoder.

  use wavhdr
  use packjt
  parameter (NMAX=300*12000)             !Total samples in .wav file
  type(hdr) h                            !Header for .wav file
  integer*2 iwave(NMAX)                  !Generated waveform
  integer itone(85)                      !Channel symbols (values 0-65)
  integer y(63)                          !Codeword
  integer istart                         !averaging compatible start seconds
  integer imins                          !minutes for 15s period timestamp
  integer isecs                          !seconds for 15s period timestamp
  real*4 xnoise(NMAX)                    !Generated random noise
  real*4 dat(NMAX)                       !Generated real data
  complex cdat(NMAX)                     !Generated complex waveform
  complex cspread(0:NMAX-1)              !Complex amplitude for Rayleigh fading 
  complex z
  real*8 f0,dt,twopi,phi,dphi,baud,fsample,freq
  character msg*37,fname*17,csubmode*1,arg*12
  character msgsent*37
  
  nargs=iargc()
  if(nargs.ne.10) then
     print*,'Usage:   q65sim         "msg"     A-E freq fDop DT  f1 Stp TRp Nfile SNR'
     print*,'Example: q65sim "K1ABC W9XYZ EN37" A  1500 0.0 0.0 0.0  1   60   1   -26'
     print*,'Example: q65sim "ST" A  1500 0.0 0.0 0.0  1   60   1   -26'
     print*,'         fDop = Doppler spread'
     print*,'         f1   = Drift or Doppler rate (Hz/min)'
     print*,'         Stp  = Step size (Hz)'
     print*,'         Stp  = 0 implies no Doppler tracking'
     print*,'         Creates filenames which increment to permit averaging in first period'
     print*,'         If msg = ST program produces a single tone at freq'
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
  read(arg,*) f1
  call getarg(7,arg)
  read(arg,*) nstp
  call getarg(8,arg)
  read(arg,*) ntrperiod
  call getarg(9,arg)
  read(arg,*) nfiles
  call getarg(10,arg)
  read(arg,*) snrdb

  if(ntrperiod.eq.15) then
     nsps=1800
  else if(ntrperiod.eq.30) then
     nsps=3600
  else if(ntrperiod.eq.60) then
     nsps=7200
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

  ichk=0
  call genq65(msg,ichk,msgsent,itone,i3,n3)

  j=0
  do i=1,85
     if(itone(i).gt.0) then
        j=j+1
        y(j)=itone(i)-1
     endif
  enddo
  write(*,1001) y(1:13),y(1:13)
1001 format('Generated message'/'6-bit:  ',13i3/'binary: ',13b6.6)
  write(*,1002) y
1002 format(/'Codeword:'/(20i3))
  write(*,1003) itone
1003 format(/'Channel symbols:'/(20i3))

  baud=12000.d0/nsps                 !Keying rate (6.67 baud fot 15-s sequences)
  h=default_header(12000,npts)

  write(*,1004) 
1004 format('File    TR   Freq Mode  S/N   Dop     DT   f1  Stp  Message'/70('-'))

  do ifile=1,nfiles  !Loop over requested number of files
     istart = (ifile*ntrperiod*2) - (ntrperiod*2)
     if(ntrperiod.lt.30) then !wdg was 60
        imins=istart/60
        isecs=istart-(60*imins)
        write(fname,1005) imins,isecs        !Construction of output  filename for 15s periods with averaging
1005    format('000000_',i4.4, i2.2,'.wav')
     else
        write(fname,1106) istart/60     !Output filename to be compatible with averaging 30-300s periods
1106    format('000000_',i4.4,'.wav')
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
     write(*,1020) ifile,ntrperiod,f0,csubmode,snrdb,fspread,xdt,f1,nstp,trim(msgsent)
1020    format(i4,i6,f7.1,2x,a1,2x,f5.1,1x,f6.2,2f6.1,i4,2x,a)
     phi=0.d0
     dphi=0.d0
     k=(xdt+0.5)*12000                   !Start audio at t=xdt+0.5 s (TR=15 and 30 s)
     if(ntrperiod.ge.60) k=(xdt+1.0)*12000   !TR 60+ at t = xdt + 1.0 s
     isym0=-99
     do i=1,npts                         !Add this signal into cdat()
        isym=i/nsps + 1
        if(isym.gt.nsym) exit
        if(isym.ne.isym0) then
           freq_drift=f1*i*dt/60.0
           if(nstp.ne.0) freq_drift=freq_drift - nstp*nint(freq_drift/nstp)
                if (msg(1:2).eq.'ST') then
                   freq = f0 + freq_drift
                else
                   freq = f0 + freq_drift + itone(isym)*baud*mode65
                endif
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
  enddo

999 end program q65sim
