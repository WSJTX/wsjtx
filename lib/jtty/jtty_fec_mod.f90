module jtty_fec

! This sync sequence has peak sidelobe level of 3/13 and only 4 such sidelobes
! over the entire 25x7 lag space.
   integer :: is13(13) = [1,3,0,0,3,2,0,3,1,2,1,2,3] 

contains

subroutine get_crc10(mc,len,ncrc)
!
! 1. To calculate 10-bit CRC, mc(1:len-10) is the message and mc(len-9:len) are zero.
! 2. To check a received CRC, mc(1:len) is the received message plus CRC.
!    ncrc will be zero if the received message/CRC are consistent.
!
   character c10*10
   integer*1 mc(len)
   integer*1 r(11),p(11)
   integer ncrc

! polynomial for 10-bit CRC 0x48f (full polynomial, no truncation)
! this is "CRC-10F/4.2" from Koopman's list of good 10-bit CRCs
   data p/1,0,0,1,0,0,0,1,1,1,1/

! divide by polynomial
   r=mc(1:11)
   do i=0,len-11
      r(11)=mc(i+11)
      r=mod(r+r(1)*p,2)
      r=cshift(r,1)
   enddo

   write(c10,'(10b1)') r(1:10)
   read(c10,'(b10.10)') ncrc

end subroutine get_crc10

subroutine encode_80_32(message32,codeword)

  integer, parameter:: N=80, K=42, M=N-K
  character c10*10
  integer*1 codeword(N)
  integer*1 gen(M,K)
  integer*1 message32(32)
  integer*1 message(42)
  integer*1 pchecks(M)
  include "jtty_generator_80_42.f90"

  message(1:32)=message32(:)
  message(33:42)=0

  call get_crc10(message,42,ncrc10)

  write(c10,'(b10.10)') ncrc10
  read(c10,'(10i1)') message(33:42)

  do i=1,M
    pchecks(i)=mod( sum(message*gen(i,:)), 2)
  enddo

  codeword(1:42)=message
  codeword(43:80)=pchecks

  return

end subroutine encode_80_32

subroutine encode_80_42(message,codeword)

  integer, parameter:: N=80, K=42, M=N-K
  integer*1 codeword(N)
  integer*1 gen(M,K)
  integer*1 message(K)
  integer*1 pchecks(M)
  include "jtty_generator_80_42.f90"

  do i=1,M
    pchecks(i)=mod( sum(message*gen(i,:)), 2)
  enddo

  codeword(1:42)=message
  codeword(43:80)=pchecks

  return

end subroutine encode_80_42

subroutine bpdecode_80_32(llr,maxiterations,message32,cw,nharderror)
!
! A log-domain belief propagation decoder for the (80,42) code.
!
! We check crc consistency in the decoder and return a 32-bit payload
! if CRC is good.
!
  integer, parameter:: N=80, K=42, M=N-K
  integer*1 cw(N)
  integer*1 message(42)
  integer*1 message32(32)
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
        message32=cw(1:32)
        nharderror=count( (2*cw-1)*llr .lt. 0.0 )
        call get_crc10(message,42,ncrc)
        if(ncrc.ne.0) nharderror=-1
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

end subroutine bpdecode_80_32

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

subroutine osd80_32(llr,ndeep,message32,cw,nhardmin,dmin)
! Ordered-statistics decoder for the (80,42) code.

  integer, parameter:: N=80, K=42, M=N-K
  integer*1 gen(M,K)
  integer*1 generator(K,N)
  integer*1 genmrb(K,N),g2(N,K)
  integer*1 temp(K),m0(K),me(K),mi(K),misub(K),e2sub(N-K),e2(N-K),ui(N-K)
  integer*1 r2pat(N-K)
  integer indices(N),nxor(N)
  integer*1 cw(N),ce(N),c0(N),hdec(N)
  integer*1 decoded(K)
  integer*1 message32(32)
  integer indx(N)
  real llr(N),rx(N),absrx(N)

  include "jtty_generator_80_42.f90"     ! This gives us gen(M,K)

  logical first,reset
  data first/.true./
  save first,generator

  if( first ) then ! fill the generator matrix
     generator=0
     do i=1,K
        generator(i,i)=1
     enddo
     generator(:,K+1:N)=transpose(gen)
     first=.false.
  endif

  rx=llr
  d1=0.
  ntheta=0
  npre1=0
  npre2=0
  nt=0

! Hard decisions on the received word.
  hdec=0            
  where(rx .ge. 0) hdec=1

! Use magnitude of received symbols as a measure of reliability.
  absrx=abs(rx) 
  call indexx(absrx,N,indx)  

