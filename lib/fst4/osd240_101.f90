subroutine osd240_101(llr,k,apmask,ndeep,message101,cw,nhardmin,dmin)
!
! An ordered-statistics decoder for the (240,101) code.
! Message payload is 77 bits. Any or all of a 24-bit CRC can be
! used for detecting incorrect codewords. The remaining CRC bits are
! cascaded with the LDPC code for the purpose of improving the
! distance spectrum of the code.
!
! If p1 (0.le.p1.le.24) is the number of CRC24 bits that are
! to be used for bad codeword detection, then the argument k should
! be set to 77+p1.
!
! Valid values for k are in the range [77,101].
!
   character*24 c24
   integer, parameter:: N=240
   integer*1 apmask(N),apmaskr(N)
   integer*1, allocatable, save :: gen(:,:)
   integer*1, allocatable :: genmrb(:,:),g2(:,:)
   integer*1, allocatable :: temp(:),m0(:),me(:),mi(:),misub(:),e2sub(:),e2(:),ui(:)
   integer*1, allocatable :: r2pat(:)
   integer indices(N),nxor(N)
   integer*1 cw(N),ce(N),c0(N),hdec(N)
   integer*1, allocatable :: decoded(:)
   integer*1 message101(101)
   integer indx(N)
   real llr(N),rx(N),absrx(N)

   logical first,reset
   data first/.true./
   save first

   allocate( genmrb(k,N), g2(N,k) )
   allocate( temp(k), m0(k), me(k), mi(k), misub(k), e2sub(N-k), e2(N-k), ui(N-k) )
   allocate( r2pat(N-k), decoded(k) )

   if( first ) then ! fill the generator matrix
!
! Create generator matrix for partial CRC cascaded with LDPC code.
! 
! Let p2=101-k and p1+p2=24. 
!
! The last p2 bits of the CRC24 are cascaded with the LDPC code.
! 
! The first p1=k-77 CRC24 bits will be used for error detection.
!
      allocate( gen(k,N) )
      gen=0
      do i=1,k
         message101=0
         message101(i)=1
         if(i.le.77) then
            call get_crc24(message101,101,ncrc24)
            write(c24,'(b24.24)') ncrc24
            read(c24,'(24i1)') message101(78:101)
            message101(78:k)=0
         endif
         call encode240_101(message101,cw)
         gen(i,:)=cw
      enddo

      first=.false.
   endif

   rx=llr
   apmaskr=apmask

! Hard decisions on the received word.
   hdec=0
   where(rx .ge. 0) hdec=1

! Use magnitude of received symbols as a measure of reliability.
   absrx=abs(rx)
   call indexx(absrx,N,indx)

! Re-order the columns of the generator matrix in order of decreasing reliability.
   do i=1,N
      genmrb(1:k,i)=gen(1:k,indx(N+1-i))
      indices(i)=indx(N+1-i)
   enddo

! Do gaussian elimination to create a generator matrix with the most reliable
! received bits in positions 1:k in order of decreasing reliability (more or less).
   do id=1,k ! diagonal element indices
      do icol=id,k+20  ! The 20 is ad hoc - beware
         iflag=0
         if( genmrb(id,icol) .eq. 1 ) then
            iflag=1
            if( icol .ne. id ) then ! reorder column
               temp(1:k)=genmrb(1:k,id)
               genmrb(1:k,id)=genmrb(1:k,icol)
               genmrb(1:k,icol)=temp(1:k)
               itmp=indices(id)
               indices(id)=indices(icol)
               indices(icol)=itmp
            endif
            do ii=1,k
               if( ii .ne. id .and. genmrb(ii,id) .eq. 1 ) then
                  genmrb(ii,1:N)=ieor(genmrb(ii,1:N),genmrb(id,1:N))
               endif
            enddo
            exit
         endif
      enddo
   enddo

   g2=transpose(genmrb)

