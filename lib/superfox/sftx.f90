program sftx

! This program is required in order to create a SuperFox transmission.

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
  character*120 fname                 !Corrected path for sfox_1.dat
  character*120 line                  !List of SuperFox message pieces
  character*40 cmsg(5)                !Old-style Fox messages
  character*26 freeTextMsg
  character*2 arg
  character*10 ckey
!  character*9 foxkey
  character*11 foxcall0,foxcall
  logical*1 bMoreCQs,bSendMsg
  integer pack_error
  logical crc_ok
  real py(0:127,0:127)                !Probabilities for received synbol values
  integer*8 n47
  integer itone(151)                  !SuperFox channel-symbol values
  integer*1 xin(0:49)                 !Packed message as 7-bit symbols
  integer*1 xdec(0:49)                !Decoded message
  integer*1 y(0:127)                  !Encoded symbols as i*1 integers
  integer*1 ydec(0:127)               !Decoded codeword
  integer*1 yy(0:10)
  integer chansym0(127)               !Transmitted symbols, data only
  integer chansym(127)                !Received symbols, data only
  integer isync(24)                   !Symbol numbers for sync tones
  data isync/1,2,4,7,11,16,22,29,37,39,42,43,45,48,52,57,63,70,78,80,  &
             83,84,86,89/
  include 'gtag.f90'

  narg=iargc()
  if(narg.ne.3) then
     print '(" Git tag: ",z9)',ntag
     go to 999
  endif

! sftx <message_file_name> <foxcall> <ckey>
  call getarg(1,fname)
  do i=1,len(trim(fname))
     if(fname(i:i).eq.'\\') fname(i:i)='/'
  enddo
  call getarg(2,foxcall0)
  call getarg(3,ckey)

!  if((foxkey(foxcall0).ne.ckey).and.(INDEX(ckey,'OTP:').eq.0)) then ! neither kind
!     itone=-99
!     go to 100
!  endif

  fsample=12000.0
  call sfox_init(7,127,50,'no',fspread,delay,fsample,24)
  open(25,file=trim(fname),status='unknown')
  do i=1,5
     read(25,1000,end=10) cmsg(i)
1000 format(a40)
  enddo
  i=6
10 close(25)
  nslots=i-1
  freeTextMsg='                          '
  bMoreCQs=cmsg(1)(40:40).eq.'1'
  bSendMsg=cmsg(nslots)(39:39).eq.'1'
  nDataSlots=nslots
  if(bSendMsg) then
     freeTextMsg=cmsg(nslots)(1:26)
     nDataSlots=nslots-1
     if(nDataSlots.gt.4) nDataSlots=4
  endif

  call foxgen2(nDataSlots,cmsg,line,foxcall)    !Parse old-style Fox messages

! Pack message information and CRC into xin(0:49)
  call sfox_pack(line,ckey,bMoreCQs,bSendMsg,freeTextMsg,xin,pack_error)
  if(pack_error.ne.0) then
     itone=-99
     go to 100
  endif
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

100 i1=max(index(fname,'sfox_1'),1)
  fname(i1:i1+9)='sfox_2.dat'
  open(25,file=trim(fname),status='unknown')
  write(25,1100) itone
1100 format(20i4)
  close(25)

999 end program sftx