! Re-order columns of generator matrix in order of decreasing reliability.
  do i=1,N
     genmrb(1:K,i)=generator(1:K,indx(N+1-i))
     indices(i)=indx(N+1-i)
  enddo

! Do gaussian elimination to create a generator matrix with the most reliable
! received bits in positions 1:K in order of decreasing reliability (more or less). 
  do id=1,K ! diagonal element indices 
     do icol=id,K+20  ! The 20 is ad hoc - beware
        iflag=0
        if( genmrb(id,icol) .eq. 1 ) then
           iflag=1
           if( icol .ne. id ) then ! reorder column
              temp(1:K)=genmrb(1:K,id)
              genmrb(1:K,id)=genmrb(1:K,icol)
              genmrb(1:K,icol)=temp(1:K) 
              itmp=indices(id)
              indices(id)=indices(icol)
              indices(icol)=itmp
           endif
           do ii=1,K
              if( ii .ne. id .and. genmrb(ii,id) .eq. 1 ) then
                 genmrb(ii,1:N)=ieor(genmrb(ii,1:N),genmrb(id,1:N))
              endif
           enddo
           exit
        endif
     enddo
  enddo

  g2=transpose(genmrb)

  ! The hard decisions for the K MRB bits define the order 0 message, m0. 
! Encode m0 using the modified generator matrix to find the "order 0" codeword. 
! Flip various combinations of bits in m0 and re-encode to generate a list of
! codewords. Return the member of the list that has the smallest Euclidean
! distance to the received word. 

  hdec=hdec(indices)   ! hard decisions from received symbols
  m0=hdec(1:K)         ! zero'th order message
  absrx=absrx(indices) 
  rx=rx(indices)       

  call mrbencode42(m0,c0,g2,N,K)
  nxor=ieor(c0,hdec)
  nhardmin=sum(nxor)
  dmin=sum(nxor*absrx)

  cw=c0
  ntotal=0
  nrejected=0

  if(ndeep.eq.0) goto 998  ! norder=0
  if(ndeep.gt.5) ndeep=5
  if( ndeep.eq. 1) then
     nord=1
     npre1=0
     npre2=0
     nt=12
     ntheta=3
  elseif(ndeep.eq.2) then
     nord=1
     npre1=1
     npre2=0
     nt=18
     ntheta=4
  elseif(ndeep.eq.3) then
     nord=1
     npre1=1
     npre2=0
     nt=12
     ntheta=4
  elseif(ndeep.eq.4) then
     nord=1
     npre1=1
     npre2=1
     nt=12
     ntheta=4
     ntau=15
  elseif(ndeep.eq.5) then
     nord=1
     npre1=1
     npre2=1
     nt=12
     ntheta=4
     ntau=5
  endif

  do iorder=1,nord
     misub(1:K-iorder)=0
     misub(K-iorder+1:K)=1
     iflag=K-iorder+1
     do while(iflag .ge.0)
        if(iorder.eq.nord .and. npre1.eq.0) then
           iend=iflag
        else
           iend=1
        endif
        do n1=iflag,iend,-1
           mi=misub
           mi(n1)=1
           ntotal=ntotal+1
           me=ieor(m0,mi)
           if(n1.eq.iflag) then
              call mrbencode42(me,ce,g2,N,K)
              e2sub=ieor(ce(K+1:N),hdec(K+1:N))
              e2=e2sub
              nd1Kpt=sum(e2sub(1:nt))+1
              d1=sum(ieor(me(1:K),hdec(1:K))*absrx(1:K))
           else
              e2=ieor(e2sub,g2(K+1:N,n1))
              nd1Kpt=sum(e2(1:nt))+2
           endif
           if(nd1Kpt .le. ntheta) then
              call mrbencode42(me,ce,g2,N,K)
              nxor=ieor(ce,hdec)
              if(n1.eq.iflag) then
                 dd=d1+sum(e2sub*absrx(K+1:N))
              else
                 dd=d1+ieor(ce(n1),hdec(n1))*absrx(n1)+sum(e2*absrx(K+1:N))
              endif
              if( dd .lt. dmin ) then
                 dmin=dd
                 cw=ce
                 nhardmin=sum(nxor)
                 nd1Kptbest=nd1Kpt
              endif
           else 
              nrejected=nrejected+1
           endif
        enddo
! Get the next test error pattern, iflag will go negative
! when the last pattern with weight iorder has been generated.
        call nextpat42(misub,k,iorder,iflag)
     enddo
  enddo

  if(npre2.eq.1) then
     reset=.true.
     ntotal=0
     do i1=K,1,-1
        do i2=i1-1,1,-1
           ntotal=ntotal+1
           mi(1:ntau)=ieor(g2(K+1:K+ntau,i1),g2(K+1:K+ntau,i2))
           call boxit42(reset,mi(1:ntau),ntau,ntotal,i1,i2)
        enddo
     enddo

     ncount2=0
     ntotal2=0
     reset=.true.
