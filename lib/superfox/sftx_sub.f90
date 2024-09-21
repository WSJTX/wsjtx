subroutine sftx_sub(ckey0)

! This routine is required in order to create a SuperFox transmission.

! The present version goes through the following steps:
!   1. Read old-style Fox messages from file 'sfox_1.dat' in the WSJT-X 
!      writable data directory.
!   2. Parse up to NSlots=5 messages to extract MyCall, up to 9 Hound
!      calls, and the report or RR73 to be sent to each Hound.
!   3. Assemble and encode a single SuperFox message to produce itone(1:151),
!      the array of channel symbol values.
!   4. Write the contents of array itone to file 'sfox_2.dat'.

  use qpc_mod
  use sfox_mod
  character*120 line                  !List of SuperFox message pieces
  character*40 cmsg                   !Old-style Fox messages
  character*26 freeTextMsg
  character*(*) ckey0
  character*10 ckey
  character*11 foxcall
  logical*1 bMoreCQs,bSendMsg
  integer*1 xin(0:49)                 !Packed message as 7-bit symbols
  integer*1 y(0:127)                  !Encoded symbols as i*1 integers
  integer chansym0(127)               !Transmitted symbols, data only
  integer isync(24)                   !Symbol numbers for sync tones
  common/foxcom3/nslots,cmsg(5),itone(151)
  data isync/1,2,4,7,11,16,22,29,37,39,42,43,45,48,52,57,63,70,78,80,  &
             83,84,86,89/

  ckey=ckey0

  fsample=12000.0
  call sfox_init(7,127,50,'no',fspread,delay,fsample,24)
  freeTextMsg='                          '
  bMoreCQs=cmsg(1)(40:40).eq.'1'
  bSendMsg=cmsg(nslots)(39:39).eq.'1'
  if(bSendMsg) then
     freeTextMsg=cmsg(nslots)(1:26)
     if(nslots.gt.4) nslots=4
  endif

  call foxgen2(nslots,cmsg,line,foxcall)    !Parse old-style Fox messages

! Pack message information and CRC into xin(0:49)
  call sfox_pack(line,ckey,bMoreCQs,bSendMsg,freeTextMsg,xin)
  call qpc_encode(y,xin)                    !Encode the message to 128 symbols
  y=cshift(y,1)                             !Puncture the code by removing y(0)
  y(127)=0
  chansym0=y(0:126)

! Create the full itone sequence containing both data and sync symbols
  j=1
  k=0
  do i=1,NDS
     if(j.le.NS .and. i.eq.isync(j)) then
        if(j.lt.NS) j=j+1       !Index for next sync symbol
        itone(i)=0              !Insert sync symbol at tone 0
     else
        k=k+1
        itone(i)=chansym0(k) + 1    !Symbol value 0 transmitted as tone 1, etc.
     endif
  enddo

end subroutine sftx_sub
