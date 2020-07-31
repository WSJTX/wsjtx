subroutine get_crc24(mc,len,ncrc)
!
! 1. To calculate 24-bit CRC, mc(1:len-24) is the message and mc(len-23:len) are zero.
! 2. To check a received CRC, mc(1:len) is the received message plus CRC. 
!    ncrc will be zero if the received message/CRC are consistent.
!
   character c24*24
   integer*1 mc(len)
   integer*1 r(25),p(25)
   integer ncrc
! polynomial for 24-bit CRC 0x100065b
   data p/1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,1,1,0,1,1/

! divide by polynomial
   r=mc(1:25)
   do i=0,len-25 
      r(25)=mc(i+25)
      r=mod(r+r(1)*p,2)
      r=cshift(r,1)
   enddo

   write(c24,'(24b1)') r(1:24)
   read(c24,'(b24.24)') ncrc

end subroutine get_crc24
