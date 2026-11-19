module osd240_101_module
   use fst4_osd_workspace, only: fst4_osd_workspace_type
   private
   public :: osd240_101_owned
contains
subroutine osd240_101_owned(work,llr,k,apmask,ndeep,message101,cw,nhardmin,dmin)
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
   type(fst4_osd_workspace_type), intent(inout), target :: work
   integer :: iscratch,jscratch
   character*24 c24
   integer, parameter:: N=240
   integer*1 apmask(N),apmaskr(N)
   integer*1, pointer, contiguous :: gen(:,:)
   integer*1, pointer, contiguous :: genmrb(:,:),g2(:,:)
   integer*1, pointer, contiguous :: temp(:),m0(:),me(:),mi(:),misub(:),e2sub(:),e2(:),ui(:)
   integer*1, pointer, contiguous :: r2pat(:)
   integer indices(N),nxor(N)
   integer*1 cw(N),ce(N),c0(N),hdec(N)
   integer*1, pointer, contiguous :: decoded(:)
   integer*1 message101(101)
   integer indx(N)
   real llr(N),rx(N),absrx(N)

   logical reset,systematic

   call work%ensure()
   genmrb(1:k,1:N)=>work%matrix(1:k*N)
   g2(1:N,1:k)=>work%transpose_matrix(1:k*N)
   temp=>work%vectors(1:k,1)
   m0=>work%vectors(1:k,2)
   me=>work%vectors(1:k,3)
   mi=>work%vectors(1:k,4)
   misub=>work%vectors(1:k,5)
   e2sub=>work%vectors(1:N-k,6)
   e2=>work%vectors(1:N-k,7)
   ui=>work%vectors(1:N-k,8)
   r2pat=>work%vectors(1:N-k,9)
   decoded=>work%vectors(1:k,10)
   if(.not.allocated(work%generator(k)%matrix)) then ! fill the generator matrix
!
! Create generator matrix for partial CRC cascaded with LDPC code.
! 
! Let p2=101-k and p1+p2=24. 
!
! The last p2 bits of the CRC24 are cascaded with the LDPC code.
! 
! The first p1=k-77 CRC24 bits will be used for error detection.
!
      allocate(work%generator(k)%matrix(k,N))
      gen=>work%generator(k)%matrix
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

   endif

   gen=>work%generator(k)%matrix

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
      do jscratch=1,k
         genmrb(jscratch,i)=gen(jscratch,indx(N+1-i))
      enddo
      indices(i)=indx(N+1-i)
   enddo

! Do gaussian elimination to create a generator matrix with the most reliable
! received bits in positions 1:k in order of decreasing reliability (more or less).
   systematic=.true.
   pivot_rows: do id=1,k
      do icol=id,k+20  ! The 20 is ad hoc - beware
         if( genmrb(id,icol) .eq. 1 ) then
            if( icol .ne. id ) then ! reorder column
               do jscratch=1,k
                  temp(jscratch)=genmrb(jscratch,id)
                  genmrb(jscratch,id)=genmrb(jscratch,icol)
                  genmrb(jscratch,icol)=temp(jscratch)
               enddo
               itmp=indices(id)
               indices(id)=indices(icol)
               indices(icol)=itmp
            endif
            do ii=1,k
               if( ii .ne. id .and. genmrb(ii,id) .eq. 1 ) then
                  do jscratch=1,N
                     genmrb(ii,jscratch)=ieor(genmrb(ii,jscratch),genmrb(id,jscratch))
                  enddo
               endif
            enddo
            cycle pivot_rows
         endif
      enddo
      systematic=.false.
   enddo pivot_rows

   do iscratch=1,k
      do jscratch=1,N
         g2(jscratch,iscratch)=genmrb(iscratch,jscratch)
      enddo
   enddo

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

   nord=0
   if(ndeep.le.0) goto 998  ! norder=0
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
            do iscratch=1,k
               mi(iscratch)=misub(iscratch)
            enddo
            mi(n1)=1
            if(any(iand(apmaskr(1:k),mi).eq.1)) cycle
            ntotal=ntotal+1
            do iscratch=1,k
               me(iscratch)=ieor(m0(iscratch),mi(iscratch))
            enddo
            if(n1.eq.iflag) then
               call mrbencode101(me,ce,g2,N,k)
               e2sub=ieor(ce(k+1:N),hdec(k+1:N))
               do iscratch=1,N-k
                  e2(iscratch)=e2sub(iscratch)
               enddo
               nd1kpt=sum(e2sub(1:nt))+1
               d1=sum(ieor(me(1:k),hdec(1:k))*absrx(1:k))
            else
               do iscratch=1,N-k
                  e2(iscratch)=ieor(e2sub(iscratch),g2(k+iscratch,n1))
               enddo
               nd1kpt=sum(e2(1:nt))+2
            endif
            if(nd1kpt .le. ntheta) then
               call mrbencode101(me,ce,g2,N,k)
               nxor=ieor(ce,hdec)
               if(.not.systematic) then
                  dd=sum(nxor*absrx)
               elseif(n1.eq.iflag) then
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
            do iscratch=1,ntau
               mi(iscratch)=ieor(g2(k+iscratch,i1),g2(k+iscratch,i2))
            enddo
            call boxit101(work,reset,mi(1:ntau),ntau,ntotal,i1,i2)
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
         do iscratch=1,k
            me(iscratch)=ieor(m0(iscratch),misub(iscratch))
         enddo
         call mrbencode101(me,ce,g2,N,k)
         e2sub=ieor(ce(k+1:N),hdec(k+1:N))
         do i2=0,ntau
            ntotal2=ntotal2+1
            ui=0
            if(i2.gt.0) ui(i2)=1
            do iscratch=1,N-k
               r2pat(iscratch)=ieor(e2sub(iscratch),ui(iscratch))
            enddo
