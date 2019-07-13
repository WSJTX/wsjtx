program allsim

! Generate simulated data for WSJT-X modes: JT4, JT9, JT65, FT8, FT4, QRA64,
! and WSPR.  Also unmodulated carrier and 20 WPM CW.
  

  use wavhdr
  use packjt
  parameter (NMAX=60*12000)
  type(hdr) h
  integer*2 iwave(NMAX)                  !Generated waveform (no noise)
  integer itone(206)                     !Channel symbols (values 0-8)
  integer icw(250)
  integer*1 msgbits(87)
  logical*1 bcontest
  real*4 dat(NMAX)
  character message*22,msgsent*22,arg*8,mygrid*6
  character*37 msg37,msgsent37

  nargs=iargc()
  if(nargs.ne.1) then
     print*,'Usage:   allsim <snr>'
     go to 999
  endif

  call getarg(1,arg)
  read(arg,*) snrdb                  !S/N in dB (2500 hz reference BW)

  message='CQ KA2ABC FN20'
  mygrid='FN20  '
  bcontest=.false.
  rmsdb=25.
  rms=10.0**(0.05*rmsdb)
  sig=10.0**(0.05*snrdb)
  npts=NMAX

  call init_random_seed()       !Seed Fortran RANDOM_NUMBER generator
  call sgran()                  !Seed C rand generator (used in gran)

  h=default_header(12000,npts)  
  open(10,file='000000_0000.wav',access='stream',status='unknown')
  do i=1,npts                   !Generate gaussian noise
     dat(i)=gran()
  enddo

  itone=0
  call addit(itone,12000,85,6912,400,sig,dat)    !Unmodulated carrier

  call morse('CQ CQ DE KA2ABC KA2ABC',icw,ncw)
!  print*,ncw
!  write(*,3001) icw(1:ncw)
!3001 format(50i1)
  call addcw(icw,ncw,600,sig,dat)                !CW

  call genwspr(message,msgsent,itone)
  call addit(itone,12000,86,8192,800,sig,dat)    !WSPR (only 59 s of data)

  call gen9(message,0,msgsent,itone,itype)
  call addit(itone,12000,85,6912,1000,sig,dat)   !JT9

  call gen4(message,0,msgsent,itone,itype)
  call addit(itone,11025,206,2520,1200,sig,dat)  !JT4

  i3=-1
  n3=-1
  call genft8(message,i3,n3,msgsent,msgbits,itone)
  call addit(itone,12000,79,1920,1400,sig,dat)   !FT8

  msg37=message//'               '
  call genft4(msg37,0,msgsent37,itone)
  call addit(itone,12000,103,512,1600,sig,dat)   !FT4

  call genqra64(message,0,msgsent,itone,itype)
  call addit(itone,12000,84,6912,1800,sig,dat)   !QRA64

  call gen65(message,0,msgsent,itone,itype)
  call addit(itone,11025,126,4096,2000,sig,dat)  !JT65

  iwave(1:npts)=nint(rms*dat(1:npts))
  
  write(10) h,iwave(1:npts)
  close(10)

999 end program allsim
