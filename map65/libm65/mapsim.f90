program mapsim

! Generate simulated data for testing of MAP65

  parameter (NMAX=60*96000)
  real*4 d4(4,NMAX)                   !Floating-point data
  integer*2 id4(4,NMAX)               !i*2 data, dual polarization
  integer*2 id2(2,NMAX)               !i*2 data, single polarization
  complex cwave(NMAX)                 !Generated complex waveform (no noise)
  complex z,zx,zy
  real*8 fcenter,fsample,samfac,f,dt,twopi,phi,dphi
  character msg0*22,message*22,msgsent*22,arg*8,fname*13,mode*2
  logical bq65

  nargs=iargc()
  if(nargs.ne.10) then
     print*,'Usage:   mapsim "message"     mode DT  fa fb nsigs pol fDop SNR nfiles'
     print*,'Example: mapsim "CQ K1ABC FN42" B 2.5 -20 20  21    45  0.0 -20   1'
     print*,' '
     print*,'         mode = A B C for JT65; QA-QE for Q65-60A' 
     print*,'         fa = lowest freq in kHz, relative to center'
     print*,'         fb = highest freq in kHz, relative to center'
     print*,'         message = "" to use entries in msgs.txt.'
     print*,'         pol = -1 to generate a range of polarization angles.'
     print*,'         SNR = 0 to generate a range of SNRs.'
     go to 999
  endif

  call getarg(1,msg0)
  message=msg0                       !Transmitted message
  call getarg(2,mode)                !JT65 sub-mode (A B C QA-QE)
  call getarg(3,arg)
  read(arg,*) dt0                    !Time delay
  call getarg(4,arg)
  read(arg,*) fa                     !Lowest freq (kHz, relative to fcenter)
  call getarg(5,arg)
  read(arg,*) fb                     !Highest freq
  call getarg(6,arg)
  read(arg,*) nsigs                  !Number of signals in each file
  call getarg(7,arg)
  read(arg,*) npol                   !Polarization in degrees
  pol=npol
  call getarg(8,arg)
  read(arg,*) fdop                   !Doppler spread
  call getarg(9,arg)
  read(arg,*) snrdb                  !S/N
  call getarg(10,arg)
  read(arg,*) nfiles                 !Number of files

  rmsdb=25.
  rms=10.0**(0.05*rmsdb)
  fcenter=144.125d0                  !Center frequency (MHz)
  fsample=96000.d0                   !Sample rate (Hz)
  dt=1.d0/fsample                    !Sample interval (s)
  twopi=8.d0*atan(1.d0)
  rad=360.0/twopi
  samfac=1.d0
  bq65=(mode(1:1).eq.'Q')
  ntone_spacing=1
  ntxfreq=1270
  fac=1.0/32767.0
  if(mode(1:1).eq.'B' .or. mode(2:2).eq.'B') ntone_spacing=2
  if(mode(1:1).eq.'C' .or. mode(2:2).eq.'C') ntone_spacing=4
  if(mode(2:2).eq.'D') ntone_spacing=8
  if(mode(2:2).eq.'E') ntone_spacing=16
  npts=NMAX
  open(12,file='msgs.txt',status='old')

  write(*,1000)
1000 format('File N Mode  DT    freq   pol   fDop   SNR   Message'/    &
            '--------------------------------------------------------------------')

  do ifile=1,nfiles
     nmin=ifile-1
     if(mode(2:2).eq.' ') nmin=2*nmin
     write(fname,1002) nmin                      !Create the output filenames
1002 format('000000_',i4.4,'00')
     open(10,file=fname//'.iq',access='stream',status='unknown')
     open(11,file=fname//'.tf2',access='stream',status='unknown')

     call noisegen(d4,npts)                      !Generate Gaussuian noise

     if(msg0.ne.'                      ') then
        if(bq65) then
           call gen_q65_cwave(message,ntxfreq,ntone_spacing,msgsent,      &
                cwave,nwave)
        else
           call cgen65(message,ntone_spacing,samfac,nsendingsh,msgsent,  &
                cwave,nwave)
        endif
     endif
     rewind 12

     if(fdop.gt.0.0) call dopspread(cwave,nwave,fdop)

     do isig=1,nsigs

        if(msg0.eq.'                      ') then
           read(12,1004) message
1004       format(a22)
           if(bq65) then
           call gen_q65_cwave(msg,ntxfreq,mode65,msgsent,cwave,nwave)
           else
              call cgen65(message,ntone_spacing,samfac,nsendingsh,msgsent,    &
                   cwave,nwave)
           endif
        endif

        if(npol.lt.0) pol=(isig-1)*180.0/nsigs
        a=cos(pol/rad)
        b=sin(pol/rad)
        f=1000.0*(fa+fb)/2.0
        if(nsigs.gt.1) f=1000.0*(fa + (isig-1)*(fb-fa)/(nsigs-1.0))
        dphi=twopi*f*dt + 0.5*twopi

        snrdbx=snrdb
        if(snrdb.ge.-1.0) snrdbx=-15.0 - 15.0*(isig-1.0)/nsigs
        sig=sqrt(2.2*2500.0/96000.0) * 10.0**(0.05*snrdbx)
        write(*,1020) ifile,isig,mode,dt0,0.001*f,nint(pol),fDop,snrdbx,msgsent
1020    format(i3,i3,2x,a2,f6.2,f8.3,i5,2f7.1,2x,a22)

        phi=0.
!        i0=fsample*(3.5d0+0.05d0*(isig-1))
        i0=fsample*(1.d0 + dt0)
        do i=1,nwave
           phi=phi + dphi
           if(phi.lt.-twopi) phi=phi+twopi
           if(phi.gt.twopi) phi=phi-twopi
           xphi=phi
           z=sig*cwave(i)*cmplx(cos(xphi),-sin(xphi))
           zx=a*z
           zy=b*z
           j=i+i0
           d4(1,j)=d4(1,j) + real(zx)
           d4(2,j)=d4(2,j) + aimag(zx)
           d4(3,j)=d4(3,j) + real(zy)
           d4(4,j)=d4(4,j) + aimag(zy)
        enddo
     enddo

     do i=1,npts
        id4(1,i)=nint(rms*d4(1,i))
        id4(2,i)=nint(rms*d4(2,i))
        id4(3,i)=nint(rms*d4(3,i))
        id4(4,i)=nint(rms*d4(4,i))
        id2(1,i)=id4(1,i)
        id2(2,i)=id4(2,i)
     enddo

     write(10) fcenter,id2(1:2,1:npts)
     write(11) fcenter,id4(1:4,1:npts)
     close(10)
     close(11)
  enddo

999 end program mapsim

subroutine dopspread(cwave,nwave,fspread)

  parameter (NMAX=60*96000)
  parameter (NFFT=NMAX,NH=NFFT/2)
  complex cwave(NMAX)
  complex cspread(0:NMAX-1)

  twopi=8.0*atan(1.0)
  df=96000.0/nfft
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
  cwave=cspread*cwave                   !Apply Rayleigh fading
  
!  do i=0,nfft-1
!     p=real(cspread(i))**2 + aimag(cspread(i))**2
!     write(14,3010) i,p,cspread(i)
!3010 format(i8,3f12.6)
!  enddo

  return
end subroutine dopspread
