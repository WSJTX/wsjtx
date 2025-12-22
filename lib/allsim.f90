program allsim

! Generate simulated data for WSJT-X modes: JT4, JT9, JT65, FT8, FT4, QRA64,
! and WSPR.  Also unmodulated carrier and 20 WPM CW.

  use wavhdr
  use packjt
  parameter (NMAX=60*12000)
  type(hdr) h                            !Header for the .wav file
  integer*2 iwave(NMAX)                  !Generated waveform (no noise)
  integer itone(206)                     !Channel symbols (values 0-8)
  integer icw(250)                       !Encoded CW message bits
  integer*1 msgbits(101)                 !Encoded message bits for FST4 FT8 FT4
  real dat(NMAX)                         !Audio waveform
  complex cwave(NMAX)
  real wave(NMAX)
  character message*22,msgsent*22,arg*8
  character*37 msg37,msgsent37

  nargs=iargc()
  if(nargs.ne.1 .and. nargs.ne.2) then
     print*,'Usage:    allsim snr [isig]'
     print*,'Examples: allsim -10           #Include all signal types'
     print*,'          allsim -10 6         #Include FT8 only'
     print*,'isig order:    1     2   3    4   5   6   7   8   9   10'
     print*,'            Carrier CW WSPR FST4 JT9 JT4 FT8 FT4 Q65 JT65'
     go to 999
  endif

  call getarg(1,arg)
  read(arg,*) snrdb                  !S/N in dB (2500 hz reference BW)
  isig=0
  if(nargs.eq.2) then
     call getarg(2,arg)
     read(arg,*) isig
  endif

  message='CQ KA2ABC FN20'
  msg37=message//'               '
  rmsdb=25.
  rms=10.0**(0.05*rmsdb)
  sig=10.0**(0.05*snrdb)

  call init_random_seed()       !Seed Fortran RANDOM_NUMBER generator
  call sgran()                  !Seed C rand generator (used in gran)

  h=default_header(12000,NMAX)
  open(10,file='000000_0000.wav',access='stream',status='unknown')
  do i=1,NMAX                   !Generate gaussian noise
     dat(i)=gran()
  enddo

  itone=0
  if(isig.eq.0 .or. isig.eq.1) then
     call addit(itone,12000,85,6912,400,sig,dat)    !1 Unmodulated carrier
  endif

  if(isig.eq.0 .or. isig.eq.2) then
     call morse('CQ CQ DE KA2ABC KA2ABC',icw,ncw)
     call addcw(icw,ncw,600,sig,dat)                !2 CW at 20 WPM
  endif

  if(isig.eq.0 .or. isig.eq.3) then
     call genwspr(message,msgsent,itone)
     call addit(itone,12000,86,8192,800,sig,dat)    !3 WSPR (only 59 s of data)
  endif

  if(isig.eq.0 .or. isig.eq.4) then                    !4 FST4-60
     iwspr=0
     call genfst4(msg37,0,msgsent37,msgbits,itone,iwspr)
     nwave=162*3888
     call gen_fst4wave(itone,160,3888,nwave,12000.0,1,1000.0,0,cwave,wave)
     dat(6001:6000+nwave)=dat(6001:6000+nwave) + sig*wave(1:nwave)
  endif

  if(isig.eq.0 .or. isig.eq.5) then
     call gen9(message,0,msgsent,itone,itype)
     call addit(itone,12000,85,6912,1200,sig,dat)      !4 JT9
  endif

  if(isig.eq.0 .or. isig.eq.6) then
     call gen4(message,0,msgsent,itone,itype)
     call addit(itone,11025,206,2520,1400,sig,dat)     !6 JT4
  endif

  if(isig.eq.0 .or. isig.eq.7) then
     call genft8(msg37,i3,n3,msgsent37,msgbits,itone)  !7 FT8
     nwave=79*1920
     call gen_ft8wave(itone,79,1920,2.0,12000.0,1600.0,cwave,wave,0,nwave)
     dat(6001:6000+nwave)=dat(6001:6000+nwave) + sig*wave(1:nwave)
     k=30*12000
     dat(6001+k:6000+nwave+k)=dat(6001+k:6000+nwave+k) + sig*wave(1:nwave)
  endif

  if(isig.eq.0 .or. isig.eq.8) then
     call genft4(msg37,0,msgsent37,msgbits,itone)      !8 FT4
     nwave=105*576
     call gen_ft4wave(itone,103,576,12000.0,1800.0,cwave,wave,0,nwave)
     dat(6001:6000+nwave)=dat(6001:6000+nwave) + sig*wave(1:nwave)
     k=15*12000
     dat(6001+k:6000+nwave+k)=dat(6001+k:6000+nwave+k) + sig*wave(1:nwave)
     k=30*12000
     dat(6001+k:6000+nwave+k)=dat(6001+k:6000+nwave+k) + sig*wave(1:nwave)
     k=45*12000
     dat(6001+k:6000+nwave+k)=dat(6001+k:6000+nwave+k) + sig*wave(1:nwave)
  endif

  if(isig.eq.0 .or. isig.eq.9) then
     call genq65(msg37,0,msgsent37,itone,i3,n3)
     call addit(itone,12000,85,7200,2000,sig,dat)      !9 Q65
  endif

  if(isig.eq.0 .or. isig.eq.10) then
     call gen65(message,0,msgsent,itone,itype)
     call addit(itone,11025,126,4096,2200,sig,dat)     !10 JT65A
  endif

  iwave=nint(rms*dat)
  write(10) h,iwave
  close(10)

999 end program allsim
