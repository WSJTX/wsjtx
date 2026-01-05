module jtty_fec

contains

subroutine encode_80_42(message,codeword)

use, intrinsic :: iso_c_binding
use iso_c_binding, only: c_loc,c_size_t
!use crc

integer, parameter:: N=80, K=42, M=N-K
integer*1 codeword(N)
integer*1 gen(M,K)
integer*1 message(K)
integer*1 pchecks(M)
logical first
include "jtty_generator_80_42.f90"
data first/.true./
save first,gen

do i=1,M
  pchecks(i)=mod( sum(message*gen(i,:)), 2)
enddo

codeword(1:42)=message
codeword(43:80)=pchecks

return

end subroutine encode_80_42

subroutine bpdecode_80_42(llr,maxiterations,message,cw,nharderror)
!
! A log-domain belief propagation decoder for the (80,42) code.
!
!  use iso_c_binding, only: c_loc,c_size_t
!  use crc
  integer, parameter:: N=80, K=42, M=N-K
  integer*1 cw(N)
  integer*1 message(K)
  integer Nm(7,M)   
  integer Mn(3,N) 
  integer nrw(M)
  integer synd(M)
  real tov(3,N)
  real toc(7,M)
  real tanhtoc(7,M)
  real zn(N)
  real llr(N)
  real Tmn

  include "jtty_parity_80_42.f90"

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
       zn(i)=llr(i)+sum(tov(1:ncw,i))
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
    if( ncheck .eq. 0 ) then ! we have a codeword - return it
        message=cw(1:42)
        nharderror=count( (2*cw-1)*llr .lt. 0.0 )
        return
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
      tanhtoc(1:7,i)=tanh(-toc(1:7,i)/2)
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

include "../indexx.f90"
include "checkcrc.f90"
include "osd80_42.f90"

end module jtty_fec
