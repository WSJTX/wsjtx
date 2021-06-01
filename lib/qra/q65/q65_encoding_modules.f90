module gf64math
! add and subtract in GF(2^6) based on primitive polynomial x^6+x+1

   implicit none
   integer, private ::  gf64log(0:63)
   integer, private ::  gf64antilog(0:62)

! table of the logarithms of the elements of GF(M) (log(0) never used)
   data gf64log/    &
      -1,   0,   1,   6,   2,  12,   7,  26,   3,  32,  &
      13,  35,   8,  48,  27,  18,   4,  24,  33,  16,  &
      14,  52,  36,  54,   9,  45,  49,  38,  28,  41,  &
      19,  56,   5,  62,  25,  11,  34,  31,  17,  47,  &
      15,  23,  53,  51,  37,  44,  55,  40,  10,  61,  &
      46,  30,  50,  22,  39,  43,  29,  60,  42,  21,  &
      20,  59,  57,  58/

! table of GF(M) elements given their logarithm
   data gf64antilog/   &
      1,   2,   4,   8,  16,  32,   3,   6,  12,  24, &
      48,  35,   5,  10,  20,  40,  19,  38,  15,  30, &
      60,  59,  53,  41,  17,  34,   7,  14,  28,  56, &
      51,  37,   9,  18,  36,  11,  22,  44,  27,  54, &
      47,  29,  58,  55,  45,  25,  50,  39,  13,  26, &
      52,  43,  21,  42,  23,  46,  31,  62,  63,  61, &
      57,  49,  33/

contains

   integer function gf64_add(i1,i2)
      implicit none
      integer::i1
      integer::i2
      gf64_add=iand(ieor(i1,i2),63)
   end function gf64_add

   integer function gf64_mult(i1,i2)
      implicit none
      integer::i1
      integer::i2
      integer::j

      if(i1.eq.0 .or. i2.eq.0) then
         gf64_mult=0
      elseif(i1.eq.1) then
         gf64_mult=i2
      elseif(i2.eq.1) then
         gf64_mult=i1
      else
         j=mod(gf64log(i1)+gf64log(i2),63)
         gf64_mult=gf64antilog(j)
      endif
   end function gf64_mult

end module gf64math

module q65_generator

   integer generator(15,50)
   data generator/  &
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, &
      0,20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, &
      0,20, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, &
      0,20, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, &
      0,20, 0, 1, 1, 0, 0, 0,10, 0, 0, 0, 0, 1, 0, &
      0,20, 0, 1, 1, 0, 0, 0,10, 0, 0, 0,44, 1, 0, &
      0,20, 0, 1, 1, 0, 0, 0,10, 1, 0, 0,44, 1, 0, &
      0,20, 0, 1, 1, 0, 0, 0,10, 1, 0, 0,44, 1,14, &
      0,20, 0, 1, 1, 0, 0, 0,10, 1,31, 0,44, 1,14, &
      0,20, 0, 1, 1,33, 0, 0,10, 1,31, 0,44, 1,14, &
      56,20, 0, 1, 1,33, 0, 0,10, 1,31, 0,44, 1,14, &
      56,20, 0, 1, 1,33, 0, 1,10, 1,31, 0,44, 1,14, &
      56, 1, 0, 1, 1,33, 0, 1,10, 1,31, 0,44, 1,14, &
      56, 1, 0, 1, 1,33, 0, 1,10, 1,31,36,44, 1,14, &
      56, 1, 0, 1, 1,33, 0, 1,43, 1,31,36,44, 1,14, &
      56, 1, 0, 1, 1,33, 0, 1,43,17,31,36,44, 1,14, &
      56, 1, 0, 1, 1,33, 0, 1,43,17,31,36,36, 1,14, &
      56, 1, 0, 1, 1,33,53, 1,43,17,31,36,36, 1,14, &
      56, 1, 0,35, 1,33,53, 1,43,17,31,36,36, 1,14, &
      56, 1, 0,35, 1,33,53, 1,43,17,30,36,36, 1,14, &
      56, 1, 0,35, 1,33,53,52,43,17,30,36,36, 1,14, &
      56, 1, 0,35, 1,32,53,52,43,17,30,36,36, 1,14, &
      56, 1,60,35, 1,32,53,52,43,17,30,36,36, 1,14, &
      56, 1,60,35, 1,32,53,52,43,17,30,36,36,49,14, &
      56, 1,60,35, 1,32,53,52,43,17,30,36,37,49,14, &
      56, 1,60,35,54,32,53,52,43,17,30,36,37,49,14, &
      56, 1,60,35,54,32,53,52, 1,17,30,36,37,49,14, &
      1, 1,60,35,54,32,53,52, 1,17,30,36,37,49,14, &
      1, 0,60,35,54,32,53,52, 1,17,30,36,37,49,14, &
      1, 0,60,35,54,32,53,52, 1,17,30,37,37,49,14, &
      1, 0,61,35,54,32,53,52, 1,17,30,37,37,49,14, &
      1, 0,61,35,54,32,53,52, 1,48,30,37,37,49,14, &
      1, 0,61,35,54,32,53,52, 1,48,30,37,37,49,15, &
      1, 0,61,35,54, 0,53,52, 1,48,30,37,37,49,15, &
      1, 0,61,35,54, 0,52,52, 1,48,30,37,37,49,15, &
      1, 0,61,35,54, 0,52,52, 1,48,30,37,37, 0,15, &
      1, 0,61,35,54, 0,52,34, 1,48,30,37,37, 0,15, &
      1, 0,61,35,54, 0,52,34, 1,48,30,37, 0, 0,15, &
      1, 0,61,35,54, 0,52,34, 1,48,30,20, 0, 0,15, &
      1, 0, 0,35,54, 0,52,34, 1,48,30,20, 0, 0,15, &
      1, 0, 0,35,54, 0,52,34, 1, 0,30,20, 0, 0,15, &
      0, 0, 0,35,54, 0,52,34, 1, 0,30,20, 0, 0,15, &
      0, 0, 0,35,54, 0,52,34, 1, 0,38,20, 0, 0,15, &
      0, 0, 0,35, 0, 0,52,34, 1, 0,38,20, 0, 0,15, &
      0, 0, 0,35, 0, 0,52, 0, 1, 0,38,20, 0, 0,15, &
      0, 0, 0,35, 0, 0,52, 0, 1, 0,38,20, 0, 0, 0, &
      0, 0, 0,35, 0, 0,52, 0, 0, 0,38,20, 0, 0, 0, &
      0, 0, 0,35, 0, 0,52, 0, 0, 0,38, 0, 0, 0, 0, &
      0, 0, 0, 0, 0, 0,52, 0, 0, 0,38, 0, 0, 0, 0, &
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0,38, 0, 0, 0, 0/