! Now run through again and do the second pre-processing rule
     misub(1:K-nord)=0
     misub(K-nord+1:K)=1
     iflag=K-nord+1
     do while(iflag .ge.0)
        me=ieor(m0,misub)
        call mrbencode42(me,ce,g2,N,K)
        e2sub=ieor(ce(K+1:N),hdec(K+1:N))
        do i2=0,ntau
           ntotal2=ntotal2+1
           ui=0 
           if(i2.gt.0) ui(i2)=1 
           r2pat=ieor(e2sub,ui)
778        continue 
           call fetchit42(reset,r2pat(1:ntau),ntau,in1,in2)
           if(in1.gt.0.and.in2.gt.0) then
              ncount2=ncount2+1
              mi=misub               
              mi(in1)=1
              mi(in2)=1
              if(sum(mi).lt.nord+npre1+npre2) cycle
              me=ieor(m0,mi)
              call mrbencode42(me,ce,g2,N,K)
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
        call nextpat42(misub,K,nord,iflag)
     enddo
  endif

998 continue

! Re-order the codeword to [message bits][parity bits] format. 
  cw(indices)=cw
  hdec(indices)=hdec

  decoded=cw(1:K) 

  call get_crc10(decoded,42,ncrc10)
  if(ncrc10.ne.0) then
    nhardmin=-nhardmin
  endif
  message32=decoded(1:32)

  return
end subroutine osd80_32

subroutine mrbencode42(me,codeword,g2,N,K)
  integer*1 me(K),codeword(N),g2(N,K)
! fast encoding for low-weight test patterns
  codeword=0
  do i=1,K
     if( me(i) .eq. 1 ) then
        codeword=ieor(codeword,g2(1:N,i))
     endif
  enddo
  return
end subroutine mrbencode42

subroutine nextpat42(mi,k,iorder,iflag)
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
end subroutine nextpat42

subroutine boxit42(reset,e2,ntau,npindex,i1,i2)
  integer*1 e2(1:ntau)
  integer indexes(5000,2),fp(0:525000),np(5000)
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
end subroutine boxit42

subroutine fetchit42(reset,e2,ntau,i1,i2)
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
end subroutine fetchit42

subroutine indexx(arr,n,indx)

  parameter (M=7,NSTACK=50)
  integer n,indx(n)
  real arr(n)
  integer i,indxt,ir,itemp,j,jstack,k,l,istack(NSTACK)
  real a

  do j=1,n
     indx(j)=j
  enddo

  jstack=0
  l=1
  ir=n
1 if(ir-l.lt.M) then
     do j=l+1,ir
        indxt=indx(j)
        a=arr(indxt)
        do i=j-1,1,-1
           if(arr(indx(i)).le.a) goto 2
           indx(i+1)=indx(i)
        enddo
        i=0
2       indx(i+1)=indxt
     enddo
     if(jstack.eq.0) return

     ir=istack(jstack)
     l=istack(jstack-1)
     jstack=jstack-2

  else
     k=(l+ir)/2
     itemp=indx(k)
     indx(k)=indx(l+1)
     indx(l+1)=itemp

     if(arr(indx(l+1)).gt.arr(indx(ir))) then
        itemp=indx(l+1)
        indx(l+1)=indx(ir)
        indx(ir)=itemp
     endif

     if(arr(indx(l)).gt.arr(indx(ir))) then
        itemp=indx(l)
        indx(l)=indx(ir)
        indx(ir)=itemp
     endif

     if(arr(indx(l+1)).gt.arr(indx(l))) then
        itemp=indx(l+1)
        indx(l+1)=indx(l)
        indx(l)=itemp
     endif

     i=l+1
     j=ir
     indxt=indx(l)
     a=arr(indxt)
3    continue
     i=i+1
     if(arr(indx(i)).lt.a) goto 3

4    continue
     j=j-1
     if(arr(indx(j)).gt.a) goto 4
     if(j.lt.i) goto 5
     itemp=indx(i)
     indx(i)=indx(j)
     indx(j)=itemp
     goto 3

5    indx(l)=indx(j)
     indx(j)=indxt
     jstack=jstack+2
     if(jstack.gt.NSTACK) stop 'NSTACK too small in indexx'
     if(ir-i+1.ge.j-l)then
        istack(jstack)=ir
        istack(jstack-1)=i
        ir=j-1
     else
        istack(jstack)=j-1
        istack(jstack-1)=l
        l=i
     endif
  endif
  goto 1

end subroutine indexx

end module jtty_fec
