subroutine osd174_91(llr,k,apmask,ndeep,message91,cw,nhardmin,dmin)
!
! An ordered-statistics decoder for the (174,91) code.
! Message payload is 77 bits.  Any or all of a 14-bit CRC can be used for
! detecting incorrect codewords; the remaining CRC bits are cascaded with the
! LDPC code.  If p1 (0.le.p1.le.14) CRC bits are used for bad codeword
! detection, k must be 77+p1, so the valid range of k is [77,91].
!
! ndeep=1 and ndeep=2 with 77.le.k.le.91 and finite llr run on bit-packed
! vectors in osd174_91_fast; FT8 and FT4 call it with ndeep=2 and k=91.  Every
! other setting, and any llr that is NaN or infinite, runs the original
! byte-array code, osd174_91_slow.  For a given k the two give identical
! results.
!
   implicit none
   integer, parameter:: N=174
   integer k,ndeep,nhardmin
   integer*1 apmask(N),cw(N),message91(91)
   real llr(N),dmin

   if((ndeep.eq.1 .or. ndeep.eq.2) .and. k.ge.77 .and. k.le.91 .and. &
      all(abs(llr).le.huge(llr))) then
      call osd174_91_fast(llr,k,apmask,ndeep,message91,cw,nhardmin,dmin)
   else
      call osd174_91_slow(llr,k,apmask,ndeep,message91,cw,nhardmin,dmin)
   endif
   return
end subroutine osd174_91

subroutine osd174_91_gen(k,g)
! Generator matrix for the partial-CRC cascaded code: g(:,c) holds the k bits
! of column c, message bit i in bit i-1 of word (i-1)/64+1.
   use iso_fortran_env, only: int64
   implicit none
   integer, parameter:: N=174
   integer k
   integer(int64) g(2,N)
   character*14 c14
   integer*1 msg(91),cwl(N),m96(96)
   integer i,c,ncrc14

   g=0
   do i=1,k
      msg=0
      msg(i)=1
      if(i.le.77) then
         m96=0
         m96(1:91)=msg
         call get_crc14(m96,96,ncrc14)
         write(c14,'(b14.14)') ncrc14
         read(c14,'(14i1)') msg(78:91)
         msg(78:k)=0
      endif
      call encode174_91_nocrc(msg,cwl)
      do c=1,N
         if(cwl(c).eq.1) g((i-1)/64+1,c)=ibset(g((i-1)/64+1,c),mod(i-1,64))
      enddo
   enddo
   return
end subroutine osd174_91_gen

subroutine osd174_91_fast(llr,k,apmask,ndeep,message91,cw,nhardmin,dmin)
!
! osd174_91 for ndeep=1,2 on bit-packed columns.  The generator matrix is kept
! as 174 columns of k bits (two 64-bit words each), so a row operation of the
! Gaussian elimination is one masked XOR per column, and the test patterns are
! screened with XOR and a bit count on the first nt parity bits.  The order and
! values of every comparison and every floating-point sum are those of
! osd174_91_slow.
!
   use iso_fortran_env, only: int64
   use first_call_tsan, only: wsjt_tsan_acquire_first_call,wsjt_tsan_release_first_call
   implicit none
   integer, parameter:: N=174
   integer, parameter:: NT=40
   integer, intent(in) :: k,ndeep
   real, intent(in) :: llr(N)
   integer*1, intent(in) :: apmask(N)
   integer*1, intent(out) :: message91(91),cw(N)
   integer, intent(out) :: nhardmin
   real, intent(out) :: dmin

   integer(int64), save :: genw(2,N,77:91)
   integer, save :: nbuilt=0
   integer(int64) cols(2,N),s(2),m0w(2),rp(91),e20w,e2s,x,mk
   integer indx(N),indices(N),ntheta,id,icol,iw,ib,c,i,j,p,ip,n1,itmp,nbadcrc,b
   real absrx(N),absr(N),d1,dd,tt,ss
   integer*1 hd(N),c0b(N),ceb(N),apr(91),m96(96)
   integer mask

   !$omp atomic read seq_cst
   mask=nbuilt
   if(.not.btest(mask,k-77)) then
      !$omp critical(first_call)
      call wsjt_tsan_acquire_first_call()
      if(.not.btest(nbuilt,k-77)) then
         call osd174_91_gen(k,genw(:,:,k))
         mask=ior(nbuilt,ibset(0,k-77))
         !$omp atomic write seq_cst
         nbuilt=mask
      endif
      call wsjt_tsan_release_first_call()
      !$omp end critical(first_call)
   endif

