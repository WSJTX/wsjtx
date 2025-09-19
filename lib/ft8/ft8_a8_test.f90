program ft8_a8_test

! Aid for testing "a8" decodes -- those based on known MyCall, DXcall, and DXgrid.

  use wavhdr
  use ft8_a7
  include 'ft8_params.f90'           !Set various constants
  type(hdr) h                        !Header for .wav file
  character arg*12,fname*120
  character mycall*12,dxcall*12,dxgrid*6
  character*37 msgbest
  character*4 apflag
  integer*2 iwave(NMAX)              !Raw data from a *.wav file, fsample = 12000 Hz
  real dd(NMAX)                      !Floating point copy of iwave

! Get command-line argument(s)
  nargs=iargc()
  if(nargs.lt.6) then
     print*,'Usage:   ft8_a8_test MyCall DXcall DXgrid  f1 ndepth    fname [...]'
     print*,'Example: ft8_a8_test  W3SZ  DL3WDG  JN68  1500  3  250804_120500.wav'
     go to 999
  endif
  call getarg(1,mycall)                  !Message to be transmitted
  call getarg(2,dxcall)
  call getarg(3,dxgrid)
  call getarg(4,arg)
  read(arg,*) f1                         !Expected Rx Freq
  call getarg(5,arg)
  read(arg,*) ndepth                     !Decoding depth

  do ifile=1,nargs-5
     call getarg(ifile+5,fname)                   !File name for *.wav data
     open(10,file=trim(fname),status='old',access='stream')
     read(10) h,iwave
     fac=1.0/32767.0
     dd=fac*iwave

     call ft8_a8d(dd,mycall,dxcall,dxgrid,f1,xdt,fbest,xsnr,plog,msgbest)

     apflag='  a8'
     if(plog.lt.-155) apflag(1:1)='?'
     if(len(trim(msgbest)).eq.0) apflag='    '
     write(*,1100) ifile,xdt,fbest,xsnr,plog,msgbest,apflag
1100 format(i4,f6.2,3f7.1,1x,a37,a4)
  enddo

999 end program ft8_a8_test