! The hard decisions for the k MRB bits define the order 0 message, m0.
! Encode m0 using the modified generator matrix to find the "order 0" codeword.
! Flip various combinations of bits in m0 and re-encode to generate a list of
! codewords. Return the member of the list that has the smallest Euclidean
! distance to the received word.

   hdec=hdec(indices)   ! hard decisions from received symbols
   m0=hdec(1:k)         ! zero'th order message
   absrx=absrx(indices)
   rx=rx(indices)
   apmaskr=apmaskr(indices)

   call mrbencode101(m0,c0,g2,N,k)
   nxor=ieor(c0,hdec)
   nhardmin=sum(nxor)
   dmin=sum(nxor*absrx)

   cw=c0
   ntotal=0
   nrejected=0
   npre1=0
   npre2=0
   nt=0

   if(ndeep.eq.0) goto 998  ! norder=0
   if(ndeep.gt.6) ndeep=6
   if( ndeep.eq. 1) then
      nord=1
      npre1=0
      npre2=0
      nt=40
      ntheta=12
   elseif(ndeep.eq.2) then
      nord=1
      npre1=1
      npre2=0
      nt=40
      ntheta=12
   elseif(ndeep.eq.3) then
      nord=1
      npre1=1
      npre2=1
      nt=40
      ntheta=12
      ntau=14
   elseif(ndeep.eq.4) then
      nord=2
      npre1=1
      npre2=1
      nt=40
      ntheta=12
      ntau=17
   elseif(ndeep.eq.5) then
      nord=3
      npre1=1
      npre2=1
      nt=40
      ntheta=12
      ntau=15
   elseif(ndeep.eq.6) then
      nord=4
      npre1=1
      npre2=1
      nt=95
      ntheta=12
      ntau=15
   endif

   do iorder=1,nord
      misub(1:k-iorder)=0
      misub(k-iorder+1:k)=1
      iflag=k-iorder+1
      do while(iflag .ge.0)
         if(iorder.eq.nord .and. npre1.eq.0) then
            iend=iflag
         else
            iend=1
         endif
         d1=0.
         do n1=iflag,iend,-1
            mi=misub
            mi(n1)=1
            if(any(iand(apmaskr(1:k),mi).eq.1)) cycle
            ntotal=ntotal+1
            me=ieor(m0,mi)
            if(n1.eq.iflag) then
               call mrbencode101(me,ce,g2,N,k)
               e2sub=ieor(ce(k+1:N),hdec(k+1:N))
               e2=e2sub
               nd1kpt=sum(e2sub(1:nt))+1
               d1=sum(ieor(me(1:k),hdec(1:k))*absrx(1:k))
            else
               e2=ieor(e2sub,g2(k+1:N,n1))
               nd1kpt=sum(e2(1:nt))+2
            endif
            if(nd1kpt .le. ntheta) then
               call mrbencode101(me,ce,g2,N,k)
               nxor=ieor(ce,hdec)
               if(n1.eq.iflag) then
                  dd=d1+sum(e2sub*absrx(k+1:N))
               else
                  dd=d1+ieor(ce(n1),hdec(n1))*absrx(n1)+sum(e2*absrx(k+1:N))
               endif
               if( dd .lt. dmin ) then
                  dmin=dd
                  cw=ce
                  nhardmin=sum(nxor)
                  nd1kptbest=nd1kpt
               endif
            else
               nrejected=nrejected+1
            endif
         enddo
! Get the next test error pattern, iflag will go negative
! when the last pattern with weight iorder has been generated.
         call nextpat101(misub,k,iorder,iflag)
      enddo
   enddo

   if(npre2.eq.1) then
      reset=.true.
      ntotal=0
      do i1=k,1,-1
         do i2=i1-1,1,-1
            ntotal=ntotal+1
            mi(1:ntau)=ieor(g2(k+1:k+ntau,i1),g2(k+1:k+ntau,i2))
            call boxit101(reset,mi(1:ntau),ntau,ntotal,i1,i2)
         enddo
      enddo

      ncount2=0
      ntotal2=0
      reset=.true.
