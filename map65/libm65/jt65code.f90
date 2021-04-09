program JT65code

! Provides examples of message packing, bit and symbol ordering,
! Reed Solomon encoding, and other necessary details of the JT65
! protocol.

  character*22 msg0,msg,decoded,cok*3
  integer dgen(12),sent(63),recd(12),era(51)

  nargs=iargc()
  if(nargs.ne.1) then
     print*,'Usage: JT65code "message"'
     go to 999
  endif

  call getarg(1,msg0)                     !Get message from command line
  msg=msg0

  call chkmsg(msg,cok,nspecial,flip)      !See if it includes "OOO" report
  if(nspecial.gt.0) then                  !or is a shorthand message
     write(*,1010) 
1010 format('Shorthand message.')
     go to 999
  endif

  call packmsg(msg,dgen)                  !Pack message into 72 bits
  write(*,1020) msg0
1020 format('Message:   ',a22)            !Echo input message
  if(iand(dgen(10),8).ne.0) write(*,1030) !Is plain text bit set?
1030 format('Plain text.')         
  write(*,1040) dgen
1040 format('Packed message, 6-bit symbols: ',12i3) !Display packed symbols

  call rs_encode(dgen,sent)               !RS encode
  call interleave63(sent,1)               !Interleave channel symbols
  call graycode(sent,63,1)                !Apply Gray code
  write(*,1050) sent
1050 format('Channel symbols, including FEC:'/(i5,20i3))

  call graycode(sent,63,-1)
  call interleave63(sent,-1)
  call rs_decode(sent,era,0,recd,nerr)
  call unpackmsg(recd,decoded)            !Unpack the user message
  write(*,1060) decoded,cok
1060 format('Decoded message: ',a22,2x,a3)

999 end program JT65code
