subroutine get_crc14(mc,len,ncrc)
!
! 1. To calculate 14-bit CRC, mc(1:len-14) is the message and mc(len-13:len) are zero.
! 2. To check a received CRC, mc(1:len is the received message plus CRC.
!    ncrc will be zero if the received message/CRC are consistent
!  
  character c14*14
  integer*1 mc(len)
  integer*1 r(15),p(15)
  integer ncrc
! polynomial for 14-bit CRC 0x6757
  data p/1,1,0,0,1,1,1,0,1,0,1,0,1,1,1/
  
! divide by polynomial
  r=mc(1:15)
  do i=0,len-15
    r(15)=mc(i+15)
    r=mod(r+r(1)*p,2)
    r=cshift(r,1)
  enddo

  write(c14,'(14b1)') r(1:14)
  read(c14,'(b14.14)') ncrc

end subroutine get_crc14
