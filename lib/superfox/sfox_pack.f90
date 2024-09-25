subroutine sfox_pack(line,ckey,bMoreCQs,bSendMsg,freeTextMsg,xin)

  use qpc_mod
  use packjt
  use packjt77
  use julian
  parameter (NQU1RKS=203514677)
  integer*8 n47,n58,now
  integer*1 xin(0:49)                    !Packed message as 7-bit symbols
  logical*1 bMoreCQs,bSendMsg
  logical text,allz
  character*120 line                     !SuperFox message pieces
  character*10 ckey
  character*26 freeTextMsg
  character*13 w(16)
  character*11 c11
  character*329 msgbits                  !Packed message as bits
  character*38 c
  data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/'/

  i0=index(line,'/')
  i3=0                                   !Default to i3=0, standard message
  nh1=0                                      !Number of Hound calls with RR73 
  nh2=0                                      !Number of Hound calls with report

! Split the command line into words
  w=' '
  i1=1
  do j=1,16
     do i=i1,min(i1+12,120)
        if(line(i:i).eq.' ') exit
     enddo
     i2=i-1
     w(j)=line(i1:i2)
     do i=i2+1,120
        if(line(i:i).ne.' ') exit
     enddo
     i1=i
     if(i1.ge.120) exit
  enddo
  nwords=j

  do i=1,nwords
     if(w(i)(1:1).eq.' ') then
        nwords=i-1
        exit
     endif
  enddo

  do i=1,329                             !Set all msgbits to '0'
     msgbits(i:i)='0'
  enddo

  now=itime8()/30
  now=30*now
  read(ckey(5:10),*) notp

  write(msgbits(307:326),'(b20.20)') notp  !Insert the digital signature

  if(w(1)(1:3).eq.'CQ ') then
     i3=3
     c11=w(2)(1:11)
     n58=0
     do i=1,11
        n58=n58*38 + index(c,c11(i:i)) - 1
     enddo
     write(msgbits(1:58),'(b58.58)') n58
     call packgrid(w(3)(1:4),n15,text)
     write(msgbits(59:73),'(b15.15)') n15
     write(msgbits(327:329),'(b3.3)') i3     !Message type i3=3
     go to 800
  endif

  call pack28(w(1),n28)                      !Fox call
  write(msgbits(1:28),'(b28.28)') n28

! Default report is RR73 if we're also sending a free text message.
  if(bSendMsg) msgbits(141:160)='11111111111111111111'
  
  j=29
  
! Process callsigns with RR73
  do i=2,nwords
     if(w(i)(1:1).eq.'+' .or. w(i)(1:1).eq.'-') cycle     !Skip report words
     i1=min(i+1,nwords)
     if(w(i1)(1:1) .eq.'+' .or. w(i1)(1:1).eq.'-') cycle  !Skip if i+1 is report
     call pack28(w(i),n28)
     write(msgbits(j:j+27),1002) n28         !Insert this call for RR73 message
1002 format(b28.28)
     j=j+28
     nh1=nh1+1
     if(nh1.ge.5) exit                       !At most 5 RR73 callsigns
  enddo
  
! Process callsigns with a report
  j=169
  j2=281
  if(bSendMsg) then
     i3=2
     j=29 + 28*nh1
     j2=141 + 5*nh1
  endif

  do i=2,nwords
     i1=min(i+1,nwords)
     if(w(i1)(1:1).eq.'+' .or. w(i1)(1:1).eq.'-') then
        call pack28(w(i),n28)
        write(msgbits(j:j+27),1002) n28       !Insert this call 
        read(w(i1),*) n              !Valid reports are -18 to +12, plus RR73
        if(n.lt.-18) n=-18           !... Even numbers only ...
        if(n.gt.12) n=12
        write(msgbits(j2:j2+4),1000) n+18
1000    format(b5.5)
        w(i1)=""
        nh2=nh2+1
!        print*,'C',i3,i,j,n,w(i)
        if( nh2.ge.4 .or. (nh1+nh2).ge.9 ) exit  ! At most 4 callsigns w/reports
        j=j+28
        j2=j2+5
     endif
  enddo

800 if(bSendMsg) then
     i1=26
     do i=1,26
        if(freeTextMsg(i:i).ne.' ') i1=i
     enddo
     do i=i1+1,26
        freeTextMsg(i:i)='.'
     enddo
     if(i3.eq.3) then
        call packtext77(freeTextMsg(1:13),msgbits(74:144))
        call packtext77(freeTextMsg(14:26),msgbits(145:215))
     elseif(i3.eq.2) then
        call packtext77(freeTextMsg(1:13),msgbits(161:231))
        call packtext77(freeTextMsg(14:26),msgbits(232:302))
     endif
     write(msgbits(327:329),'(b3.3)') i3     !Message type i3=2
  endif
  if(bMoreCQs) msgbits(306:306)='1'

  read(msgbits(327:329),'(b3)') i3
  if(i3.eq.0) then
     do i=1,9
        i0=i*28 + 1
        read(msgbits(i0:i0+27),'(b28)') n28
        if(n28.eq.0) write(msgbits(i0:i0+27),'(b28.28)') NQU1RKS
     enddo
  else if(i3.eq.3) then
     allz=.true.
     do i=0,6
        i0=i*32 + 74
        read(msgbits(i0:i0+31),'(b32)') n32
        if(n32.ne.0) allz=.false.
     enddo
     if(allz) then
        do i=0,6
           i0=i*32 + 74
           write(msgbits(i0:i0+31),'(b32.32)') NQU1RKS
        enddo
     endif
  endif

  read(msgbits,1004) xin(0:46)
1004 format(47b7)

  mask21=2**21 - 1
  n47=47
  ncrc21=iand(nhash2(xin,n47,571),mask21)     !Compute 21-bit CRC
  xin(47)=ncrc21/16384                       !First 7 of 21 bits
  xin(48)=iand(ncrc21/128,127)               !Next 7 bits 
  xin(49)=iand(ncrc21,127)                   !Last 7 bits
  
  xin=xin(49:0:-1)                           !Reverse the symbol order
! NB: CRC is now in first three symbols, fox call in the last four.

  return
end subroutine sfox_pack
