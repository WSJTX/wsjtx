program test_qra66

  character*70 cmd1,cmd2,line
  character*22 msg
  character*8 arg
  integer nretcode(0:11)
  integer fDop
  real fspread
  logical decok

  nargs=iargc()
  if(nargs.ne.7) then
     print*,'Usage:   test_qra66        "msg"     ndepth freq  DT fDop nfiles SNR'
     print*,'Example: test_qra66 "K1ABC W9XYZ EN37"  3   1500 0.0  5    100    0'
     print*,'         SNR = 0 to loop over all relevant SNRs'
     go to 999
  endif
  call getarg(1,msg)
  call getarg(2,arg)
  read(arg,*) ndepth
  call getarg(3,arg)
  read(arg,*) nf0
  call getarg(4,arg)
  read(arg,*) dt
  call getarg(5,arg)
  read(arg,*) fDop
  call getarg(6,arg)
  read(arg,*) nfiles
  call getarg(7,arg)
  read(arg,*) nsnr

!                1         2         3         4         5         6
!       1234567890123456789012345678901234567890123456789012345678901234'
  cmd1='qra66sim "K1ABC W9XYZ EN37      " A 1500  5.0  0.0  100 -10 > junk0'
  cmd2='jt9 -3 -p 15 -L 300 -H 3000 -d 1 *.wav > junk'

  write(cmd1(10:33),'(a)') '"'//msg//'"'
  write(cmd1(37:40),'(i4)') nf0
  write(cmd1(41:45),'(i5)') fDop
  write(cmd1(46:50),'(f5.2)') dt
  write(cmd1(51:55),'(i5)') nfiles
  write(cmd2(32:32),'(i1)') ndepth
  call system('rm -f *.wav')

  write(*,1000) (j,j=0,11)
  write(12,1000) (j,j=0,11)
1000 format(/'SNR d Dop Sync Dec1 DecN Bad',i5,11i4,'  tdec'/83('-'))
  ia=-12
  ib=-30
  if(nsnr.ne.0) then
     ia=nsnr
     ib=nsnr
  endif
  
  do nsnr=ia,ib,-1
     nsync=0
     ndecodes=0
     nfalse=0
     nretcode=0
     navg=0
     write(cmd1(57:59),'(i3)') nsnr
     call system(cmd1)
     call sec0(0,tdec)
     call system(cmd2)
     call sec0(1,tdec)
     open(10,file='junk',status='unknown')
     n=0
     do iline=1,9999
        read(10,'(a70)',end=10) line
        if(len(trim(line)).lt.60) cycle
        read(line(11:20),*) xdt,nf
        if(abs(xdt-dt).lt.0.15 .and. abs(nf-nf0).lt.4) nsync=nsync+1
        read(line(60:),*) irc,iavg
        if(irc.lt.0) cycle
        decok=index(line,'W9XYZ').gt.0
        if(decok) then
           i=irc
           if(i.le.11) then
              ndecodes=ndecodes + 1
              navg=navg + 1
           else
              i=mod(i,10)
              navg=navg + 1
           endif
           nretcode(i)=nretcode(i) + 1
        else
           nfalse=nfalse + 1
           print*,'False: ',line
        endif
     enddo
10   close(10)
     write(*,1100) nsnr,ndepth,fDop,nsync,ndecodes,navg,nfalse,nretcode,tdec/nfiles
     write(12,1100) nsnr,ndepth,fDop,nsync,ndecodes,navg,nfalse,nretcode,tdec/nfiles
1100 format(i3,i2,i3,3i5,i4,i6,11i4,f6.2)
     flush(6)
     flush(12)
  enddo

999 end program test_qra66

  include 'sec0.f90'

