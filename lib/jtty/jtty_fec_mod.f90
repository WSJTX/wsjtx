module jtty_fec

contains

subroutine encode_80_42(message42,codeword80)

use, intrinsic :: iso_c_binding
use iso_c_binding, only: c_loc,c_size_t
!use crc

integer, parameter:: N=128, K=90, M=N-K
integer*1 codeword80(80)
integer*1 gen(M,K)
integer*1 message(K), message42(42)
integer*1 pchecks(M)
include "ldpc_128_90_generator.f90"
logical first
data first/.true./
save first,gen

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
first=.false.
endif

! Shorten the MSK144 (128,90) code by zeroing 48 bits of the 90-bit message.

message(1:42)=message42
message(43:90)=0

do i=1,M
  nsum=0
  do j=1,K 
    nsum=nsum+message(j)*gen(i,j)
  enddo
  pchecks(i)=mod(nsum,2)
enddo

codeword80(1:42)=message42
codeword80(43:80)=pchecks

return
end subroutine encode_80_42

subroutine bpdecode_80_42(llr80,maxiterations,message42,cw80,nharderror)
!
! A log-domain belief propagation decoder for the (128,90) code.
!
!  use iso_c_binding, only: c_loc,c_size_t
!  use crc
  integer, parameter:: N=128, K=90, M=N-K
  integer*1 cw(N),apmask(N),cw80(80)
  integer*1 decoded(K)
  integer*1 message42(42)
  integer Nm(11,M)   
  integer Mn(3,N) 
  integer nrw(M)
  integer synd(M)
  real tov(4,N)
  real toc(11,M)
  real tanhtoc(11,M)
  real zn(N)
  real llr(N),llr80(80)
  real Tmn

  include "ldpc_128_90_reordered_parity.f90"

  apmask=0
!  apmask(43:90)=1
  apval=-20.0
  llr(1:42)=llr80(1:42)
  llr(43:90)=apval
  llr(91:128)=llr80(43:80)

  decoded=0
  toc=0
  tov=0
  tanhtoc=0
! initialize messages to checks
  do j=1,M
    do i=1,nrw(j)
      toc(i,j)=llr((Nm(i,j)))
    enddo
  enddo

  ncnt=0
  nclast=0

  do iter=0,maxiterations

! Update bit log likelihood ratios (tov=0 in iteration 0).
    do i=1,N
      if( apmask(i) .ne. 1 ) then
        zn(i)=llr(i)+sum(tov(1:ncw,i))
      else
        zn(i)=llr(i)
      endif
    enddo

! Check to see if we have a codeword (check before we do any iteration).
    cw=0
    where( zn .gt. 0. ) cw=1
    ncheck=0
    do i=1,M
      synd(i)=sum(cw(Nm(1:nrw(i),i)))
      if( mod(synd(i),2) .ne. 0 ) ncheck=ncheck+1
!     if( mod(synd(i),2) .ne. 0 ) write(*,*) 'check ',i,' unsatisfied'
    enddo
!   write(*,*) 'number of unsatisfied parity checks ',ncheck
    if( ncheck .eq. 0 ) then ! we have a codeword - reorder the columns and return it
!      decoded=cw(1:K)
!      if(nbadcrc.eq.0) then
        message42=cw(1:42)
        cw80(1:42)=cw(1:42)
        cw80(43:80)=cw(91:128)
        nharderror=count( (2*cw-1)*llr .lt. 0.0 )
        return
!      endif
    endif

    if( iter.gt.0 ) then  ! this code block implements an early stopping criterion
!    if( iter.gt.10000 ) then  ! this code block implements an early stopping criterion
      nd=ncheck-nclast
      if( nd .lt. 0 ) then ! # of unsatisfied parity checks decreased
        ncnt=0  ! reset counter
      else
        ncnt=ncnt+1
      endif
!      write(*,*) iter,ncheck,nd,ncnt
      if( ncnt .ge. 3 .and. iter .ge. 5 .and. ncheck .gt. 10) then
        nharderror=-1
        return
      endif
    endif
    nclast=ncheck

! Send messages from bits to check nodes 
    do j=1,M
      do i=1,nrw(j)
        ibj=Nm(i,j)
        toc(i,j)=zn(ibj)  
        do kk=1,ncw ! subtract off what the bit had received from the check
          if( Mn(kk,ibj) .eq. j ) then  
            toc(i,j)=toc(i,j)-tov(kk,ibj)
          endif
        enddo
      enddo
    enddo

! send messages from check nodes to variable nodes
    do i=1,M
      tanhtoc(1:11,i)=tanh(-toc(1:11,i)/2)
    enddo

    do j=1,N
      do i=1,ncw
        ichk=Mn(i,j)  ! Mn(:,j) are the checks that include bit j
        Tmn=product(tanhtoc(1:nrw(ichk),ichk),mask=Nm(1:nrw(ichk),ichk).ne.j)
        call platanh(-Tmn,y)
!        y=atanh(-Tmn)
        tov(i,j)=2*y
      enddo
    enddo

  enddo
  nharderror=-1
  return

end subroutine bpdecode_80_42

subroutine platanh(x,y)
  isign=+1
  z=x
  if( x.lt.0 ) then
    isign=-1
    z=abs(x)
  endif
  if( z.le. 0.664 ) then
    y=x/0.83
    return
  elseif( z.le. 0.9217 ) then
    y=isign*(z-0.4064)/0.322
    return
  elseif( z.le. 0.9951 ) then
    y=isign*(z-0.8378)/0.0524
    return
  elseif( z.le. 0.9998 ) then
    y=isign*(z-0.9914)/0.0012
    return
  else
    y=isign*7.0
    return
  endif
end subroutine platanh

end module jtty_fec
