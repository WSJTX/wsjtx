subroutine decode174_91(llr,Keff,maxosd,norder,apmask,message91,cw,ntype,nharderror,dmin)
!
! A hybrid bp/osd decoder for the (174,91) code.
!
! maxosd<0: do bp only
! maxosd=0: do bp and then call osd once with channel llrs
! maxosd>1: do bp and then call osd maxosd times with saved bp outputs
! norder  : osd decoding depth
!
   integer, parameter:: N=174, K=91, M=N-K
   integer*1 cw(N),apmask(N)
   integer*1 nxor(N),hdec(N)
   integer*1 message91(91),m96(96)
   integer nrw(M),ncw
   integer Nm(7,M)
   integer Mn(3,N)  ! 3 checks per bit
   integer synd(M)
   real tov(3,N)
   real toc(7,M)
   real tanhtoc(7,M)
   real zn(N),zsum(N),zsave(N,3)
   real llr(N)
   real Tmn

   include "ldpc_174_91_c_parity.f90"

   maxiterations=30
   nosd=0
   if(maxosd.gt.3) maxosd=3
   if(maxosd.eq.0) then ! osd with channel llrs
      nosd=1
      zsave(:,1)=llr
   elseif(maxosd.gt.0) then !
      nosd=maxosd
   elseif(maxosd.lt.0) then ! just bp
      nosd=0
   endif

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
   zsum=0.0
   do iter=0,maxiterations
! Update bit log likelihood ratios (tov=0 in iteration 0).
      do i=1,N
         if( apmask(i) .ne. 1 ) then
            zn(i)=llr(i)+sum(tov(1:ncw,i))
         else
            zn(i)=llr(i)
         endif
      enddo
      zsum=zsum+zn
      if(iter.gt.0 .and. iter.le.maxosd) then
         zsave(:,iter)=zsum
      endif

! Check to see if we have a codeword (check before we do any iteration).
      cw=0
      where( zn .gt. 0. ) cw=1
      ncheck=0
      do i=1,M
         synd(i)=sum(cw(Nm(1:nrw(i),i)))
         if( mod(synd(i),2) .ne. 0 ) ncheck=ncheck+1
      enddo
      if( ncheck .eq. 0 ) then ! we have a codeword - if crc is good, return it
         m96=0
         m96(1:77)=cw(1:77)
         m96(83:96)=cw(78:91)
         call get_crc14(m96,96,nbadcrc)
         nharderror=count( (2*cw-1)*llr .lt. 0.0 )
         if(nbadcrc.eq.0) then
            message91=cw(1:91)
            hdec=0
            where(llr .ge. 0) hdec=1
            nxor=ieor(hdec,cw)
            dmin=sum(nxor*abs(llr))
            ntype=1
            return
         endif
      endif

  if( iter.gt.0 ) then  ! this code block implements an early stopping criterion
!      if( iter.gt.10000 ) then  ! this code block implements an early stopping criterion
         nd=ncheck-nclast
         if( nd .lt. 0 ) then ! # of unsatisfied parity checks decreased
            ncnt=0  ! reset counter
         else
            ncnt=ncnt+1
         endif
!    write(*,*) iter,ncheck,nd,ncnt
         if( ncnt .ge. 5 .and. iter .ge. 10 .and. ncheck .gt. 15) then
            nharderror=-1
            exit
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
!      y=atanh(-Tmn)
            tov(i,j)=2*y
         enddo
      enddo

   enddo   ! bp iterations

   do i=1,nosd
      zn=zsave(:,i)
      call osd174_91(zn,Keff,apmask,norder,message91,cw,nharderror,dminosd)
      if(nharderror.gt.0) then
         hdec=0
         where(llr .ge. 0) hdec=1
         nxor=ieor(hdec,cw)
         dmin=sum(nxor*abs(llr))
         ntype=2
         return
      endif
   enddo

   ntype=0
   nharderror=-1
   dminosd=0.0

   return
end subroutine decode174_91