! Reliability order of the received bits.
   absrx=abs(llr)
   call indexx(absrx,N,indx)
   do i=1,N
      indices(i)=indx(N+1-i)
      cols(1,i)=genw(1,indices(i),k)
      cols(2,i)=genw(2,indices(i),k)
   enddo

! Gaussian elimination: bring the most reliable independent columns to
! positions 1:k as unit vectors.  Column c is stored with row i in bit i-1;
! "row ii = row ii + row id for every row ii (not id) that has a 1 in the pivot
! column" becomes, for each column c with a 1 in row id, column c = column c +
! (pivot column without bit id).
   do id=1,k
      iw=(id-1)/64+1
      ib=mod(id-1,64)
      do icol=id,k+20   ! the pivot search limit of osd174_91_slow
         if(btest(cols(iw,icol),ib)) then
            if(icol.ne.id) then
               s=cols(:,id)
               cols(:,id)=cols(:,icol)
               cols(:,icol)=s
               itmp=indices(id)
               indices(id)=indices(icol)
               indices(icol)=itmp
            endif
            s=cols(:,id)
            s(iw)=ibclr(s(iw),ib)
            do c=1,N
               mk=-iand(ishft(cols(iw,c),-ib),1_int64)
               cols(1,c)=ieor(cols(1,c),iand(mk,s(1)))
               cols(2,c)=ieor(cols(2,c),iand(mk,s(2)))
            enddo
            exit
         endif
      enddo
   enddo

! Received values in the new column order.
   do i=1,N
      absr(i)=absrx(indices(i))
      if(llr(indices(i)).ge.0.) then
         hd(i)=1
      else
         hd(i)=0
      endif
   enddo
   do i=1,k
      apr(i)=apmask(indices(i))
   enddo

! Order-0 message from the k hard decisions, and its codeword.
   m0w=0
   do i=1,k
      if(hd(i).eq.1) then
         iw=(i-1)/64+1
         m0w(iw)=ibset(m0w(iw),mod(i-1,64))
      endif
   enddo
   do c=1,N
      x=ieor(iand(m0w(1),cols(1,c)),iand(m0w(2),cols(2,c)))
      x=ieor(x,ishft(x,-32))
      x=ieor(x,ishft(x,-16))
      x=ieor(x,ishft(x,-8))
      x=ieor(x,ishft(x,-4))
      x=ieor(x,ishft(x,-2))
      x=ieor(x,ishft(x,-1))
      c0b(c)=int(iand(x,1_int64),1)
   enddo
   nhardmin=0
   dmin=0.
   do c=1,N
      if(c0b(c).ne.hd(c)) then
         nhardmin=nhardmin+1
         dmin=dmin+absr(c)
      endif
   enddo
   cw=c0b

! Row i of the first nt parity columns, one bit per column.
   rp=0
   do j=1,NT
      c=k+j
      do iw=1,2
         x=cols(iw,c)
         do while(x.ne.0)
            b=trailz(x)
            p=(iw-1)*64+b+1
            rp(p)=ibset(rp(p),j-1)
            x=ibclr(x,b)
         enddo
      enddo
   enddo
   e20w=0
   do j=1,NT
      if(c0b(k+j).ne.hd(k+j)) e20w=ibset(e20w,j-1)
   enddo

   if(ndeep.eq.1) then
      ntheta=12
   else
      ntheta=10
   endif

