subroutine q65_ap(nQSOprogress,ipass,ncontest,lapcqonly,iaptype,   &
     apsym0,apmask,apsymbols)

  integer apsym0(58),aph10(10)
  integer apmask(78),apsymbols(78)
  integer naptypes(0:5,4) ! (nQSOProgress, ipass)  maximum of 4 passes for now
  integer mcqru(29),mcqfd(29),mcqtest(29),mcqww(29)
  integer mcq(29),mrrr(19),m73(19),mrr73(19)
  logical lapcqonly,first
  data     mcq/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0/
  data   mcqru/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1,1,0,0,1,1,0,0/
  data   mcqfd/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,1,0,0,0,1,0/
  data mcqtest/0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,1,0,1,0,1,1,1,1,1,1,0,0,1,0/
  data   mcqww/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,1,0,1,1,1,1,0/
  data    mrrr/0,1,1,1,1,1,1,0,1,0,0,1,0,0,1,0,0,0,1/
  data     m73/0,1,1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,0,1/
  data   mrr73/0,1,1,1,1,1,1,0,0,1,1,1,0,1,0,1,0,0,1/
  data ncontest0/99/
  data first/.true./
  save naptypes,ncontest0

! nQSOprogress
!   0  CALLING
!   1  REPLYING
!   2  REPORT
!   3  ROGER_REPORT
!   4  ROGERS
!   5  SIGNOFF

  if(first.or.(ncontest.ne.ncontest0)) then
! iaptype
!------------------------
!   1        CQ     ???    ???           (29+4=33 ap bits)
!   2        MyCall ???    ???           (29+4=33 ap bits)
!   3        MyCall DxCall ???           (58+4=62 ap bits)
!   4        MyCall DxCall RRR           (78 ap bits)
!   5        MyCall DxCall 73            (78 ap bits)
!   6        MyCall DxCall RR73          (78 ap bits)

     naptypes(0,1:4)=(/1,2,0,0/) ! Tx6 selected (CQ)
     naptypes(1,1:4)=(/2,3,0,0/) ! Tx1
     naptypes(2,1:4)=(/2,3,0,0/) ! Tx2
     naptypes(3,1:4)=(/3,4,5,6/) ! Tx3
     naptypes(4,1:4)=(/3,4,5,6/) ! Tx4
     naptypes(5,1:4)=(/3,1,2,0/) ! Tx5
     first=.false.
     ncontest0=ncontest
  endif

  apsymbols=0
  iaptype=naptypes(nQSOProgress,ipass)
  if(lapcqonly) iaptype=1
  
! ncontest=0 : NONE
!          1 : NA_VHF
!          2 : EU_VHF
!          3 : FIELD DAY
!          4 : RTTY
!          5 : WW_DIGI 
!          6 : FOX
!          7 : HOUND

