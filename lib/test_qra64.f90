program test_qra64

  character*71 cmd1,cmd2,line
  character*22 msg
  character*8 arg
  character*1 csubmode
  integer nretcode(0:11)
  logical decok

  nargs=iargc()
  if(nargs.ne.9) then
     print*,'Usage:   test_qra64        "msg"       A-D depth freq  DT fDop TRp nfiles SNR'
     print*,'Example: test_qra64 "K1ABC W9XYZ EN37"  A    3   1000 0.0  5.0  60  100   -20'
     print*,'         SNR = 0 to loop over all relevant SNRs'
     go to 999
  endif
  call getarg(1,msg)
  call getarg(2,csubmode)
  call getarg(3,arg)
  read(arg,*) ndepth
  call getarg(4,arg)
  read(arg,*) nf0
  call getarg(5,arg)
  read(arg,*) dt
  call getarg(6,arg)
  read(arg,*) fDop
  call getarg(7,arg)
  read(arg,*) ntrperiod
  call getarg(8,arg)
  read(arg,*) nfiles
  call getarg(9,arg)
  read(arg,*) nsnr

  nsps=7680
  i50=-28
  ia=-20
  ib=-33
  if(nsnr.ne.0) then
     ia=nsnr
     ib=nsnr
  endif

  baud=12000.0/nsps
  tsym=1.0/baud

!                1         2         3         4         5         6         7
!       12345678901234567890123456789012345678901234567890123456789012345678901'
  cmd1='qra64sim "K1ABC W9XYZ EN37      " A  1  0.2 0.00  100 -20 > junk0'
  
  cmd2='jt9 -q -L 300 -H 3000 -f 1000 -d 3 -b A *.wav > junk'
!       jt9 -q -L 300 -H 3000 -f 1000 -d 3 -b A *.w1000  unk
  
  write(cmd1(10:33),'(a)') '"'//msg//'"'
  cmd1(35:35)=csubmode
  write(cmd1(40:43),'(f4.1)') fDop
  write(cmd1(44:48),'(f5.2)') dt
  write(cmd1(49:53),'(i5)') nfiles

  write(cmd2(26:29),'(i4)') nf0
  write(cmd2(34:34),'(i1)') ndepth
  cmd2(39:39)=csubmode
  
  call system('rm -f *.wav')

  write(*,1000) (j,j=0,11)
  write(12,1000) (j,j=0,11)
1000 format(/'SNR d  Dop Sync Dec1 DecN Bad',i6,11i4,'  tdec dtavg dtrms'/97('-'))

  dterr=tsym/4.0
  nferr=max(1,nint(0.5*baud))
  
  do nsnr=ia,ib,-1
     nsync=0
     ndecodes=0
     nfalse=0
     nretcode=0
     navg=0
     sumxdt=0.
     sqxdt=0.
     write(cmd1(55:57),'(i3)') nsnr
     call system(cmd1)
     call sec0(0,tdec)
     call system(cmd2)
     call sec0(1,tdec)
     open(10,file='junk',status='unknown')
     n=0
     do iline=1,9999
        read(10,'(a71)',end=10) line
        if(index(line,'<Decode').eq.1) cycle
        read(line(11:20),*) xdt,nf
        irc=-1
        if(line(23:23).ne.' ') read(line(45:46),*) irc
!        write(71,3071) dt,xdt,abs(xdt-dt),dterr,nf0,nf,abs(nf-nf0),nferr,irc
!3071    format(4f6.2,4i5,i8)
        if(abs(xdt-dt).le.dterr .and. abs(nf-nf0).le.nferr) nsync=nsync+1
        if(irc.lt.0) cycle
        decok=index(line,'W9XYZ').gt.0
        if(decok) then
           i=irc
           if(i.le.11) then
              ndecodes=ndecodes + 1
              sumxdt=sumxdt + xdt
              sqxdt=sqxdt + xdt*xdt
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
     xdt_avg=0.
     xdt_rms=0.
     if(ndecodes.ge.2) then
        xdt_avg=sumxdt/ndecodes
        xdt_rms=sqxdt/(ndecodes-1) - xdt_avg*xdt_avg
     endif
     write(*,1100) nsnr,ndepth,fDop,nsync,ndecodes,navg,nfalse,nretcode,   &
          tdec/nfiles,xdt_avg,xdt_rms
     write(12,1100) nsnr,ndepth,fDop,nsync,ndecodes,navg,nfalse,nretcode,  &
          tdec/nfiles,xdt_avg,xdt_rms
1100 format(i3,i2,f5.1,3i5,i4,i6,11i4,3f6.2)
     flush(6)
     flush(12)
  enddo

999 end program test_qra64

  include 'sec0.f90'

