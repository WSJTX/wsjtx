subroutine fastosd240_74(llr,k,apmask,ndeep,message74,cw,nhardmin,dmin)
! 
! An ordered-statistics decoder for the (240,74) code.
! Message payload is 50 bits. Any or all of a 24-bit CRC can be
! used for detecting incorrect codewords. The remaining CRC bits are
! cascaded with the LDPC code for the purpose of improving the
! distance spectrum of the code.
!
! If p1 (0.le.p1.le.24) is the number of CRC24 bits that are
! to be used for bad codeword detection, then the argument k should
! be set to 77+p1.
!
! Valid values for k are in the range [50,74].
!
   character*24 c24
   integer, parameter:: N=240
   integer*1 apmask(N),apmaskr(N)
   integer*1, allocatable, save :: gen(:,:)
   integer*1, allocatable :: genmrb(:,:),g2(:,:)
   integer*1, allocatable :: temp(:),temprow(:),m0(:),me(:),mi(:)
   integer indices(N),indices2(N),nxor(N)
   integer*1 cw(N),ce(N),c0(N),hdec(N)
   integer*1, allocatable :: decoded(:)
   integer*1 message74(74)
   integer*1, allocatable :: sp(:)
   integer indx(N),ksave
   real llr(N),rx(N),absrx(N)

   logical first
   data first/.true./,ksave/64/
   save first,ksave

   allocate( genmrb(k,N), g2(N,k) )
   allocate( temp(k), temprow(n), m0(k), me(k), mi(k) )
   allocate( decoded(k) )

   if( first .or. k.ne.ksave) then ! fill the generator matrix
!
! Create generator matrix for partial CRC cascaded with LDPC code.
!
! Let p2=74-k and p1+p2=24.
!
! The last p2 bits of the CRC24 are cascaded with the LDPC code.
!
! The first p1=k-50 CRC24 bits will be used for error detection.
!
      if( allocated(gen) ) deallocate(gen)
      allocate( gen(k,N) )
      gen=0
      do i=1,k
         message74=0
         message74(i)=1
         if(i.le.50) then
            call get_crc24(message74,74,ncrc24)
            write(c24,'(b24.24)') ncrc24
            read(c24,'(24i1)') message74(51:74)
            message74(51:k)=0
         endif
         call encode240_74(message74,cw)
         gen(i,:)=cw
      enddo

      first=.false.
      ksave=k
   endif

! Use best k elements from the sorted list for the first basis. For the 2nd basis replace 
! the nswap lowest quality symbols with the best nswap elements from the parity symbols.
   nswap=20

   do ibasis=1,2
      rx=llr
      apmaskr=apmask

! Hard decisions on the received word.
      hdec=0
      where(rx .ge. 0) hdec=1

! Use magnitude of received symbols as a measure of reliability.
      absrx=abs(llr)
      call indexx(absrx,N,indx)

! Re-order the columns of the generator matrix in order of decreasing reliability.
      do i=1,N
         genmrb(1:k,i)=gen(1:k,indx(N+1-i))
         indices(i)=indx(N+1-i)
      enddo

      if(ibasis.eq.2) then
         do i=k-nswap+1,k
            temp(1:k)=genmrb(1:k,i)
            genmrb(1:k,i)=genmrb(1:k,i+nswap)
            genmrb(1:k,i+nswap)=temp(1:k)
            itmp=indices(i)
            indices(i)=indices(i+nswap)
            indices(i+nswap)=itmp
         enddo
      endif

! Do gaussian elimination to create a generator matrix with the most reliable
! received bits in positions 1:k in order of decreasing reliability (more or less).

      icol=1
      indices2=0
      nskipped=0
      do id=1,k
         iflag=0
         do while(iflag.eq.0)
            if(genmrb(id,icol).ne.1) then
               do j=id+1,k
                  if(genmrb(j,icol).eq.1) then
                     temprow=genmrb(id,:)
                     genmrb(id,:)=genmrb(j,:)
                     genmrb(j,:)=temprow
                     iflag=1
                  endif
               enddo
               if(iflag.eq.0) then ! skip this column
                  nskipped=nskipped+1
                  indices2(k+nskipped)=icol ! put icol where skipped columns go
                  icol=icol+1 ! look at the next column
               endif
            else
               iflag=1
            endif
         enddo
         indices2(id)=icol
         do j=1,k
            if(id.ne.j .and. genmrb(j,icol).eq.1) then
               genmrb(j,:)=ieor(genmrb(id,:),genmrb(j,:))
            endif
         enddo
         icol=icol+1
      enddo
      do i=k+nskipped+1,240
         indices2(i)=i
      enddo
      genmrb(1:k,:)=genmrb(1:k,indices2)
      indices=indices(indices2)

!************************************
      g2=transpose(genmrb)

