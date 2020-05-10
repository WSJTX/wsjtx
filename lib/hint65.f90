subroutine hint65(s3,mrs,mrs2,nadd,nflip,mycall,hiscall,hisgrid,qual,decoded)

  use packjt
  use prog_args
  parameter (MAXCALLS=10000,MAXRPT=63)
  parameter (MAXMSG=2*MAXCALLS + 2 + MAXRPT)
  real s3(64,63)
  integer*1 sym1(0:62,MAXMSG)
  integer*1 sym2(0:62,MAXMSG)
  integer mrs(63),mrs2(63)
  integer dgen(12),sym(0:62),sym_rev(0:62)
  character*6 mycall,hiscall,hisgrid,call2(MAXCALLS)
  character*4 grid2(MAXCALLS),rpt(MAXRPT)
  character callsign*12,grid*4
  character*180 line
  character ceme*3,msg*22,msg00*22
  character*22 msg0(MAXMSG),decoded
  logical*1 eme(MAXCALLS)
  logical first
  data first/.true./
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
  save first,sym1,nused,msg0,sym2

  first=.true.   !### For now, at least: always recompute hypothetical messages
  if(first) then
     neme=0
     open(23,file=trim(data_dir)//'/CALL3.TXT',status='unknown')
     icall=0
     j=0
     do i=1,MAXCALLS
        read(23,1002,end=10) line
1002    format(a80)
        if(line(1:4).eq.'ZZZZ') cycle
        if(line(1:2).eq.'//') cycle
        i1=index(line,',')
        if(i1.lt.4) cycle
        i2=index(line(i1+1:),',')
        if(i2.lt.5) cycle
        i2=i2+i1
        i3=index(line(i2+1:),',')
        if(i3.lt.1) i3=index(line(i2+1:),' ')
        i3=i2+i3
        callsign=line(1:i1-1)
        grid=line(i1+1:i1+4)
        ceme=line(i2+1:i3-1)
        eme(i)=ceme.eq.'EME'
        if(neme.eq.1 .and. (.not.eme(i))) cycle
        j=j+1
        call2(j)=callsign(1:6)               !### Fix for compound callsigns!
        grid2(j)=grid
     enddo
10   ncalls=j
     close(23)

! NB: generation of test messages is not yet complete!
     j=0
     do i=-1,ncalls
        if(i.eq.0 .and. hiscall.eq.'      ' .and. hisgrid(1:4).eq.'    ') cycle
        mz=2
        if(i.eq.-1) mz=1
        if(i.eq.0) mz=65
        do m=1,mz
           j=j+1
           if(i.eq.-1) then
              msg='0123456789ABC'
           else if(i.eq.0) then
              if(m.eq.1) msg=mycall//' '//hiscall//' '//hisgrid(1:4)
              if(m.eq.2) msg='CQ '//hiscall//' '//hisgrid(1:4)
              if(m.ge.3) msg=mycall//' '//hiscall//' '//rpt(m-2)
           else
              if(m.eq.1)  msg=mycall//' '//call2(i)//' '//grid2(i)
              if(m.eq.2)  msg='CQ '//call2(i)//' '//grid2(i)
           endif
           call fmtmsg(msg,iz)
           call packmsg(msg,dgen,itype) !Pack message into 72 bits
           call rs_encode(dgen,sym_rev)            !RS encode
           sym(0:62)=sym_rev(62:0:-1)
           sym1(0:62,j)=sym

           call interleave63(sym_rev,1)            !Interleave channel symbols
           call graycode(sym_rev,63,1,sym_rev)     !Apply Gray code
           sym2(0:62,j)=sym_rev(0:62)
           msg0(j)=msg
        enddo
     enddo
     nused=j
     first=.false.
  endif

  ref0=0.
  do j=1,63
     ref0=ref0 + s3(mrs(j)+1,j)
  enddo

  u1=0.
  u1=-99.0
  u2=u1

! Find u1 and u2 (best and second-best) codeword from a list, using 
! a bank of matched filters on the symbol spectra s3(i,j).
  ipk=1
  ipk2=0
  msg00='                      '
  do k=1,nused
     if(k.ge.2 .and. k.le.64 .and. nflip.lt.0) cycle
! Test all messages if nflip=+1; skip the CQ messages if nflip=-1.
     if(nflip.gt.0 .or. msg0(k)(1:3).ne.'CQ ') then
        psum=0.
        ref=ref0
        do j=1,63
           i=sym2(j-1,k)+1
           psum=psum + s3(i,j)
           if(i.eq.mrs(j)+1) ref=ref - s3(i,j) + s3(mrs2(j)+1,j)
        enddo
        p=psum/ref

        if(p.gt.u1) then
           if(msg0(k).ne.msg00) then
              ipk2=ipk
              u2=u1
           endif
           u1=p
           ipk=k
           msg00=msg0(k)
        endif
        if(msg0(k).ne.msg00 .and. p.gt.u2) then
           u2=p
           ipk2=k
        endif
     endif
  enddo

  decoded='                      '
  bias=max(1.12*u2,0.35)
  if(nadd.ge.4) bias=max(1.08*u2,0.45)
  if(nadd.ge.8) bias=max(1.04*u2,0.60)
  qual=100.0*(u1-bias)
  qmin=1.0
  if(qual.ge.qmin) decoded=msg0(ipk)

  return
end subroutine hint65
