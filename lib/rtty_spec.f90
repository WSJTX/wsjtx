program rtty_spec

! Generate simulated data for standard RTTY and WSJT-X modes FT8, FT4

  use wavhdr
  use packjt
  parameter (NMAX=15*12000)
  type(hdr) h
  complex cwave(NMAX)
  real wave(NMAX)
  real*4 dat(NMAX)             !Generated waveform
  integer*2 iwave(NMAX)        !Generated waveform
  integer itone(680)           !Channel symbols (values 0-1, 0-3, 0-7)
  integer*1 msgbits(77)
  character*37 msg37,msgsent37
  character*8 arg

  nargs=iargc()
  if(nargs.ne.1) then
     print*,'Usage:   rtty_spec <snr>'
     go to 999
  endif
  call getarg(1,arg)
  read(arg,*) snrdb                  !S/N in dB (2500 hz reference BW)

  rmsdb=25.
  rms=10.0**(0.05*rmsdb)
  sig=10.0**(0.05*snrdb)
  npts=NMAX

  do i=1,NMAX                   !Generate gaussian noise
     dat(i)=gran()
  enddo

! Add the RTTY signal
  fsample=12000.0                   !Sample rate (Hz)
  dt=1.0/fsample                    !Sample interval (s)
  twopi=8.0*atan(1.0)
  phi=0.
  dphi=0.
  j0=-1
  do i=6001,NMAX-6000
     j=nint(i*dt/0.022)
     if(j.ne.j0) then
        f0=1415.0
        call random_number(rr)
        if(rr.gt.0.5) f0=1585.0
        dphi=twopi*f0*dt
        j0=j
     endif
     phi=phi+dphi
     if(phi.gt.twopi) phi=phi-twopi
     dat(i)=dat(i) + sig*sin(phi)
  enddo

! FT8 signal (FSK)
  i3=0
  n3=0
  msg37='WB9XYZ KA2ABC FN42'
  call genft8(msg37,i3,n3,msgsent37,msgbits,itone)
  nsym=79
  nsps=1920
  bt=99.0
  f0=3500.0
  icmplx=0
  nwave=nsym*nsps
  call gen_ft8wave(itone,nsym,nsps,bt,fsample,f0,cwave,wave,icmplx,nwave)
  dat(6001:6000+nwave)=dat(6001:6000+nwave) + sig*wave(1:nwave)

! FT8 signal (GFSK)
  i3=0
  n3=0
  msg37='WB9XYZ KA2ABC FN42'
  call genft8(msg37,i3,n3,msgsent37,msgbits,itone)
  nsym=79
  nsps=1920
  bt=2.0
  f0=4000.0
  icmplx=0
  nwave=nsym*nsps
  call gen_ft8wave(itone,nsym,nsps,bt,fsample,f0,cwave,wave,icmplx,nwave)
  dat(6001:6000+nwave)=dat(6001:6000+nwave) + sig*wave(1:nwave)

! Add the FT4 signal
  ichk=0
  call genft4(msg37,ichk,msgsent37,msgbits,itone)
  nsym=103
  nsps=576
  f0=4500.0
  icmplx=0
  nwave=(nsym+2)*nsps
  call gen_ft4wave(itone,nsym,nsps,fsample,f0,cwave,wave,icmplx,nwave)
  dat(6001:6000+nwave)=dat(6001:6000+nwave) + sig*wave(1:nwave)

  h=default_header(12000,NMAX)
  datmax=maxval(abs(dat))
  iwave=nint(32767.0*dat/datmax)
  open(10,file='000000_000001.wav',access='stream',status='unknown')
  write(10) h,iwave
  close(10)

999 end program rtty_spec
