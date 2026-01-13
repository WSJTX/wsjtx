program testcrc10

character c10*10
integer*1 message(42)
integer   ncrc10
!                  0       9       e       4       6       8       e       0
data message/0,0,0,0,1,0,0,1,1,1,1,0,0,1,0,0,0,1,1,0,1,0,0,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0/

write(*,*) 'message with crc bits zeroed'
write(*,'(42i1)') message

! get crc10 - note that length of message vector is length of data+crc.
!
call get_crc10(message,42,ncrc10)

! the calculated crc value is 0x63 (99 decimal) for ncrc10. This agrees with 
! the ninjacalc.mbedded.ninja online calculator. Data input is 0x09e468e0 (32 bits)
! and truncated generator polynomial was entered as 0x08f (drop the most significant bit 
! of the polynomial).
!
write(*,'(a,z3.3)') 'ncrc10 (hex): ',ncrc10

! now enter crc bits into message vector and call get_crc10 again. This should
! return ncrc10=0, which indicates that the crc embedded in the message vector
! agrees with the calculated crc.
!
write(c10,'(b10.10)') ncrc10
read(c10,'(10b1)') message(33:42)

write(*,*) 'message with crc '
write(*,'(42i1)') message

call get_crc10(message,42,ncrc10)
write(*,'(a,z3.3)') 'ncrc10 (hex): ',ncrc10

if(ncrc10 .eq.0 ) write(*,*) 'Good CRC'

stop
end
