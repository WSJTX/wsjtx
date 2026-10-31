subroutine encode_128_90(message77,codeword)
!
! Add a 13-bit CRC to a 77-bit message and return a 128-bit codeword
!
use, intrinsic :: iso_c_binding
use iso_c_binding, only: c_loc,c_size_t
use crc
use first_call_tsan, only: wsjt_tsan_acquire_first_call,wsjt_tsan_release_first_call

integer, parameter:: N=128, K=90, M=N-K
character*90 tmpchar
integer*1 codeword(N)
integer*1 gen(M,K)
integer*1 message77(77),message(K)
integer*1 pchecks(M)
integer*1, target :: i1MsgBytes(12)
include "ldpc_128_90_generator.f90"
logical first
data first/.true./
save first,gen
logical building

!$omp atomic read seq_cst
building=first
if(building) then
   !$omp critical(first_call)
   call wsjt_tsan_acquire_first_call()
   if( first ) then ! fill the generator matrix
     gen=0
     do i=1,M
       do j=1,23
         read(g(i)(j:j),"(Z1)") istr
           ibmax=4
           if(j.eq.23) ibmax=2 
           do jj=1, ibmax 
             icol=(j-1)*4+jj
             if( btest(istr,4-jj) ) gen(i,icol)=1
           enddo
       enddo
     enddo
   !$omp atomic write seq_cst
   first=.false.
   endif
   call wsjt_tsan_release_first_call()
   !$omp end critical(first_call)
endif

! Add 13 bit CRC to form 90-bit message+CRC13
write(tmpchar,'(77i1)') message77
tmpchar(78:80)='000'
i1MsgBytes=0
read(tmpchar,'(10b8)') i1MsgBytes(1:10)
ncrc13 = crc13 (c_loc (i1MsgBytes), 12)
write(tmpchar(78:90),'(b13)') ncrc13
read(tmpchar,'(90i1)') message

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
end subroutine encode_128_90
