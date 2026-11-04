subroutine encode_msk40(message,codeword)
! Encode a 16-bit message and return a 32-bit codeword. 
! The code is a (32,16) regular ldpc code with column weight 3.
! The code was generated using the PEG algorithm.
! After creating the codeword, the columns are re-ordered according to 
! "colorder" to make the codeword compatible with the parity-check
! matrix stored in Radford Neal's "pchk" format.
!
use first_call_tsan, only: wsjt_tsan_acquire_first_call,wsjt_tsan_release_first_call
integer*1 codeword(32)
integer*1 colorder(32)
integer g(16)
integer*1 gen40(16,16)
integer*1 itmp(32)
integer*1 message(16)
integer*1 pchecks(16)
logical first
data first/.true./
data g/Z'4428',Z'5a6b',Z'1b04',Z'2c12',Z'60c4',Z'1071',Z'be6a',Z'36dd', &
       Z'c580',Z'ad9a',Z'eca2',Z'7843',Z'332e',Z'a685',Z'5906',Z'1efe'/
data colorder/4,1,2,3,0,8,6,10,13,28,20,23,17,15,27,25, &
             16,12,18,19,7,21,22,11,24,5,26,14,9,29,30,31/
save first,gen40
logical building

!$omp atomic read seq_cst
building=first
if(building) then
   !$omp critical(first_call)
   call wsjt_tsan_acquire_first_call()
   if( first ) then ! fill the generator matrix
     gen40=0
     do i=1,16
       do j=1,16
         if( btest(g(i),16-j) ) gen40(i,j)=1
       enddo
     enddo
     !$omp atomic write seq_cst
     first=.false.
   endif
   call wsjt_tsan_release_first_call()
   !$omp end critical(first_call)
endif

do i=1,16
  nsum=0
  do j=1,16
    nsum=nsum+message(j)*gen40(i,j)
  enddo
  pchecks(i)=mod(nsum,2)
enddo
itmp(1:16)=pchecks
itmp(17:32)=message(1:16)
codeword(colorder+1)=itmp(1:32)

return
end subroutine encode_msk40
