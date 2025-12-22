program test_ft8q3

! Test q3-style decodes for FT8.

  use packjt77
  parameter(NN=79,NSPS=32)
  parameter(NWAVE=NN*NSPS)               !2528
  parameter(NZ=3200,NLAGS=NZ-NWAVE)
  character arg*12
  character*37 msg
  character*12 call_1,call_2
  character*4 grid4
  complex cd(0:NZ-1)

! Get command-line argument(s)
  nargs=iargc()
  if(nargs.ne.4 .and. nargs.ne.5) then
     print*,'Usage: ft8q3 DT f0 call_1 call_2 [grid4]'
     go to 999
  endif
  call getarg(1,arg)
  read(arg,*) xdt                        !Time offset from nominal (s)
  call getarg(2,arg)
  read(arg,*) f0                         !Frequency (Hz)
  call getarg(3,call_1)                  !First callsign
  call getarg(4,call_2)                  !Second callsign
  grid4='    '
  if(nargs.eq.5) call getarg(5,grid4)    !Locator for call_2

  do i=0,NZ-1
     read(40,3040) cd(i)
3040 format(17x,2f10.3)
  enddo

  call sec0(0,t)
  call ft8q3(cd,xdt,f0,call_1,call_2,grid4,msg,snr)
  call sec0(1,t)
  write(*,1100) t,snr,trim(msg)
1100 format('Time:',f6.2,'   S/N:',f6.1,'   msg: ',a)

999 end program test_ft8q3
