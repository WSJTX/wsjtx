program qra66sim

! Generate simulated QRA66 data for testing the decoder.

  use wavhdr
  use packjt
  parameter (NMAX=15*12000)              !180,000
  parameter (NFFT=NMAX,NH=NFFT/2)
  type(hdr) h                            !Header for .wav file
  integer*2 iwave(NMAX)                  !Generated waveform
  integer*4 itone(85)                    !Channel symbols (values 0-65)
  real*4 xnoise(NMAX)                    !Generated random noise
  real*4 dat(NMAX)                       !Generated real data
  complex cdat(NMAX)                     !Generated complex waveform
  complex cspread(0:NFFT-1)              !Complex amplitude for Rayleigh fading
  complex z
  real*8 f0,dt,twopi,phi,dphi,baud,fsample,freq
  character msg*22,fname*13,csubmode*1,arg*12
  character msgsent*22

  nargs=iargc()
  if(nargs.ne.6) then
     print *, 'Usage:   qra66sim         "msg"     A|B fDop DT Nfiles SNR'
     print *, 'Example  qra66sim "K1ABC W9XYZ EN37" A  0.2 0.0   1    -10'
     go to 999
  endif
  call getarg(1,msg)
  call getarg(2,csubmode)
  mode66=2**(ichar(csubmode)-ichar('A'))
  call getarg(3,arg)
  read(arg,*) fspread
  call getarg(4,arg)
  read(arg,*) xdt
  call getarg(5,arg)
  read(arg,*) nfiles
  call getarg(6,arg)
  read(arg,*) snrdb
  
  rms=100.
  fsample=12000.d0                   !Sample rate (Hz)
  dt=1.d0/fsample                    !Sample interval (s)
  twopi=8.d0*atan(1.d0)
  npts=NMAX                          !Total samples in .wav file
  nsps=1920
  nsym=85                            !Number of channel symbols
  if(csubmode.eq.'B') then
     nsps=960
     nsym=169
  endif

  ichk=66                            !Flag sent to genqra64
  call genqra64(msg,ichk,msgsent,itone,itype)
  write(*,1001) itone
1001 format('Channel symbols:'/(20i3))

  baud=12000.d0/nsps                 !Keying rate = 6.25 baud
  h=default_header(12000,npts)

  write(*,1000) 
1000 format('File     Freq  A|B   S/N   DT    Dop  Message'/60('-'))

  do ifile=1,nfiles                  !Loop over requested number of files
     write(fname,1002) ifile         !Output filename
1002 format('000000_',i6.6)
     open(10,file=fname//'.wav',access='stream',status='unknown')
     xnoise=0.
     cdat=0.
     if(snrdb.lt.90) then
        do i=1,npts
           xnoise(i)=gran()          !Generate gaussian noise
        enddo
     endif

     f0=1500.0
     bandwidth_ratio=2500.0/6000.0
     sig=sqrt(2*bandwidth_ratio)*10.0**(0.05*snrdb)
     if(snrdb.gt.90.0) sig=1.0
     write(*,1020) ifile,f0,csubmode,xsnr,xdt,fspread,msgsent
1020    format(i4,f10.3,2x,a1,2x,f5.1,f6.2,f6.1,1x,a22)
     phi=0.d0
     dphi=0.d0
     k=(xdt+0.5)*12000                   !Start audio at t = xdt + 0.5 s
     isym0=-99
     do i=1,npts                         !Add this signal into cdat()
        isym=i/nsps + 1
        if(isym.gt.nsym) exit
        if(csubmode.eq.'B' .and. isym.gt.84) isym=isym-84
        if(isym.ne.isym0) then
           freq=f0 + itone(isym)*baud
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
        cspread(NH)=0.
        b=6.0                       !Use truncated Lorenzian shape for fspread
        do i=1,NH
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
           cspread(NFFT-i)=z
        enddo

!        do i=0,NFFT-1
!           f=i*df
!           if(i.gt.NH) f=(i-nfft)*df
!           s=real(cspread(i))**2 + aimag(cspread(i))**2
!          write(13,3000) i,f,s,cspread(i)
!3000      format(i5,f10.3,3f12.6)
!        enddo
!        s=real(cspread(0))**2 + aimag(cspread(0))**2
!        write(13,3000) 1024,0.0,s,cspread(0)

        call four2a(cspread,NFFT,1,1,1)             !Transform to time domain

        sum=0.
        do i=0,NFFT-1
           p=real(cspread(i))**2 + aimag(cspread(i))**2
           sum=sum+p
        enddo
        avep=sum/NFFT
        fac=sqrt(1.0/avep)
        cspread=fac*cspread                   !Normalize to constant avg power
        cdat=cspread*cdat                     !Apply Rayleigh fading

!        do i=0,NFFT-1
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

!     do i=1,NMAX
!        write(15,3020) i/12000.0,iwave(i)
!3020    format(f10.6,i8)
!     enddo
  enddo

999 end program qra66sim
