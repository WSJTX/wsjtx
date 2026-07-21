subroutine sfox_pack(line,ckey,bMoreCQs,bSendMsg,freeTextMsg,xin,pack_error)

  use qpc_mod
  use packjt
  use packjt77
  parameter (NQU1RKS=203514677)
  integer*8 n47,n58
  integer*1 xin(0:49)                    !Packed message as 7-bit symbols
  logical*1 bMoreCQs,bSendMsg
  integer pack_error
  integer, parameter :: SFOX_PACK_OK=0
  integer, parameter :: SFOX_PACK_BAD_TOKEN=1
  integer, parameter :: SFOX_PACK_BAD_OTP=2
  integer, parameter :: SFOX_PACK_BAD_CQ=3
  integer, parameter :: SFOX_PACK_BAD_CALL=4
  integer, parameter :: SFOX_PACK_BAD_REPORT=5
  integer, parameter :: SFOX_PACK_BAD_FREE_TEXT=6
  logical text,allz,split_success
  character*120 line                     !SuperFox message pieces
  character*10 ckey
  character*26 freeTextMsg
  character*13 w(16)
  character*13 w1,w2,w3                  !Scalar copies of w() elements passed to
                                          !explicit-interface functions/subroutines
                                          !below: gfortran's -Wcharacter-truncation
                                          !treats an array-element actual argument
                                          !as spanning the rest of w()'s storage
                                          !sequence, so pass a same-length scalar
                                          !instead to give it an exact length
  character*11 c11
  character*329 msgbits                  !Packed message as bits
  character*38 c
  data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/'/

  pack_error=SFOX_PACK_BAD_TOKEN
  xin=0
  i3=0                                   !Default to i3=0, standard message
  nh1=0                                      !Number of Hound calls with RR73 
  nh2=0                                      !Number of Hound calls with report

! Split the command line into words
  call split_sfox_line(line,w,nwords,split_success)
  if(.not.split_success .or. nwords.lt.1) then
     pack_error=SFOX_PACK_BAD_TOKEN
     return
  endif

  do i=1,329                             !Set all msgbits to '0'
     msgbits(i:i)='0'
  enddo

  read(ckey(5:10),*,err=910) notp

  write(msgbits(307:326),'(b20.20)') notp  !Insert the digital signature

  if(w(1)(1:3).eq.'CQ ') then
     if(nwords.ne.3) then
        pack_error=SFOX_PACK_BAD_CQ
        return
     endif
     w2=w(2)
     w3=w(3)
     if(.not.valid_sfox_cq_call(w2) .or. .not.valid_sfox_grid4(w3)) then
        pack_error=SFOX_PACK_BAD_CQ
        return
     endif
     i3=3
     c11=w2(1:11)
     n58=0
     do i=1,11
        n58=n58*38 + index(c,c11(i:i)) - 1
     enddo
     write(msgbits(1:58),'(b58.58)') n58
     call packgrid(w3(1:4),n15,text)
     if(text) then
        pack_error=SFOX_PACK_BAD_CQ
        return
     endif
     write(msgbits(59:73),'(b15.15)') n15
     write(msgbits(327:329),'(b3.3)') i3     !Message type i3=3
     go to 800
  endif

  w1=w(1)
  if(.not.valid_sfox_call(w1)) then
     pack_error=SFOX_PACK_BAD_CALL
     return
  endif
  call pack28(w1,n28)                      !Fox call
  write(msgbits(1:28),'(b28.28)') n28

  nrr73_total=0
  nreport_total=0
  i=2
  do while(i.le.nwords)
     if(w(i)(1:1).eq.'+' .or. w(i)(1:1).eq.'-') then
        pack_error=SFOX_PACK_BAD_TOKEN
        return
     endif
     if(.not.valid_sfox_call(w(i))) then
        pack_error=SFOX_PACK_BAD_CALL
        return
     endif
     i1=i+1
     if(i1.le.nwords .and. &
          (w(i1)(1:1).eq.'+' .or. w(i1)(1:1).eq.'-')) then
        if(.not.valid_sfox_report(w(i1),n)) then
           pack_error=SFOX_PACK_BAD_REPORT
           return
        endif
        nreport_total=nreport_total+1
        i=i+2
     else
        nrr73_total=nrr73_total+1
        i=i+1
     endif
  enddo

  if(bSendMsg) then
     if(nrr73_total+nreport_total.gt.4 .or. nreport_total.gt.4) then
        pack_error=SFOX_PACK_BAD_TOKEN
        return
     endif
  else if(nrr73_total.gt.5 .or. nreport_total.gt.4 .or. &
       nrr73_total+nreport_total.gt.9) then
     pack_error=SFOX_PACK_BAD_TOKEN
     return
  endif

