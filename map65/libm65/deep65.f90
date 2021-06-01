subroutine deep65(s3,mode65,neme,flip,mycall,hiscall,hisgrid,decoded,qual)

  use timer_module, only: timer
  parameter (MAXCALLS=10000,MAXRPT=63)
  real s3(64,63)
  character callsign*12,grid*4,message*22,hisgrid*6,c*1,ceme*3
  character*12 mycall,hiscall
  character*22 decoded,bestmsg
  character*22 testmsg(2*MAXCALLS + 2 + MAXRPT)
  character*15 callgrid(MAXCALLS)
  character*180 line
  character*4 rpt(MAXRPT)
  integer ncode(63,2*MAXCALLS + 2 + MAXRPT)
  real pp(2*MAXCALLS + 2 + MAXRPT)
  common/mrscom/ mrs(63),mrs2(63)
  common/c3com/ mcall3a
  data rpt/'-01','-02','-03','-04','-05',          &
           '-06','-07','-08','-09','-10',          &
           '-11','-12','-13','-14','-15',          &
           '-16','-17','-18','-19','-20',          &
           '-21','-22','-23','-24','-25',          &
           '-26','-27','-28','-29','-30',          &
           'R-01','R-02','R-03','R-04','R-05',     &
           'R-06','R-07','R-08','R-09','R-10',     &
           'R-11','R-12','R-13','R-14','R-15',     &
           'R-16','R-17','R-18','R-19','R-20',     &
           'R-21','R-22','R-23','R-24','R-25',     &
           'R-26','R-27','R-28','R-29','R-30',     &
           'RO','RRR','73'/
  save

  if(mcall3a.eq.0) go to 30

  call timer('deep65a ',0)
  mcall3a=0
  rewind 23
  k=0
  icall=0
  do n=1,MAXCALLS
     if(n.eq.1) then
        callsign=hiscall
        do i=4,12
           if(ichar(callsign(i:i)).eq.0) callsign(i:i)=' '
        enddo
        grid=hisgrid(1:4)
        if(ichar(grid(3:3)).eq.0) grid(3:3)=' '
        if(ichar(grid(4:4)).eq.0) grid(4:4)=' '
     else
        read(23,1002,end=20) line
1002    format (A80)
        if(line(1:4).eq.'ZZZZ') go to 20
        if(line(1:2).eq.'//') go to 10
        i1=index(line,',')
        if(i1.lt.4) go to 10
        i2=index(line(i1+1:),',')
        if(i2.lt.5) go to 10
        i2=i2+i1
        i3=index(line(i2+1:),',')
        if(i3.lt.1) i3=index(line(i2+1:),' ')
        i3=i2+i3
        callsign=line(1:i1-1)
        grid=line(i1+1:i2-1)
        ceme=line(i2+1:i3-1)
        if(neme.eq.1 .and. ceme.ne.'EME') go to 10
     endif

     icall=icall+1
     j1=index(mycall,' ') - 1
     if(j1.le.-1) j1=12
     if(j1.lt.3) j1=6
     j2=index(callsign,' ') - 1
     if(j2.le.-1) j2=12
     if(j2.lt.3) j2=6
     j3=index(mycall,'/')                 ! j3>0 means compound mycall
     j4=index(callsign,'/')               ! j4>0 means compound hiscall
     callgrid(icall)=callsign(1:j2)

     mz=1
! Allow MyCall + HisCall + rpt (?)
     if(n.eq.1 .and. j3.lt.1 .and. j4.lt.1 .and. callsign(1:6).ne.'      ')  &
          mz=MAXRPT+1
     do m=1,mz
        if(m.gt.1) grid=rpt(m-1)
        if(j3.lt.1 .and.j4.lt.1) callgrid(icall)=callsign(1:j2)//' '//grid
        message=mycall(1:j1)//' '//callgrid(icall)
        k=k+1
        testmsg(k)=message
        call encode65(message,ncode(1,k))
        
! Insert CQ message
        if(j4.lt.1) callgrid(icall)=callsign(1:j2)//' '//grid
        message='CQ '//callgrid(icall)
        k=k+1
        testmsg(k)=message
        call encode65(message,ncode(1,k))
     enddo
10   continue
  enddo

20 continue
  ntot=k
  call timer('deep65a ',1)

30 continue
  call timer('deep65b ',0)
  ref0=0.
  do j=1,63
     ref0=ref0 + s3(mrs(j),j)
  enddo

  p1=-1.e30
  do k=1,ntot
     pp(k)=0.
     if(k.ge.2 .and. k.le.64 .and. flip.lt.0.0) cycle
! Test all messages if flip=+1; skip the CQ messages if flip=-1.
     if(flip.gt.0.0 .or. testmsg(k)(1:3).ne.'CQ ') then
        sum=0.
        ref=ref0
        do j=1,63
           i=ncode(j,k)+1
           sum=sum + s3(i,j)
           if(i.eq.mrs(j)) ref=ref - s3(i,j) + s3(mrs2(j),j)
        enddo
        p=sum/ref
        pp(k)=p
        if(p.gt.p1) then
           p1=p
           ip1=k
           bestmsg=testmsg(k)
        endif
     endif
  enddo

  p2=-1.e30
  do i=1,ntot
     if(pp(i).gt.p2 .and. testmsg(i).ne.bestmsg) p2=pp(i)
  enddo

  if(mode65.eq.1) bias=max(1.12*p2,0.335)
  if(mode65.eq.2) bias=max(1.08*p2,0.405)
  if(mode65.ge.4) bias=max(1.04*p2,0.505)

  if(p2.eq.p1 .and. p1.ne.-1.e30) then
     open(77,file='error.log',status='unknown',access='append')
     write(77,*) p1,p2,ip1,bestmsg
     close(77)
  endif

  qual=100.0*(p1-bias)

  decoded='                      '
  c=' '

  if(qual.gt.1.0) then
     if(qual.lt.6.0) c='?'
     decoded=testmsg(ip1)
  else
     qual=0.
  endif
  decoded(22:22)=c

! Make sure everything is upper case.
  do i=1,22
     if(decoded(i:i).ge.'a' .and. decoded(i:i).le.'z')                &
          decoded(i:i)=char(ichar(decoded(i:i))-32)
  enddo
  call timer('deep65b ',1)

  return
end subroutine deep65
