subroutine get_crc10(mc,len,ncrc)
!
! 1. To calculate 10-bit CRC, mc(1:len-10) is the message and mc(len-9:len) are zero.
! 2. To check a received CRC, mc(1:len) is the received message plus CRC.
!    ncrc will be zero if the received message/CRC are consistent.
!
   character c10*10
   integer*1 mc(len)
   integer*1 r(11),p(11)
   integer ncrc
! polynomial for 10-bit CRC 0x48f (full polynomial, no truncation)
! this is "CRC-10F/4.2" from Koopman's list of good 10-bit CRCs
   data p/1,0,0,1,0,0,0,1,1,1,1/

! divide by polynomial
   r=mc(1:11)
   do i=0,len-11
      r(11)=mc(i+11)
      r=mod(r+r(1)*p,2)
      r=cshift(r,1)
   enddo

   write(c10,'(10b1)') r(1:10)
   read(c10,'(b10.10)') ncrc

end subroutine get_crc10