! Test patterns: each single flip of message bit ip (ip=k down to 1) and, for
! ndeep=2, the pairs (ip,n1) with n1 from ip-1 down to 1.
   do ip=k,1,-1
      if(iand(int(apr(ip)),1).eq.1) cycle
      e2s=ieor(e20w,rp(ip))
      d1=absr(ip)
      if(pc64(e2s)+1.le.ntheta) call try(ip,0)
      if(ndeep.eq.2) then
         do n1=ip-1,1,-1
            if(iand(int(apr(n1)),1).eq.1) cycle
            if(pc64(ieor(e2s,rp(n1)))+2.le.ntheta) call try(ip,n1)
         enddo
      endif
   enddo

! Re-order the codeword to [message bits][parity bits] format.
   cw(indices)=cw
   message91=cw(1:91)
   m96=0
   m96(1:77)=cw(1:77)
   m96(83:96)=cw(78:91)
   call get_crc14(m96,96,nbadcrc)
   if(nbadcrc.ne.0) nhardmin=-nhardmin
   return

contains

   integer function pc64(v)
! Bit count without assuming a hardware popcount instruction.
      integer(int64), intent(in) :: v
      integer(int64) y
      y=v-iand(ishft(v,-1),z'5555555555555555')
      y=iand(y,z'3333333333333333')+iand(ishft(y,-2),z'3333333333333333')
      y=iand(y+ishft(y,-4),z'0F0F0F0F0F0F0F0F')
      y=y+ishft(y,-8)
      y=y+ishft(y,-16)
      y=y+ishft(y,-32)
      pc64=int(iand(y,127_int64))
   end function pc64

   subroutine try(ia,ib2)
! Full codeword for pattern (ia) or (ia,ib2): order-0 codeword plus rows ia and ib2.
      integer, intent(in) :: ia,ib2
      integer cc,jw,jb,jj,nh
      ceb=c0b
      jw=(ia-1)/64+1
      jb=mod(ia-1,64)
      do cc=1,N
         if(btest(cols(jw,cc),jb)) ceb(cc)=ieor(ceb(cc),1_1)
      enddo
      if(ib2.gt.0) then
         jw=(ib2-1)/64+1
         jb=mod(ib2-1,64)
         do cc=1,N
            if(btest(cols(jw,cc),jb)) ceb(cc)=ieor(ceb(cc),1_1)
         enddo
      endif
      ss=0.
      do jj=k+1,N
         if(ceb(jj).ne.hd(jj)) ss=ss+absr(jj)
      enddo
      if(ib2.eq.0) then
         dd=d1+ss
      else
         tt=0.
         if(ceb(ib2).ne.hd(ib2)) tt=absr(ib2)
         dd=d1+tt+ss
      endif
      if(dd.lt.dmin) then
         dmin=dd
         cw=ceb
         nh=0
         do cc=1,N
            if(ceb(cc).ne.hd(cc)) nh=nh+1
         enddo
         nhardmin=nh
      endif
   end subroutine try

end subroutine osd174_91_fast

