subroutine encode280_74(message,codeword)
   integer, parameter:: N=280, K=74, M=N-K
   integer*1 codeword(N)
   integer*1 gen(M,K)
   integer*1 message(K)
   integer*1 pchecks(M)
   include "ldpc_280_74_generator.f90"
   logical first
   data first/.true./
   save first,gen

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
      first=.false.
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
end subroutine encode280_74
