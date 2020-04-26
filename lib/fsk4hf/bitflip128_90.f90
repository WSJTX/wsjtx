subroutine bitflip128_90(llr,message77,cw,nharderror)
!
! A hard-decision bit flipping decoder for the (128,90) code.
! 
 
  use iso_c_binding, only: c_loc,c_size_t
  use crc
  integer, parameter:: N=128, K=90, M=N-K
  integer*1 cw(N),apmask(N)
  integer*1 decoded(K)
  integer*1 message77(77)
  integer Nm(11,M)   
  integer Mn(3,N) 
  integer nrw(M)
  integer synd(M)
  integer nuns(N)
  real zn(N)
  real llr(N)

  include "ldpc_128_90_reordered_parity.f90"

  decoded=0
  zn=llr

  do iter=0,0

! Check to see if we have a codeword (check before we do any iteration).
    cw=0
    where( zn .gt. 0. ) cw=1
    ncheck=0
    nuns=0
    do i=1,M
      synd(i)=sum(cw(Nm(1:nrw(i),i)))
      if( mod(synd(i),2) .ne. 0 ) then
        ncheck=ncheck+1
        do j=1,nrw(i)
          nuns(Nm(j,i))=nuns(Nm(j,i))+1
        enddo
      endif 
    enddo
    if( ncheck .eq. 0 ) then ! we have a codeword - reorder the columns and return it
      decoded=cw(1:K)
      call chkcrc13a(decoded,nbadcrc)
      if(nbadcrc.eq.0) then
        message77=decoded(1:77)
        nharderror=count( (2*cw-1)*llr .lt. 0.0 )
        return
      endif
    endif
! flip the sign on the symbols that show up in the largest number
! of un-satisfied parity checks
    where( nuns .eq. maxval(nuns) ) zn=-zn

  enddo
  llr=zn
  nharderror=-1
  return

end subroutine bitflip128_90
