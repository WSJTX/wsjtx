subroutine encode240_74(message,codeword)
   use, intrinsic :: iso_c_binding
   use iso_c_binding, only: c_loc,c_size_t
   use crc
   use first_call_tsan, only: wsjt_tsan_acquire_first_call,wsjt_tsan_release_first_call

   integer, parameter:: N=240, K=74, M=N-K
   integer*1 codeword(N)
   integer*1 gen(M,K)
   integer*1 message(K)
   integer*1 pchecks(M)
   include "ldpc_240_74_generator.f90"
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
            do j=1,19
               read(g(i)(j:j),"(Z1)") istr
               ibmax=4
               if(j.eq.19) ibmax=2
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
end subroutine encode240_74