end module q65_generator

module q65_encoding

contains

subroutine q65_encode(message,codeword)
   use gf64math
   use q65_generator
   integer message(15)
   integer codeword(65)
   integer i,j

   codeword=0
   codeword(1:15)=message
   do i=1,15
      do j=16,65
         codeword(j)=gf64_add(codeword(j),gf64_mult(message(i),generator(i,j-15)))
      enddo
   enddo

   return
end

subroutine get_q65crc12(mc2,ncrc1,ncrc2)
!
   character c12*12,c6*6
   integer*1 mc(90),mc2(90),tmp(6)
   integer*1 r(13),p(13)
   integer ncrc
! polynomial for 12-bit CRC 0xF01
   data p/1,1,0,0,0,0,0,0,0,1,1,1,1/

! flip bit order of each 6-bit symbol for consistency with Nico's calculation
   do i=0,14
      tmp=mc2(i*6+1:i*6+6)
      mc(i*6+1:i*6+6)=tmp(6:1:-1)
   enddo

! divide by polynomial
   r=mc(1:13)
   do i=0,77
      r(13)=mc(i+13)
      r=mod(r+r(1)*p,2)
      r=cshift(r,1)
   enddo

   write(c6,'(6b1)') r(6:1:-1)
   read(c6,'(b6.6)') ncrc1
   read(c6,'(6b1)') mc2(79:84)
   write(c6,'(6b1)') r(12:7:-1)
   read(c6,'(b6.6)') ncrc2
   read(c6,'(6b1)') mc2(85:90)

end subroutine get_q65crc12

subroutine get_q65_tones(msg37,codeword,itone)
   use packjt77
   implicit none
   character*37 msg37
   character*77 c77
   character*12 c12
   character*6  c6
   integer codeword(65)
   integer sync(22)
   integer message(15)
   integer shortcodeword(63)
   integer itone(85)
   integer i,j,k
   integer*1 mbits(90)
   integer i3,n3,ncrc1,ncrc2
   data sync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/

   i3=-1
   n3=-1
   call pack77(msg37,i3,n3,c77)
   mbits=0
   read(c77,'(77i1)') mbits(1:77)

! Message is 77 bits long. Add a 0 bit to create a 78-bit message and pad with 
! 12 zeros to create 90-bit mbit array for CRC calculation. 
   call get_q65crc12(mbits,ncrc1,ncrc2)

! Now have message in bits 1:78 and CRC in bits 79:90.
! Group message bits into 15 6-bit symbols:
   do i=0,14
      write(c6,'(6i1)') mbits( (i*6+1):(i*6+6) )
      read(c6,'(b6.6)') message(i+1)
   enddo

! Encode to create a 65-symbol codeword
   call q65_encode(message,codeword)

!Shorten the codeword by omitting the CRC symbols (symbols 14 and 15)
   shortcodeword(1:13)=codeword(1:13)
   shortcodeword(14:63)=codeword(16:65)

!Insert sync symbols to create array of channel symbols
   j=1
   k=0
   do i=1,85
      if(i.eq.sync(j)) then
         j=j+1
         itone(i)=0
      else
         k=k+1
         itone(i)=shortcodeword(k)+1
      endif
   enddo
end subroutine get_q65_tones

end module q65_encoding
