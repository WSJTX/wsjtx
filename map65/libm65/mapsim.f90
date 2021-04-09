program mapsim

! Generate simulated data for testing of MAP65

  parameter (NMAX=96000*60)
  real*4 d4(4,NMAX)                   !Floating-point data
  integer*2 id4(4,NMAX)               !i*2 data, dual polarization
  integer*2 id2(2,NMAX)               !i*2 data, single polarization
  complex cwave(NMAX)                 !Generated complex waveform (no noise)
  complex z,zx,zy
  real*8 fcenter,fsample,samfac,f,dt,twopi,phi,dphi
  character msg0*22,message*22,msgsent*22,arg*8,fname*13,mode*2

  nargs=iargc()
  if(nargs.ne.9) then
     print*,'Usage: mapsim  DT "message"     mode f1 f2 nsigs pol SNR nfiles'
     print*,'Example:      2.5 "CQ K1ABC FN42" B -22 33  20    45 -20    1'
     print*,' '
     print*,'Enter message = "" to use entries in msgs.txt.'
     print*,'Enter pol = -1 to generate a range of polarization angles.'
     print*,'Enter SNR = 0 to generate a range of SNRs.'
     go to 999
  endif

  call getarg(1,arg)
  read(arg,*) dt0                    !Time delay
  call getarg(2,msg0)
  message=msg0                       !Transmitted message
  call getarg(3,mode)                !JT65 sub-mode (A B C B2 C2)
  call getarg(4,arg)
  read(arg,*) f1                     !Lowest freq (kHz, relative to fcenter)
  call getarg(5,arg)
  read(arg,*) f2                     !Highest freq
  call getarg(6,arg)
  read(arg,*) nsigs                  !Number of signals in each file
  call getarg(7,arg)
  read(arg,*) npol                   !Polarization in degrees
  call getarg(8,arg)
  read(arg,*) snrdb                  !S/N
  pol=npol
  call getarg(9,arg)
  read(arg,*) nfiles                 !Number of files

  rmsdb=25.
  rms=10.0**(0.05*rmsdb)
  fcenter=144.125d0                  !Center frequency (MHz)
  fsample=96000.d0                   !Sample rate (Hz)
  dt=1.d0/fsample                    !Sample interval (s)
  twopi=8.d0*atan(1.d0)
  rad=360.0/twopi
  samfac=1.d0
  mode65=1
  if(mode(1:1).eq.'B') mode65=2
  if(mode(1:1).eq.'C') mode65=4
  nfast=1
  if(mode(2:2).eq.'2') nfast=2
  npts=NMAX/nfast
  open(12,file='msgs.txt',status='old')

  write(*,1000)
1000 format('File  N   freq     S/N  pol  Message'/    &
            '---------------------------------------------------')

  do ifile=1,nfiles
     nmin=ifile-1
     if(mode(2:2).eq.' ') nmin=2*nmin
     write(fname,1002) nmin                      !Create the output filenames
1002 format('000000_',i4.4,'00')
     open(10,file=fname//'.iq',access='stream',status='unknown')
     open(11,file=fname//'.tf2',access='stream',status='unknown')

     call noisegen(d4,npts)                      !Generate Gaussuian noise

     if(msg0.ne.'                      ') then
        call cgen65(message,mode65,nfast,samfac,nsendingsh,msgsent,cwave,nwave)
     endif

     rewind 12
     do isig=1,nsigs

        if(msg0.eq.'                      ') then
           read(12,1004) message
1004       format(a22)
           call cgen65(message,mode65,nfast,samfac,nsendingsh,msgsent,    &
                cwave,nwave)
        endif
           
        if(npol.lt.0) pol=(isig-1)*180.0/nsigs
        a=cos(pol/rad)
        b=sin(pol/rad)
        f=1000.0*(f1+f2)/2.0
        if(nsigs.gt.1) f=1000.0*(f1 + (isig-1)*(f2-f1)/(nsigs-1.0))
        dphi=twopi*f*dt + 0.5*twopi

        snrdbx=snrdb
        if(snrdb.ge.-1.0) snrdbx=-15.0 - 15.0*(isig-1.0)/nsigs
        sig=sqrt(2.2*2500.0/96000.0) * 10.0**(0.05*snrdbx)
        write(*,1020) ifile,isig,0.001*f,snrdbx,nint(pol),msgsent
1020    format(i3,i4,f8.3,f7.1,i5,2x,a22)

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
