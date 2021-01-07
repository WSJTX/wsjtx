subroutine get_fst4_bitmetrics2(cd,nss,hmod,nsizes,bitmetrics,s4snr,badsync)

   include 'fst4_params.f90'
   complex cd(0:NN*nss-1)
   complex csymb(nss)
   complex, allocatable, save :: c1(:,:)   ! ideal waveforms, 4 tones
   complex cp(0:3)        ! accumulated phase shift over symbol types 0:3
   integer isyncword1(0:7),isyncword2(0:7)
   integer graymap(0:3)
   integer ip(1)
   integer hmod
   logical one(0:65535,0:15)    ! 65536 8-symbol sequences, 16 bits
   logical first
   logical badsync
   real bitmetrics(2*NN,4)
   real s2(0:65535)
   real s4(0:3,NN,4),s4snr(0:3,NN)
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
         s4(itone,k,1)=abs(sum(csymb*conjg(c1(:,itone))))**2
         s4(itone,k,2)=abs(sum(csymb(      1:nss/2)*conjg(c1(      1:nss/2,itone))))**2 + &
                       abs(sum(csymb(nss/2+1:  nss)*conjg(c1(nss/2+1:  nss,itone))))**2
         s4(itone,k,3)=abs(sum(csymb(        1:  nss/4)*conjg(c1(        1:  nss/4,itone))))**2 + &
                       abs(sum(csymb(  nss/4+1:  nss/2)*conjg(c1(  nss/4+1:  nss/2,itone))))**2 + &
                       abs(sum(csymb(  nss/2+1:3*nss/4)*conjg(c1(  nss/2+1:3*nss/4,itone))))**2 + &
                       abs(sum(csymb(3*nss/4+1:    nss)*conjg(c1(3*nss/4+1:    nss,itone))))**2
         s4(itone,k,4)=abs(sum(csymb(        1:  nss/8)*conjg(c1(        1:  nss/8,itone))))**2 + &
                       abs(sum(csymb(  nss/8+1:  nss/4)*conjg(c1(  nss/8+1:  nss/4,itone))))**2 + &
                       abs(sum(csymb(  nss/4+1:3*nss/8)*conjg(c1(  nss/4+1:3*nss/8,itone))))**2 + &
                       abs(sum(csymb(3*nss/8+1:  nss/2)*conjg(c1(3*nss/8+1:  nss/2,itone))))**2 + &
                       abs(sum(csymb(  nss/2+1:5*nss/8)*conjg(c1(  nss/2+1:5*nss/8,itone))))**2 + &
                       abs(sum(csymb(5*nss/8+1:3*nss/4)*conjg(c1(5*nss/8+1:3*nss/4,itone))))**2 + &
                       abs(sum(csymb(3*nss/4+1:7*nss/8)*conjg(c1(3*nss/4+1:7*nss/8,itone))))**2 + &
                       abs(sum(csymb(7*nss/8+1:    nss)*conjg(c1(7*nss/8+1:    nss,itone))))**2

      enddo
   enddo

! Sync quality check
   is1=0
   is2=0
   is3=0
   is4=0
   is5=0
   badsync=.false.
   ibmax=0

   is1=0; is2=0; is3=0; is4=0; is5=0
   do k=1,8
      ip=maxloc(s4(:,k,1))
      if(isyncword1(k-1).eq.(ip(1)-1)) is1=is1+1
      ip=maxloc(s4(:,k+38,1))
      if(isyncword2(k-1).eq.(ip(1)-1)) is2=is2+1
      ip=maxloc(s4(:,k+76,1))
      if(isyncword1(k-1).eq.(ip(1)-1)) is3=is3+1
      ip=maxloc(s4(:,k+114,1))
      if(isyncword2(k-1).eq.(ip(1)-1)) is4=is4+1
      ip=maxloc(s4(:,k+152,1))
      if(isyncword1(k-1).eq.(ip(1)-1)) is5=is5+1
   enddo
   nsync=is1+is2+is3+is4+is5   !Number of correct hard sync symbols, 0-40
   badsync=.false.

   if(nsync .lt. 16) then
      badsync=.true.
      return
   endif

   bitmetrics=0.0
   do nsub=1,nsizes  
      do ks=1,NN  
         s2=0
         do i=0,3
            s2(i)=s4(graymap(i),ks,nsub)
         enddo
         ipt=1+(ks-1)*2
         ibmax=1
         do ib=0,ibmax
            bm=maxval(s2(0:3),one(0:3,ibmax-ib)) - &
               maxval(s2(0:3),.not.one(0:3,ibmax-ib))
            if(ipt+ib.gt.2*NN) cycle
            bitmetrics(ipt+ib,nsub)=bm
         enddo
      enddo
   enddo

   call normalizebmet(bitmetrics(:,1),2*NN)
   call normalizebmet(bitmetrics(:,2),2*NN)
   call normalizebmet(bitmetrics(:,3),2*NN)
   call normalizebmet(bitmetrics(:,4),2*NN)

! Return the s4 array corresponding to N=1. Will be used for SNR calculation
   s4snr(:,:)=s4(:,:,1)
   return

end subroutine get_fst4_bitmetrics2
