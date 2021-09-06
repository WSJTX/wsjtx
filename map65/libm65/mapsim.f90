program mapsim

! Generate simulated data for testing of MAP65

  parameter (NMAX=60*96000)
  real*4 d4(4,NMAX)                   !Floating-point data
  integer*2 id4(4,NMAX)               !i*2 data, dual polarization
  integer*2 id2(2,NMAX)               !i*2 data, single polarization
  complex cwave(NMAX)                 !Generated complex waveform (no noise)
  complex z,zx,zy
  real*8 fcenter,fsample,samfac,f,dt,twopi,phi,dphi
  logical bq65
  character msg0*22,message*22,msgsent*22,arg*8,fname*11,mode*2
  character*16 msg_list(60)
  data msg_list/                                          &
       'W1AAA K2BBB EM00','W2CCC K3DDD EM01','W3EEE K4FFF EM02',   &
       'W5GGG K6HHH EM03','W7III K8JJJ EM04','W9KKK K0LLL EM05',   &
       'G0MMM F1NNN JN06','G2OOO F3PPP JN07','G4QQQ F5RRR JN08',   &
       'G6SSS F7TTT JN09','W1XAA K2XBB EM10','W2XCC K3XDD EM11',   &
       'W3XEE K4XFF EM12','W5XGG K6XHH EM13','W7XII K8XJJ EM14',   &
       'W9XKK K0XLL EM15','G0XMM F1XNN JN16','G2XOO F3XPP JN17',   &
       'G4XQQ F5XRR JN18','G6XSS F7XTT JN19','W1YAA K2YBB EM20',   &
       'W2YCC K3YDD EM21','W3YEE K4YFF EM22','W5YGG K6YHH EM23',   &
       'W7YII K8YJJ EM24','W9YKK K0YLL EM25','G0YMM F1YNN JN26',   &
       'G2YOO F3YPP JN27','G4YQQ F5YRR JN28','G6YSS F7YTT JN29',   &
       'W1ZAA K2ZBB EM30','W2ZCC K3ZDD EM31','W3ZEE K4ZFF EM32',   &
       'W5ZGG K6ZHH EM33','W7ZII K8ZJJ EM34','W9ZKK K0ZLL EM35',   &
       'G0ZMM F1ZNN JN36','G2ZOO F3ZPP JN37','G4ZQQ F5ZRR JN38',   &
       'G6ZSS F7ZTT JN39','W1AXA K2BXB EM40','W2CXC K3DXD EM41',   &
       'W3EXE K4FXF EM42','W5GXG K6HXH EM43','W7IXI K8JXJ EM44',   &
       'W9KXK K0LXL EM45','G0MXM F1NXN JN46','G2OXO F3PXP JN47',   &
       'G4QXQ F5RXR JN48','G6SXS F7TXT JN49','W1AYA K2BYB EM50',   &
       'W2CYC K3DYD EM51','W3EYE K4FYF EM52','W5GYG K6HYH EM53',   &
       'W7IYI K8JYJ EM54','W9KYK K0LYL EM55','G0MYM F1NYN JN56',   &
       'G2OYO F3PYP JN57','G4QYQ F5RYR JN58','G6SYS F7TYT JN59'/
  
  nargs=iargc()
  if(nargs.ne.10) then
     print*,'Usage:   mapsim "message"     mode DT  fa fb nsigs pol fDop SNR nfiles'
     print*,'Example: mapsim "CQ K1ABC FN42" B 2.5 -20 20  21    45  0.0 -20   1'
     print*,' '
     print*,'         mode = A B C for JT65; QA-QE for Q65-60A' 
     print*,'         fa = lowest freq in kHz, relative to center'
     print*,'         fb = highest freq in kHz, relative to center'
     print*,'         message = "list" to use callsigns from list'
     print*,'         pol = -1 to generate a range of polarization angles.'
     print*,'         SNR = 0 to generate a range of SNRs.'
     go to 999
  endif

  call getarg(1,msg0)
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

  message=msg0                       !Transmitted message
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

  write(*,1000)
1000 format('File N Mode  DT    freq   pol   fDop   SNR   Message'/68('-'))

  do ifile=1,nfiles
     ilist=0
     nmin=ifile-1
     if(mode(2:2).eq.' ') nmin=2*nmin
     write(fname,1002) nmin                      !Create the output filenames
1002 format('000000_',i4.4)
     open(10,file=fname//'.iq',access='stream',status='unknown')
     open(11,file=fname//'.tf2',access='stream',status='unknown')

     call noisegen(d4,npts)                      !Generate Gaussuian noise

     if(msg0(1:4).ne.'list') then
        if(bq65) then
           call gen_q65_cwave(message,ntxfreq,ntone_spacing,msgsent,        &
                cwave,nwave)
        else
           call cgen65(message,ntone_spacing,samfac,nsendingsh,msgsent,     &
                cwave,nwave)
        endif
     endif

     if(fdop.gt.0.0) call dopspread(cwave,fdop)

     do isig=1,nsigs

        if(msg0(1:4).eq.'list') then
           ilist=ilist+1
           message=msg_list(ilist)
           if(bq65) then
              call gen_q65_cwave(message,ntxfreq,ntone_spacing,msgsent,     &
                   cwave,nwave)
           else
              call cgen65(message,ntone_spacing,samfac,nsendingsh,msgsent,  &
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
        if(snrdb.eq.0.0) snrdbx=-15.0 - 15.0*(isig-1.0)/nsigs
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

subroutine dopspread(cwave,fspread)

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
