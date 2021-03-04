program ft8code

! Provides examples of message packing, LDPC(174,91) encoding, bit and
! symbol ordering, and other details of the FT8 protocol.

  use packjt77
  include 'ft8_params.f90'               !Set various constants
  include 'ft8_testmsg.f90'
  parameter (NWAVE=NN*NSPS)
 
  character*37 msg,msgsent
  character*9 comment
  character bad*1,msgtype*18
  integer itone(NN)
  integer*1 msgbits(77),codeword(174)
  logical short

! Get command-line argument(s)
  nargs=iargc()
  if(nargs.ne.1 .and. nargs.ne.3) then
     print*
     print*,'Program ft8code:  Provides examples of message packing, ',       &
          'LDPC(174,91) encoding,'
     print*,'bit and symbol ordering, and other details of the FT8 protocol.'
     print*
     print*,'Usage: ft8code [-c grid] "message"  # Results for specified message'
     print*,'       ft8code -T                   # Examples of all message types'
     print*,'       ft8code -t                   # Short format examples'
     go to 999
  endif

  call getarg(1,msg)                    !Message to be transmitted
  short=.false.
  if(len(trim(msg)).eq.2 .and. (msg(1:2).eq.'-T' .or. msg(1:2).eq.'-t')) then
     nmsg=NTEST
     short=msg(1:2).eq.'-t'
  else
     call fmtmsg(msg,iz)          !To upper case; collapse multiple blanks
     nmsg=1
  endif

  if(.not.short) write(*,1010)
1010 format(4x,'Message',31x,'Decoded',29x,'Err i3.n3'/100('-')) 

  do imsg=1,nmsg
     if(nmsg.gt.1) msg=testmsg(imsg)
     
! Generate msgsent, msgbits, and itone
     i3=-1
     n3=-1
     call genft8(msg,i3,n3,msgsent,msgbits,itone)
     call encode174_91(msgbits,codeword)
     msgtype=""
     if(i3.eq.0) then
        if(n3.eq.0) msgtype="Free text"
        if(n3.eq.1) msgtype="DXpedition mode"
        if(n3.eq.2) msgtype="EU VHF Contest"
        if(n3.eq.3) msgtype="ARRL Field Day"
        if(n3.eq.4) msgtype="ARRL Field Day"
        if(n3.eq.5) msgtype="Telemetry"
        if(n3.ge.6) msgtype="Undefined type"
     endif 
     if(i3.eq.1) msgtype="Standard msg"
     if(i3.eq.2) msgtype="EU VHF Contest"
     if(i3.eq.3) msgtype="ARRL RTTY Roundup"
     if(i3.eq.4) msgtype="Nonstandard call"
     if(i3.eq.5) msgtype="EU VHF Contest"
     if(i3.ge.6) msgtype="Undefined type"
     if(i3.ge.1) n3=-1
     bad=" "
     comment='         '
     if(msg.ne.msgsent) bad="*"
     if(short) then
        if(n3.ge.0) then
           write(*,1020) i3,n3,msg,bad,msgtype
1020       format(i1,'.',i1,2x,a37,1x,a1,1x,a18)
        else
           write(*,1022) i3,msg,bad,msgtype
1022       format(i1,'.',3x,a37,1x,a1,1x,a18)
        endif
     else
        if(n3.ge.0) then
           write(*,1024) imsg,msg,msgsent,bad,i3,n3,msgtype,comment
1024       format(i2,'.',1x,a37,1x,a37,1x,a1,2x,i1,'.',i1,1x,a18,1x,a9)
        else
           write(*,1026) imsg,msg,msgsent,bad,i3,msgtype,comment
1026       format(i2,'.',1x,a37,1x,a37,1x,a1,2x,i1,'.',1x,1x,a18,1x,a9)
        endif
     endif
  enddo

  if(nmsg.eq.1) then
     write(*,1030) msgbits
1030 format(/'Source-encoded message, 77 bits: ',/77i1)
     write(*,1031) codeword(78:91)
1031 format(/'14-bit CRC: ',/14i1)
     write(*,1032) codeword(92:174)
1032 format(/'83 Parity bits: ',/83i1)     
     write(*,1034) itone
1034 format(/'Channel symbols (79 tones):'/                             &
          '  Sync ',14x,'Data',15x,'Sync',15x,'Data',15x,'Sync'/        &
           7i1,1x,29i1,1x,7i1,1x,29i1,1x,7i1)
  endif

999 end program ft8code
