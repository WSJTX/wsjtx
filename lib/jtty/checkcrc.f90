subroutine checkcrc(decoded,nbadcrc)

integer*4 crc12
integer*4 nbadcrc,ncrc10
integer*1 decoded(42)
integer*4 n32
integer*1 n32a(4)
character*42 c42
logical crcok
equivalence (n32,n32a)

write(c42,'(42i1)') decoded(1:42)
read(c42,'(b32.32,b10.10)') n32,ncrc10

crcok=ncrc10.eq.iand(crc12(n32a,4),1023)
nbadcrc=0
if( .not. crcok ) nbadcrc = 1

return
end subroutine checkcrc