778         continue
            call fetchit101(work,reset,r2pat(1:ntau),ntau,in1,in2)
            if(in1.gt.0.and.in2.gt.0) then
               ncount2=ncount2+1
               do iscratch=1,k
                  mi(iscratch)=misub(iscratch)
               enddo
               mi(in1)=1
               mi(in2)=1
               if(sum(mi).lt.nord+npre1+npre2.or.any(iand(apmaskr(1:k),mi).eq.1)) cycle
               do iscratch=1,k
                  me(iscratch)=ieor(m0(iscratch),mi(iscratch))
               enddo
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
end subroutine osd240_101_owned

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
   integer*1 mi(k),ms(101)
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
   mi=ms(1:k)
   do i=1,k  ! iflag will point to the lowest-index 1 in mi
      if(mi(i).eq.1) then
         iflag=i
         exit
      endif
   enddo
   return
end subroutine nextpat101

subroutine boxit101(work,reset,e2,ntau,npindex,i1,i2)
   type(fst4_osd_workspace_type), intent(inout) :: work
   integer*1 e2(1:ntau)
   logical reset

   call work%ensure_boxes()
   if(reset) then
      work%box_first=-1
      work%box_next=-1
      work%box_indices=-1
      reset=.false.
   endif

   work%box_indices(npindex,1)=i1
   work%box_indices(npindex,2)=i2
   ipat=0
   do i=1,ntau
      if(e2(i).eq.1) then
         ipat=ipat+ishft(1,ntau-i)
      endif
   enddo

   ip=work%box_first(ipat)   ! see what's currently stored in work%box_first(ipat)
   if(ip.eq.-1) then
      work%box_first(ipat)=npindex
   else
      do while (work%box_next(ip).ne.-1)
         ip=work%box_next(ip)
      enddo
      work%box_next(ip)=npindex
   endif
   return
end subroutine boxit101

subroutine fetchit101(work,reset,e2,ntau,i1,i2)
   type(fst4_osd_workspace_type), intent(inout) :: work
   integer*1 e2(ntau)
   logical reset

   if(reset) then
      work%last_pattern=-1
      reset=.false.
   endif

   ipat=0
   do i=1,ntau
      if(e2(i).eq.1) then
         ipat=ipat+ishft(1,ntau-i)
      endif
   enddo
   index=work%box_first(ipat)

   if(work%last_pattern.ne.ipat .and. index.gt.0) then ! return first set of indices
      i1=work%box_indices(index,1)
      i2=work%box_indices(index,2)
      work%next_index=work%box_next(index)
   elseif(work%last_pattern.eq.ipat .and. work%next_index.gt.0) then
      i1=work%box_indices(work%next_index,1)
      i2=work%box_indices(work%next_index,2)
      work%next_index=work%box_next(work%next_index)
   else
      i1=-1
      i2=-1
      work%next_index=-1
   endif
   work%last_pattern=ipat
   return
end subroutine fetchit101

end module osd240_101_module

subroutine osd240_101(llr,k,apmask,ndeep,message101,cw,nhardmin,dmin)
   use osd240_101_module, only: osd240_101_owned
   use fst4_osd_workspace, only: fst4_osd_workspace_type
   type(fst4_osd_workspace_type), save :: legacy
   real llr(240),dmin
   integer*1 apmask(240),message101(101),cw(240)
   call osd240_101_owned(legacy,llr,k,apmask,ndeep,message101,cw,nhardmin,dmin)
end subroutine