! Default report is RR73 if we're also sending a free text message.
  if(bSendMsg) msgbits(141:160)='11111111111111111111'
  
  j=29
  
! Process callsigns with RR73
  do i=2,nwords
     if(w(i)(1:1).eq.'+' .or. w(i)(1:1).eq.'-') cycle     !Skip report words
     i1=min(i+1,nwords)
     if(w(i1)(1:1) .eq.'+' .or. w(i1)(1:1).eq.'-') cycle  !Skip if i+1 is report
     if(.not.valid_sfox_call(w(i))) then
        pack_error=SFOX_PACK_BAD_CALL
        return
     endif
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
        if(.not.valid_sfox_call(w(i))) then
           pack_error=SFOX_PACK_BAD_CALL
           return
        endif
        call pack28(w(i),n28)
        write(msgbits(j:j+27),1002) n28       !Insert this call 
        if(.not.valid_sfox_report(w(i1),n)) then
           pack_error=SFOX_PACK_BAD_REPORT
           return
        endif
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
     if(.not.valid_sfox_free_text(freeTextMsg)) then
        pack_error=SFOX_PACK_BAD_FREE_TEXT
        return
     endif
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

  pack_error=SFOX_PACK_OK
900 continue
  return
910 pack_error=SFOX_PACK_BAD_OTP
  go to 900

contains

  subroutine split_sfox_line(text,words,nwords,success)
    character*120 text
    character*13 words(16)
    integer nwords
    logical success
    integer first,last

    words=' '
    nwords=0
    success=.true.
    first=1
    do while(first.le.120)
       do while(first.le.120)
          if(text(first:first).ne.' ') exit
          first=first+1
       enddo
       if(first.gt.120) exit

       last=first
       do while(last.le.120)
          if(text(last:last).eq.' ') exit
          last=last+1
       enddo
       if(last-first.gt.13) then
          success=.false.
          return
       endif
       if(nwords.ge.16) then
          success=.false.
          return
       endif
       nwords=nwords+1
       words(nwords)=text(first:last-1)
       first=last+1
    enddo
  end subroutine split_sfox_line

  logical function valid_sfox_call(call)
    character*13 call
    character*13 decoded
    logical success
    integer n28

    valid_sfox_call=.false.
    call pack28(call,n28)
    call unpack28(n28,decoded,success)
    if(.not.success) return
    valid_sfox_call=trim(decoded).eq.trim(call)
    return
  end function valid_sfox_call

  logical function valid_sfox_cq_call(callsign)
    character*13 callsign
    integer n

    valid_sfox_cq_call=.false.
    n=len(trim(callsign))
    if(n.lt.3 .or. n.gt.11) return
    do i=1,n
       if(index(c(2:38),callsign(i:i)).eq.0) return
    enddo

    valid_sfox_cq_call=.true.
    return
  end function valid_sfox_cq_call

  logical function valid_sfox_grid4(grid)
    character*13 grid

    valid_sfox_grid4=len(trim(grid)).eq.4 .and.                        &
         grid(1:1).ge.'A' .and. grid(1:1).le.'R' .and.                 &
         grid(2:2).ge.'A' .and. grid(2:2).le.'R' .and.                 &
         grid(3:3).ge.'0' .and. grid(3:3).le.'9' .and.                 &
         grid(4:4).ge.'0' .and. grid(4:4).le.'9'
    return
  end function valid_sfox_grid4

  logical function valid_sfox_report(word,n)
    character*13 word
    integer n

    valid_sfox_report=.false.
    read(word,*,err=910,end=910) n
    if(n.lt.-18 .or. n.gt.12) return
    valid_sfox_report=.true.
910 return
  end function valid_sfox_report

  logical function valid_sfox_free_text(text)
    character*26 text
    character*42 text_chars
!   Authoritative free-text charset; the GUI precheck in on_pbFreeText_clicked
!   (widgets/mainwindow_slots.cpp) must mirror this string.
    data text_chars/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?'/

    valid_sfox_free_text=.true.
    do i=1,26
       if(index(text_chars,text(i:i)).eq.0) then
          valid_sfox_free_text=.false.
          return
       endif
    enddo
    return
  end function valid_sfox_free_text
end subroutine sfox_pack