! Now run through again and do the second pre-processing rule
      misub(1:k-nord)=0
      misub(k-nord+1:k)=1
      iflag=k-nord+1
      do while(iflag .ge.0)
         me=ieor(m0,misub)
         call mrbencode101(me,ce,g2,N,k)
         e2sub=ieor(ce(k+1:N),hdec(k+1:N))
         do i2=0,ntau
            ntotal2=ntotal2+1
            ui=0
            if(i2.gt.0) ui(i2)=1
            r2pat=ieor(e2sub,ui)
778         continue
            call fetchit101(reset,r2pat(1:ntau),ntau,in1,in2)
            if(in1.gt.0.and.in2.gt.0) then
               ncount2=ncount2+1
               mi=misub
               mi(in1)=1
               mi(in2)=1
               if(sum(mi).lt.nord+npre1+npre2.or.any(iand(apmaskr(1:k),mi).eq.1)) cycle
               me=ieor(m0,mi)
               call mrbencode101(me,ce,g2,N,k)
               nxor=ieor(ce,hdec)
               dd=sum(nxor*absrx)
               if( dd .lt. dmin ) then
                  dmin=dd
                  cw=ce
                  nhardmin=sum(nxor)
               endif
               goto 778
            endif
         enddo
         call nextpat101(misub,k,nord,iflag)
      enddo
   endif

998 continue
! Re-order the codeword to [message bits][parity bits] format.
   cw(indices)=cw
   hdec(indices)=hdec
   message101=cw(1:101)
   call get_crc24(message101,101,nbadcrc)
   if(nbadcrc.ne.0) nhardmin=-nhardmin

   return
end subroutine osd240_101

subroutine mrbencode101(me,codeword,g2,N,K)
   integer*1 me(K),codeword(N),g2(N,K)
! fast encoding for low-weight test patterns
   codeword=0
   do i=1,K
      if( me(i) .eq. 1 ) then
         codeword=ieor(codeword,g2(1:N,i))
      endif
   enddo
   return
end subroutine mrbencode101

subroutine nextpat101(mi,k,iorder,iflag)
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
end subroutine nextpat101

subroutine boxit101(reset,e2,ntau,npindex,i1,i2)
   integer*1 e2(1:ntau)
   integer   indexes(5000,2),fp(0:525000),np(5000)
   logical reset
   common/boxes/indexes,fp,np

   if(reset) then
      patterns=-1
      fp=-1
      np=-1
      sc=-1
      indexes=-1
      reset=.false.
   endif

   indexes(npindex,1)=i1
   indexes(npindex,2)=i2
   ipat=0
   do i=1,ntau
      if(e2(i).eq.1) then
         ipat=ipat+ishft(1,ntau-i)
      endif
   enddo

   ip=fp(ipat)   ! see what's currently stored in fp(ipat)
   if(ip.eq.-1) then
      fp(ipat)=npindex
   else
      do while (np(ip).ne.-1)
         ip=np(ip)
      enddo
      np(ip)=npindex
   endif
   return
end subroutine boxit101

subroutine fetchit101(reset,e2,ntau,i1,i2)
   integer   indexes(5000,2),fp(0:525000),np(5000)
   integer   lastpat
   integer*1 e2(ntau)
   logical reset
   common/boxes/indexes,fp,np
   save lastpat,inext

   if(reset) then
      lastpat=-1
      reset=.false.
   endif

   ipat=0
   do i=1,ntau
      if(e2(i).eq.1) then
         ipat=ipat+ishft(1,ntau-i)
      endif
   enddo
   index=fp(ipat)

   if(lastpat.ne.ipat .and. index.gt.0) then ! return first set of indices
      i1=indexes(index,1)
      i2=indexes(index,2)
      inext=np(index)
   elseif(lastpat.eq.ipat .and. inext.gt.0) then
      i1=indexes(inext,1)
      i2=indexes(inext,2)
      inext=np(inext)
   else
      i1=-1
      i2=-1
      inext=-1
   endif
   lastpat=ipat
   return
end subroutine fetchit101

