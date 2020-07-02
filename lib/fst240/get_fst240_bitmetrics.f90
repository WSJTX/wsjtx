subroutine get_fst240_bitmetrics(cd,nss,hmod,nmax,bitmetrics,s4,badsync)

   include 'fst240_params.f90'
   complex cd(0:NN*nss-1)
   complex cs(0:3,NN)
   complex csymb(nss)
   complex, allocatable, save :: c1(:,:)   ! ideal waveforms, 20 samples per symbol, 4 tones
   complex cp(0:3)        ! accumulated phase shift over symbol types 0:3
   complex csum,cterm
   integer isyncword1(0:7),isyncword2(0:7)
   integer graymap(0:3)
   integer ip(1)
   integer hmod
   logical one(0:65535,0:15)    ! 65536 8-symbol sequences, 16 bits
   logical first
   logical badsync
   real bitmetrics(2*NN,4)
   real s2(0:65535)
   real s4(0:3,NN)
   data isyncword1/0,1,3,2,1,0,2,3/
   data isyncword2/2,3,1,0,3,2,0,1/
   data graymap/0,1,3,2/
   data first/.true./,nss0/-1/
   save first,one,cp,nss0

   if(nss.ne.nss0 .and. allocated(c1)) deallocate(c1)
   if(first .or. nss.ne.nss0) then
      allocate(c1(nss,0:3))
      one=.false.
      do i=0,65535
         do j=0,15
            if(iand(i,2**j).ne.0) one(i,j)=.true.
         enddo
      enddo
      twopi=8.0*atan(1.0)
      dphi=twopi*hmod/nss
      do itone=0,3
         dp=(itone-1.5)*dphi
         phi=0.0
         do j=1,nss
            c1(j,itone)=cmplx(cos(phi),sin(phi))
            phi=mod(phi+dp,twopi)
         enddo
         cp(itone)=cmplx(cos(phi),sin(phi))
      enddo
      first=.false.
   endif

   do k=1,NN
      i1=(k-1)*NSS
      csymb=cd(i1:i1+NSS-1)
      do itone=0,3
         cs(itone,k)=sum(csymb*conjg(c1(:,itone)))
      enddo
      s4(0:3,k)=abs(cs(0:3,k))
   enddo

! Sync quality check
   is1=0
   is2=0
   is3=0
   is4=0
   is5=0
   badsync=.false.
   ibmax=0

   do k=1,8
      ip=maxloc(s4(:,k))
      if(isyncword1(k-1).eq.(ip(1)-1)) is1=is1+1
      ip=maxloc(s4(:,k+38))
      if(isyncword2(k-1).eq.(ip(1)-1)) is2=is2+1
      ip=maxloc(s4(:,k+76))
      if(isyncword1(k-1).eq.(ip(1)-1)) is3=is3+1
      ip=maxloc(s4(:,k+114))
      if(isyncword2(k-1).eq.(ip(1)-1)) is4=is4+1
      ip=maxloc(s4(:,k+152))
      if(isyncword1(k-1).eq.(ip(1)-1)) is5=is5+1
   enddo
   nsync=is1+is2+is3+is4+is5   !Number of correct hard sync symbols, 0-40
   badsync=.false.
   if(nsync .lt. 16) then
      badsync=.true.
      return
   endif

   bitmetrics=0.0
   do nseq=1,nmax            !Try coherent sequences of 1, 2, and 4 symbols
      if(nseq.eq.1) nsym=1
      if(nseq.eq.2) nsym=2
      if(nseq.eq.3) nsym=3
      if(nseq.eq.4) nsym=4
!      if(nseq.eq.3) nsym=4
!      if(nseq.eq.4) nsym=8
      nt=4**nsym
      do ks=1,NN-nsym+1,nsym  
         s2=0
         do i=0,nt-1
            csum=0
            cterm=1
            do j=0,nsym-1
               ntone=mod(i/4**(nsym-1-j),4)
               csum=csum+cs(graymap(ntone),ks+j)*cterm
               cterm=cterm*conjg(cp(graymap(ntone)))
            enddo
            s2(i)=abs(csum)
         enddo
         ipt=1+(ks-1)*2
         if(nsym.eq.1) ibmax=1
         if(nsym.eq.2) ibmax=3
         if(nsym.eq.3) ibmax=5
         if(nsym.eq.4) ibmax=7
         if(nsym.eq.8) ibmax=15
         do ib=0,ibmax
            bm=maxval(s2(0:nt-1),one(0:nt-1,ibmax-ib)) - &
               maxval(s2(0:nt-1),.not.one(0:nt-1,ibmax-ib))
            if(ipt+ib.gt.2*NN) cycle
            bitmetrics(ipt+ib,nseq)=bm
         enddo
      enddo
   enddo

   call normalizebmet(bitmetrics(:,1),2*NN)
   call normalizebmet(bitmetrics(:,2),2*NN)
   call normalizebmet(bitmetrics(:,3),2*NN)
   call normalizebmet(bitmetrics(:,4),2*NN)
   return

end subroutine get_fst240_bitmetrics
