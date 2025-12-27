integer function crc12(data,length)

  integer*1 data(0:79)
  integer*4 crc

  crc=0                                           !Initial CRC value
  do i=0,length-1
     i4=data(i)
     crc=ieor(crc,shiftl(i4,4))
     do j=0,7
        if(iand(crc,2048).ne.0) then
           crc=ieor(2*crc,2063)
        else
           crc=2*crc
        endif
     enddo
  enddo
  crc12=iand(crc,4095)

  return
end function crc12