! Conditions that cause us to bail out of AP decoding
!  if(ncontest.le.5 .and. iaptype.ge.3 .and. (abs(f1-nfqso).gt.napwid .and. abs(f1-nftx).gt.napwid) ) goto 900
!  if(ncontest.eq.6) goto 900                      !No AP for Foxes
!  if(ncontest.eq.7.and.f1.gt.950.0) goto 900      !Hounds use AP only below 950 Hz
  if(ncontest.ge.6) goto 900
  if(iaptype.ge.2 .and. apsym0(1).gt.1) goto 900  !No, or nonstandard, mycall 
  if(ncontest.eq.7 .and. iaptype.ge.2 .and. aph10(1).gt.1) goto 900
  if(iaptype.ge.3 .and. apsym0(30).gt.1) goto 900 !No, or nonstandard, dxcall

  if(iaptype.eq.1) then ! CQ or CQ RU or CQ TEST or CQ FD
     apmask=0
     apmask(1:29)=1  
     if(ncontest.eq.0) apsymbols(1:29)=mcq
     if(ncontest.eq.1) apsymbols(1:29)=mcqtest
     if(ncontest.eq.2) apsymbols(1:29)=mcqtest
     if(ncontest.eq.3) apsymbols(1:29)=mcqfd
     if(ncontest.eq.4) apsymbols(1:29)=mcqru
     if(ncontest.eq.5) apsymbols(1:29)=mcqww
     if(ncontest.eq.7) apsymbols(1:29)=mcq
     apmask(75:78)=1 
     apsymbols(75:78)=(/0,0,1,0/)
  endif

  if(iaptype.eq.2) then ! MyCall,???,??? 
     apmask=0
     if(ncontest.eq.0.or.ncontest.eq.1.or.ncontest.eq.5) then
        apmask(1:29)=1  
        apsymbols(1:29)=apsym0(1:29)
        apmask(75:78)=1
        apsymbols(75:78)=(/0,0,1,0/)
     else if(ncontest.eq.2) then
        apmask(1:28)=1  
        apsymbols(1:28)=apsym0(1:28)
        apmask(72:74)=1
        apsymbols(72)=0
        apsymbols(73)=(+1)
        apsymbols(74)=0
        apmask(75:78)=1 
        apsymbols(75:78)=0
     else if(ncontest.eq.3) then
        apmask(1:28)=1  
        apsymbols(1:28)=apsym0(1:28)
        apmask(75:78)=1 
        apsymbols(75:78)=0
     else if(ncontest.eq.4) then
        apmask(2:29)=1  
        apsymbols(2:29)=apsym0(1:28)
        apmask(75:78)=1 
        apsymbols(75:78)=(/0,0,1,0/)
     else if(ncontest.eq.7) then ! ??? RR73; MyCall <Fox Call hash10> ???
        apmask(29:56)=1  
        apsymbols(29:56)=apsym0(1:28)
        apmask(57:66)=1
        apsymbols(57:66)=aph10(1:10)
        apmask(72:78)=1
        apsymbols(72:74)=(/0,0,1/)
        apsymbols(75:78)=0
     endif
  endif

  if(iaptype.eq.3) then ! MyCall,DxCall,??? 
     apmask=0
     if(ncontest.eq.0.or.ncontest.eq.1.or.ncontest.eq.2.or.ncontest.eq.5.or.ncontest.eq.7) then
        apmask(1:58)=1  
        apsymbols(1:58)=apsym0
        apmask(75:78)=1
        apsymbols(75:78)=(/0,0,1,0/)
     else if(ncontest.eq.3) then ! Field Day
        apmask(1:56)=1  
        apsymbols(1:28)=apsym0(1:28)
        apsymbols(29:56)=apsym0(30:57)
        apmask(72:78)=1 
        apsymbols(75:78)=0
     else if(ncontest.eq.4) then 
        apmask(2:57)=1  
        apsymbols(2:29)=apsym0(1:28)
        apsymbols(30:57)=apsym0(30:57)
        apmask(75:78)=1 
        apsymbols(75:78)=(/0,0,1,0/)
     endif
  endif

  if(iaptype.eq.5.and.ncontest.eq.7) goto 900 !Hound
  if(iaptype.eq.4 .or. iaptype.eq.5 .or. iaptype.eq.6) then  
     apmask=0
     if(ncontest.le.5 .or. (ncontest.eq.7.and.iaptype.eq.6)) then
        apmask(1:78)=1                      !MyCall, HisCall, RRR|73|RR73
        apmask(72:74)=0                     !Check for <blank>, RRR, RR73, 73
        apsymbols(1:58)=apsym0
        if(iaptype.eq.4) apsymbols(59:77)=mrrr
        if(iaptype.eq.5) apsymbols(59:77)=m73
        if(iaptype.eq.6) apsymbols(59:77)=mrr73
     else if(ncontest.eq.7.and.iaptype.eq.4) then ! Hound listens for MyCall RR73;...
        apmask(1:28)=1
        apsymbols(1:28)=apsym0(1:28)
        apmask(57:66)=1
        apsymbols(57:66)=aph10(1:10)
        apmask(72:78)=1
        apsymbols(72:78)=(/0,0,1,0,0,0,0/)
     endif
  endif

900 return
end subroutine q65_ap
