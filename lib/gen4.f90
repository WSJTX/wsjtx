subroutine gen4(msg0,ichk,msgsent,itone,itype)

! Encode a JT4 message.  Returns msgsent, the message as it will be
! decoded, an integer array itone(206) of 4-FSK tons values in the
! range 0-3; and itype, the JT message type.  

  use jt4
  use packjt
  character*22 msg0
  character*22 message          !Message to be generated
  character*22 msgsent          !Message as it will be received
  character*1 c
  integer itone(206)
  integer*4 i4Msg6BitWords(13)            !72-bit message as 6-bit words
  integer mettab(-128:127,0:1)

  if(msg0(1:1).eq.'@') then
     read(msg0(2:5),*,end=1,err=1) nfreq
     go to 2
1    nfreq=1000
2    itone(1)=nfreq
     msgsent=msg0
  else
     call getmet4(mettab,ndelta)

     message=msg0
     call fmtmsg(message,iz)
     call packmsg(message,i4Msg6BitWords,itype) !Pack into 12 6-bit bytes
     call unpackmsg(i4Msg6BitWords,msgsent) !Unpack to get msgsent
     if(ichk.ne.0) go to 999
     call encode4(message,itone)                 !Encode the information bits
     i1=index(message,'-')
     c=message(i1+1:i1+1)
     if(i1.ge.9 .and. c.ge.'0' .and. c.le.'3') then
        itone=2*itone + (1-npr(2:))             !Inverted '#' sync
     else 
        itone=2*itone + npr(2:)                 !Data = MSB, sync = LSB
     endif
  endif

999 return
end subroutine gen4
