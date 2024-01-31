program cablog

  character*100 line,infile,outfile
  character cband*4,cmode*2,cdate*10,cutc*4,callsign*10,mycall*10
  character csent*4,crcvd*4,dsent*4,drcvd*4,g1*4
  character*3 cmo(12)
  integer icomma(20)
  logical map65
  logical isgrid,gridx
  data cmo/'Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep',   &
           'Oct','Nov','Dec'/

  isgrid(g1)=g1(1:1).ge.'A' .and. g1(1:1).le.'R' .and. g1(2:2).ge.'A' .and. &
       g1(2:2).le.'R' .and. g1(3:3).ge.'0' .and. g1(3:3).le.'9' .and.       &
       g1(4:4).ge.'0' .and. g1(4:4).le.'9' .and. g1(1:4).ne.'RR73'

  nargs=iargc()
  if(nargs.ne.4) then
     print*,"Program cablog converts file 'wsjtx.log' written by WSJT-X"
     print*,"(or wsjt.log written by MAP65) to a bare-bones Cabrillo"
     print*,"log for the ARRL International EME Contest. You will"
     print*,"certainly need to edit the header information, and you may"
     print*,"edit the log elsewhere as required."
     print*,' '
     print*,'Usage:    cablog <MyCall> <sent> <rcvd> <infile>'
     print*,'Examples: cablog   W2ZQ    -15    -16  wsjtx.log'
     print*,'          cablog   W2ZQ     O      O   wsjt.log'
     go to 999
  endif
  call getarg(1,mycall)
  outfile=trim(mycall)//'.log'
  call getarg(2,dsent)
  gridx=isgrid(dsent)
  call getarg(3,drcvd)
  call getarg(4,infile)
  open(10,file=trim(infile),status='old')
  open(12,file=trim(outfile),status='unknown')

  write(12,1000)
1000 format('START-OF-LOG: 3.0'/           &
          'CONTEST: ARRL-EME'/             &
          'CALLSIGN: '/                    &
          'CATEGORY-OPERATOR: '/           &
          'CATEGORY-BAND: '/               &
          'CATEGORY-MODE: '/               &
          'EMAIL: '/                       &
          'OPERATORS: '/                   &
          'CATEGORY-POWER: HIGH'/          &
          'CATEGORY-TRANSMITTER: ONE'/     &
          'CATEGORY-STATION: FIXED'/       &
          'CATEGORY-TIME: 24-HOURS'/       &
          'CATEGORY-ASSISTED: ASSISTED'/   &
          'LOCATION: SNJ'/                 &
          'CLAIMED-SCORE: '/               &
          'CLUB: '/                        &
          'NAME: '/                        &
          'ADDRESS: '/                     &
          'ADDRESS: '/                     &
          'ADDRESS: '/                     &
          'CREATED-BY: cablog (C) K1JT')        

  n=0
  map65=.false.
  
  do nn=1,9999
     read(10,'(a100)',end=900) line
     if(len(trim(line)).eq.0) cycle
     if(line(6:6).ge.'A' .and. line(6:6).le.'Z') map65=.true.
     n=n+1
     k=0
     do j=1,100
        if(line(j:j).eq.',') then
           k=k+1
           icomma(k)=j
        endif
     enddo
     cmode='DG'
     if(index(line,',CW,').gt.10) cmode='CW'

     if(map65) then
        do i=1,12
           if(cmo(i).eq.line(6:8)) write(cdate(6:7),'(i2.2)') i
        enddo
        cdate(1:5)=line(1:5)
        cdate(8:10)=line(9:11)
        cutc=line(13:14)//line(16:17)
        i0=index(line(19:),',')
        callsign=line(19:17+i0)
        cband='144 '
        csent=dsent
        crcvd=drcvd
     else
        cdate=line(1:10)
        cutc=line(32:33)//line(35:36)
        i0=index(line(41:),',')
        callsign=line(41:39+i0)
        read(line(icomma(6)+1:icomma(7)-1),*,err=10,end=10) freq
        go to 20
10      print*,'***Error at line ',n
        print*,trim(line)     
20      if(freq.ge.50.0 .and. freq.le.54.0) cband='50  '
        if(freq.ge.144.0 .and. freq.le.148.0) cband='144 '
        if(freq.ge.28.0 .and. freq.le.29.0) cband='144 '
        if(freq.ge.222.0 .and. freq.le.225.0) cband='222 '
        if(freq.ge.420.0 .and. freq.le.450.0) cband='432 '
        if(freq.ge.902.0 .and. freq.le.928.0) cband='902 '
        if(freq.ge.1240.0 .and. freq.le.1300.0) cband='1.2G'
        if(freq.ge.2300.0 .and. freq.le.2450.0) cband='2.3G'
        if(freq.ge.3300.0 .and. freq.le.3500.0) cband='3.4G'
        if(freq.ge.5650.0 .and. freq.le.5925.0) cband='5.7G'
        if(freq.ge.10000.0 .and. freq.le.10500.0) cband='10G '
        if(freq.ge.24000.0 .and. freq.le.24250.0) cband='24G '
        if(icomma(8).eq.icomma(9)-1) then
           csent=dsent
        else
           csent=line(icomma(8)+1:icomma(9)-1)
        endif
        if(icomma(9).eq.icomma(10)-1) then
           crcvd=drcvd
        else
           crcvd=line(icomma(9)+1:icomma(10)-1)
        endif
     endif
     if(gridx) then
        csent=dsent
        crcvd=line(icomma(5)+1:icomma(6)-1)
     endif
     
     write(12,1030) cband,cmode,cdate,cutc,mycall,csent,callsign,crcvd
1030 format('QSO: ',a4,1x,a2,1x,a10,1x,a4,1x,a6,1x,a4,4x,a10,1x,a4)
  enddo

900 write(12,1900)
1900 format('END-OF-LOG:')
  write(*,1910) n,trim(outfile)
1910 format('Processed',i5,' QSOs.'/'Output file: ',a)

999 end program cablog
     
!2023-10-28,00:17:00,2023-10-28,00:21:00,G7TZZ,IO92,1296.083100,Q65,-17,-17,,,,
!2023-Nov-30,20:31,W8WN,EM77,0,Q65A
  
