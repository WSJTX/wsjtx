subroutine encode174_91var(message77,codeword)
!
! Add a 14-bit CRC to a 77-bit message and return a 174-bit codeword
!
use crc, only: crc14

integer, parameter:: N=174, K=91, M=N-K
integer*1 codeword(N)
integer*1 gen(M,K)
integer*1 message77(77),message(K)
integer*1 pchecks(M),crc_bits(96)
include '/lib/ft8/ldpc_174_91_c_generator.f90'
logical first
data first/.true./
save first,gen
!$omp threadprivate(first,gen)

if( first ) then ! fill the generator matrix
  first=.false.
  gen=0
  do i=1,M
    do j=1,23
      read(g(i)(j:j),"(Z1)") istr
        ibmax=4
        if(j.eq.23) ibmax=3 
        do jj=1, ibmax 
          icol=(j-1)*4+jj
          if( btest(istr,4-jj) ) gen(i,icol)=1
        enddo
    enddo
  enddo
endif

! Add 14-bit CRC to form 91-bit message+CRC14
crc_bits=0
crc_bits(1:77)=message77
ncrc14=crc14(crc_bits)
message(1:77)=message77
do i=1,14
   message(77+i)=ibits(ncrc14,14-i,1)
enddo

do i=1,M
  nsum=0
  do j=1,K 
    nsum=nsum+message(j)*gen(i,j)
  enddo
  pchecks(i)=mod(nsum,2)
enddo

codeword(1:K)=message
codeword(K+1:N)=pchecks

return
end subroutine encode174_91var