! The hard decisions for the k MRB bits define the order 0 message, m0.
! Encode m0 using the modified generator matrix to find the "order 0" codeword.
! Flip various combinations of bits in m0 and re-encode to generate a list of
! codewords. Return the member of the list that has the smallest Euclidean
! distance to the received word.

      hdec=hdec(indices)   ! hard decisions from received symbols
      m0=hdec(1:k)         ! zero'th order message
      absrx=abs(llr)
      absrx=absrx(indices)
      rx=rx(indices)
      apmaskr=apmaskr(indices)

      call mrbencode74(m0,c0,g2,N,k)
      nxor=ieor(c0,hdec)
      nhardmin=sum(nxor)
      dmin=sum(nxor*absrx)
      np=32
      if(ibasis.eq.1)   allocate(sp(np))

      cw=c0
      ntotal=0
      nrejected=0
      xlambda=0.0

      if(ndeep.eq.0) goto 998  ! norder=0
      if(ndeep.gt.4) ndeep=4
      if( ndeep.eq. 1) then
         nord=1
         xlambda=0.0
         nsyncmax=np
      elseif(ndeep.eq.2) then
         nord=2
         xlambda=0.0
         nsyncmax=np
      elseif(ndeep.eq.3) then
         nord=3
         xlambda=4.0
         nsyncmax=11
      elseif(ndeep.eq.4) then
         nord=4
         xlambda=3.4
         nsyndmax=12
      endif

      s1=sum(absrx(1:k))
      s2=sum(absrx(k+1:N))
      rho=s1/(s1+xlambda*s2)
      rhodmin=rho*dmin
      nerr64=-1
      do iorder=1,nord
!beta=0.0
!if(iorder.ge.3) beta=0.4
!spnc_order=sum(absrx(k-iorder+1:k))+beta*(N-k)
!if(dmin.lt.spnc_order) cycle 
         mi(1:k-iorder)=0
         mi(k-iorder+1:k)=1
         iflag=k-iorder+1
         do while(iflag .ge.0)
            ntotal=ntotal+1
            me=ieor(m0,mi)
            d1=sum(mi(1:k)*absrx(1:k))
            if(d1.gt.rhodmin) exit
            call partial_syndrome(me,sp,np,g2,N,K)
            nwhsp=sum(ieor(sp(1:np),hdec(k:k+np-1)))
            if(nwhsp.le.nsyndmax) then
               call mrbencode74(me,ce,g2,N,k)
               nxor=ieor(ce,hdec)
               dd=sum(nxor*absrx(1:N))
               if( dd .lt. dmin ) then
                  dmin=dd
                  rhodmin=rho*dmin
                  cw=ce
                  nhardmin=sum(nxor)
                  nwhspmin=nwhsp
                  nerr64=sum(nxor(1:K))
               endif
            endif
! Get the next test error pattern, iflag will go negative
! when the last pattern with weight iorder has been generated.
            call nextpat74(mi,k,iorder,iflag)
         enddo
      enddo

998   continue
! Re-order the codeword to [message bits][parity bits] format.
      cw(indices)=cw
      hdec(indices)=hdec
      message74=cw(1:74)
      call get_crc24(message74,74,nbadcrc)
      if(nbadcrc.eq.0) exit
      nhardmin=-nhardmin
   enddo  ! basis loop
   return
end subroutine fastosd240_74

subroutine mrbencode74(me,codeword,g2,N,K)
   integer*1 me(K),codeword(N),g2(N,K)
! fast encoding for low-weight test patterns
   codeword=0
   do i=1,K
      if( me(i) .eq. 1 ) then
         codeword=ieor(codeword,g2(1:N,i))
      endif
   enddo
   return
end subroutine mrbencode74

subroutine partial_syndrome(me,sp,np,g2,N,K)
   integer*1 me(K),sp(np),g2(N,K)
! compute partial syndrome
   sp=0
   do i=1,K
      if( me(i) .eq. 1 ) then
         sp=ieor(sp,g2(K:K+np-1,i))
      endif
   enddo
   return
end subroutine partial_syndrome

subroutine nextpat74(mi,k,iorder,iflag)
   integer*1 mi(k),ms(k)
! generate the next test error pattern
   ind=-1
   do i=1,k-1
      if( mi(i).eq.0 .and. mi(i+1).eq.1) ind=i
   enddo
   if( ind .lt. 0 ) then ! no more patterns of this order
      iflag=ind
      return
   endif
   ms=0
   ms(1:ind-1)=mi(1:ind-1)
   ms(ind)=1
   ms(ind+1)=0
   if( ind+1 .lt. k ) then
      nz=iorder-sum(ms)
      ms(k-nz+1:k)=1
   endif
   mi=ms
   do i=1,k  ! iflag will point to the lowest-index 1 in mi
      if(mi(i).eq.1) then
         iflag=i
         exit
      endif
   enddo
   return
end subroutine nextpat74