subroutine osd174_91_slow(llr,k,apmask,ndeep,message91,cw,nhardmin,dmin)
!
! Byte-array implementation of osd174_91, used for the settings that
! osd174_91_fast does not handle.  Arguments as for osd174_91.
!
   character*14 c14
   integer, parameter:: N=174
   integer*1 apmask(N),apmaskr(N)
   integer*1, allocatable, save :: gen(:,:)
   integer*1, allocatable :: genmrb(:,:),g2(:,:)
   integer*1, allocatable :: temp(:),m0(:),me(:),mi(:),misub(:),e2sub(:),e2(:),ui(:)
   integer*1, allocatable :: r2pat(:)
   integer indices(N),nxor(N)
   integer*1 cw(N),ce(N),c0(N),hdec(N)
   integer*1, allocatable :: decoded(:)
   integer*1 message91(91),m96(96)
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
! Let p2=91-k and p1+p2=14. 
!
! The last p2 bits of the CRC14 are cascaded with the LDPC code.
! 
! The first p1=k-77 CRC14 bits will be used for error detection.
!
      allocate( gen(k,N) )
      gen=0
      do i=1,k
         message91=0
         message91(i)=1
         if(i.le.77) then
            m96=0
            m96(1:91)=message91
            call get_crc14(m96,96,ncrc14)
            write(c14,'(b14.14)') ncrc14
            read(c14,'(14i1)') message91(78:91)
            message91(78:k)=0
         endif
         call encode174_91_nocrc(message91,cw)
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

   call mrbencode91(m0,c0,g2,N,k)
   nxor=ieor(c0,hdec)
   nhardmin=sum(nxor)
   dmin=sum(nxor*absrx)

   cw=c0
   ntotal=0
   nrejected=0
   npre1=0
   npre2=0

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
!      ntheta=12
      ntheta=10
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
   else                     !ndeep=6
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
               call mrbencode91(me,ce,g2,N,k)
               e2sub=ieor(ce(k+1:N),hdec(k+1:N))
               e2=e2sub
               nd1kpt=sum(e2sub(1:nt))+1
               d1=sum(ieor(me(1:k),hdec(1:k))*absrx(1:k))
            else
               e2=ieor(e2sub,g2(k+1:N,n1))
               nd1kpt=sum(e2(1:nt))+2
            endif
            if(nd1kpt .le. ntheta) then
               call mrbencode91(me,ce,g2,N,k)
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
         call nextpat91(misub,k,iorder,iflag)
      enddo
   enddo

   if(npre2.eq.1) then
      reset=.true.
      ntotal=0
      do i1=k,1,-1
         do i2=i1-1,1,-1
            ntotal=ntotal+1
            mi(1:ntau)=ieor(g2(k+1:k+ntau,i1),g2(k+1:k+ntau,i2))
            call boxit91(reset,mi(1:ntau),ntau,ntotal,i1,i2)
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
         call mrbencode91(me,ce,g2,N,k)
         e2sub=ieor(ce(k+1:N),hdec(k+1:N))
         do i2=0,ntau
            ntotal2=ntotal2+1
            ui=0
            if(i2.gt.0) ui(i2)=1
            r2pat=ieor(e2sub,ui)
778         continue
            call fetchit91(reset,r2pat(1:ntau),ntau,in1,in2)
            if(in1.gt.0.and.in2.gt.0) then
               ncount2=ncount2+1
               mi=misub
               mi(in1)=1
               mi(in2)=1
               if(sum(mi).lt.nord+npre1+npre2.or.any(iand(apmaskr(1:k),mi).eq.1)) cycle
               me=ieor(m0,mi)
               call mrbencode91(me,ce,g2,N,k)
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
         call nextpat91(misub,k,nord,iflag)
      enddo
   endif

998 continue
! Re-order the codeword to [message bits][parity bits] format.
   cw(indices)=cw
   hdec(indices)=hdec
   message91=cw(1:91)
   m96=0
   m96(1:77)=cw(1:77)
   m96(83:96)=cw(78:91)
   call get_crc14(m96,96,nbadcrc)
   if(nbadcrc.ne.0) nhardmin=-nhardmin

   return
end subroutine osd174_91_slow

subroutine mrbencode91(me,codeword,g2,N,K)
   integer*1 me(K),codeword(N),g2(N,K)
! fast encoding for low-weight test patterns
   codeword=0
   do i=1,K
      if( me(i) .eq. 1 ) then
         codeword=ieor(codeword,g2(1:N,i))
      endif
   enddo
   return
end subroutine mrbencode91

subroutine nextpat91(mi,k,iorder,iflag)
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
end subroutine nextpat91

subroutine boxit91(reset,e2,ntau,npindex,i1,i2)
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
end subroutine boxit91

subroutine fetchit91(reset,e2,ntau,i1,i2)
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
end subroutine fetchit91

