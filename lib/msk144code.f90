program msk144code

! Provides examples of message packing, bit and symbol ordering,
! LDPC encoding, and other necessary details of the MSK144 protocol.

  use packjt77
  character*77 c77
  character msg*37,msgsent*37,bad*1,msgtype*18
  integer*4 i4tone(144)
  include 'msk144_testmsg.f90'

  nargs=iargc()
  if(nargs.ne.1) then
     print*,'Usage: msk144code "message"'
     print*,'       msk144code -t'
     print*,' '
     print*,'Examples:'
     print*,'       msk144code "KA1ABC WB9XYZ EN37"'
     print*,'       msk144code "<KA1ABC WB9XYZ> R-03"'
     print*,'       msk144code "KA1ABC WB9XYZ R EN37"'
     go to 999
  endif

  call getarg(1,msg)
  nmsg=1
  if(msg(1:2).eq."-t") then
     nmsg=NTEST
  endif

  write(*,1010)
1010 format(4x,"Message",31x,"Decoded",29x,"Err i3.n3"/100("-")) 

  do imsg=1,nmsg
     if(nmsg.gt.1) msg=testmsg(imsg)
     call fmtmsg(msg,iz)                !To upper case, collapse multiple blanks
     call genmsk_128_90(msg,ichk,msgsent,i4tone,itype)
     i3=-1
     n3=-1
     call pack77(msg,i3,n3,c77)
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
     if(i3.eq.4) msgtype="Nonstandard calls"
     if(i3.eq.5) msgtype="EU VHF Contest"
     if(i3.ge.6) msgtype="Undefined msg type"
     if(i3.ge.1) n3=-1
     if(i4tone(41).lt.0) then
        msgtype="Sh msg"
        i3=-1
     endif
     bad=" "
     if(msg.ne.msgsent) bad="*"
     if(i3.eq.0.and.n3.ge.0) then
        write(*,1020) imsg,msg,msgsent,bad,i3,n3,msgtype
1020    format(i2,'.',1x,a37,1x,a37,1x,a1,2x,i1,'.',i1,1x,a18)
     elseif(i3.ge.1) then
        write(*,1022) imsg,msg,msgsent,bad,i3,msgtype
1022    format(i2,'.',1x,a37,1x,a37,1x,a1,2x,i1,'.',1x,1x,a18)
     elseif(i3.lt.0) then
        write(*,1024) imsg,msg,msgsent,bad,msgtype
1024    format(i2,'.',1x,a37,1x,a37,1x,a1,6x,a18)
     endif

  enddo

  if(nmsg.eq.1) then
     n=144
     if(i4tone(41).lt.0) n=40
     write(*,1030) i4tone(1:n)
1030 format(/'Channel symbols'/(72i1))
  endif

999 end program msk144code
