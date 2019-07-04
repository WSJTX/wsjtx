program QRA64code

! Provides examples of message packing, bit and symbol ordering,
! QRA (63,12) encoding, and other necessary details of the QRA64
! protocol.  

  use packjt
  character*22 msg,msg0,msg1,decoded,cok*3,msgtype*10
  integer dgen(12),sent(63)
  integer icos7(0:6)
  data icos7/2,5,6,0,4,1,3/     !Defines a 7x7 Costas array

  include 'testmsg.f90'

  nargs=iargc()
  if(nargs.lt.1) then
     print*,'Usage: qra64code "message"'
     print*,'       qra64code -t'
     go to 999
  endif

  call getarg(1,msg)                     !Get message from command line
  nmsg=1
  if(msg(1:2).eq."-t") nmsg=NTEST

  write(*,1010)
1010 format("     Message                 Decoded                Err? Type"/74("-"))

  do imsg=1,nmsg
     if(nmsg.gt.1) msg=testmsg(imsg)
    call fmtmsg(msg,iz)                     !To upper, collapse mult blanks
     msg0=msg                               !Input message
     call chkmsg(msg,cok,nspecial,flip)     !See if it includes "OOO" report
     msg1=msg                               !Message without "OOO"
     call packmsg(msg1,dgen,itype)          !Pack message into 12 six-bit bytes
     msgtype=""
     if(itype.eq.1) msgtype="Std Msg"
     if(itype.eq.2) msgtype="Type 1 pfx"
     if(itype.eq.3) msgtype="Type 1 sfx"
     if(itype.eq.4) msgtype="Type 2 pfx"
     if(itype.eq.5) msgtype="Type 2 sfx"
     if(itype.eq.6) msgtype="Free text"

     call qra64_enc(dgen,sent)              !Encode using QRA64

     call unpackmsg(dgen,decoded)           !Unpack the user message
     call fmtmsg(decoded,iz)
     ii=imsg
     write(*,1020) ii,msg0,decoded,itype,msgtype
1020 format(i4,1x,a22,2x,a22,4x,i3,": ",a13)
  enddo

  if(nmsg.eq.1) then
     write(*,1030) dgen
1030 format(/'Packed message, 6-bit symbols ',12i3) !Display packed symbols

     write(*,1040) sent
1040 format(/'Information-carrying channel symbols'/(i5,29i3))

     write(*,1050) 10*icos7,sent(1:32),10*icos7,sent(33:63),10*icos7
1050 format(/'Channel symbols including sync'/(i5,29i3))
  endif
  
999 end program QRA64code
