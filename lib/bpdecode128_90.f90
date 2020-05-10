subroutine bpdecode128_90(llr,apmask,maxiterations,message77,cw,nharderror,iter)
!
! A log-domain belief propagation decoder for the (128,90) code.
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
  real tov(4,N)
  real toc(11,M)
  real tanhtoc(11,M)
  real zn(N)
  real llr(N)
  real Tmn

  include "ldpc_128_90_reordered_parity.f90"

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
      decoded=cw(1:K)
      call chkcrc13a(decoded,nbadcrc)
      if(nbadcrc.eq.0) then
        message77=decoded(1:77)
        nharderror=count( (2*cw-1)*llr .lt. 0.0 )
        return
      endif
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

end subroutine bpdecode128_90
