program robo

! Examine spots from PSK Reporter for past week to identify probable robots
  
! In a bash shell, from command line:
!    $ curl https://pskreporter.info/cgi-bin/robot.pl > robo.dat
!    $ robo | sort -nr -k2

  parameter (NCHARS=20000)
  parameter (NMAX=100)

  character*1 c
  character*20000 blob
  character*12 callsign(0:NMAX)
  integer*1 ic1
  
  open(10,file='/tmp/robo.dat',status='old',access='stream')
  callsign='            '
  
  do ichar=1,NCHARS
     read(10,end=10) ic1
     ic=ic1
     if(ic.lt.0) ic=ic+256
     c=char(ic)
     blob(ichar:ichar)=c
  enddo
10 continue

  do icall=1,NMAX
     i1=index(blob,'":{"median_minutes":')-1
     do i=i1,i1-10,-1
        if(i1.lt.1) go to 20
        if(blob(i:i).eq.'"') exit
     enddo
     i0=i+1
     callsign(icall)=blob(i0:i1)
     i2=index(blob,',')-1
     read(blob(i1+21:i2),*) median_minutes
     i3=index(blob,'median_hours')+14
     i4=index(blob,'}')-1
     read(blob(i3:i4),*) median_hours
     write(*,3001) median_minutes,median_hours,trim(callsign(icall))
3001 format(2i5,2x,a)
     blob=blob(i4+3:)
  enddo
  
20 continue
end program robo
