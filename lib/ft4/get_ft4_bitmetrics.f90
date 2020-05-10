subroutine get_ft4_bitmetrics(cd,bitmetrics,badsync)

   include 'ft4_params.f90'
   parameter (NSS=NSPS/NDOWN,NDMAX=NMAX/NDOWN)
   complex cd(0:NN*NSS-1)
   complex cs(0:3,NN)
   complex csymb(NSS)
   integer icos4a(0:3),icos4b(0:3),icos4c(0:3),icos4d(0:3)
   integer graymap(0:3)
   integer ip(1)
   logical one(0:255,0:7)    ! 256 4-symbol sequences, 8 bits
   logical first
   logical badsync
   real bitmetrics(2*NN,3)
   real s2(0:255)
   real s4(0:3,NN)

   data icos4a/0,1,3,2/
   data icos4b/1,0,2,3/
   data icos4c/2,3,1,0/
   data icos4d/3,2,0,1/
   data graymap/0,1,3,2/
   data first/.true./
   save first,one

   if(first) then
      one=.false.
      do i=0,255
         do j=0,7
            if(iand(i,2**j).ne.0) one(i,j)=.true.
         enddo
      enddo
      first=.false.
   endif

   do k=1,NN
      i1=(k-1)*NSS
      csymb=cd(i1:i1+NSS-1)
      call four2a(csymb,NSS,1,-1,1)
      cs(0:3,k)=csymb(1:4)
      s4(0:3,k)=abs(csymb(1:4))
   enddo

! Sync quality check
   is1=0
   is2=0
   is3=0
   is4=0
   badsync=.false.
   ibmax=0
   
   do k=1,4
      ip=maxloc(s4(:,k))
      if(icos4a(k-1).eq.(ip(1)-1)) is1=is1+1
      ip=maxloc(s4(:,k+33))
      if(icos4b(k-1).eq.(ip(1)-1)) is2=is2+1
      ip=maxloc(s4(:,k+66))
      if(icos4c(k-1).eq.(ip(1)-1)) is3=is3+1
      ip=maxloc(s4(:,k+99))
      if(icos4d(k-1).eq.(ip(1)-1)) is4=is4+1
   enddo
   nsync=is1+is2+is3+is4   !Number of correct hard sync symbols, 0-16
   if(nsync .lt. 8) then
      badsync=.true.
      return
   endif

   do nseq=1,3             !Try coherent sequences of 1, 2, and 4 symbols
      if(nseq.eq.1) nsym=1
      if(nseq.eq.2) nsym=2
      if(nseq.eq.3) nsym=4
      nt=2**(2*nsym)
      do ks=1,NN-nsym+1,nsym  !87+16=103 symbols.
         amax=-1.0
         do i=0,nt-1
            i1=i/64
            i2=iand(i,63)/16
            i3=iand(i,15)/4
            i4=iand(i,3)
            if(nsym.eq.1) then
               s2(i)=abs(cs(graymap(i4),ks))
            elseif(nsym.eq.2) then
               s2(i)=abs(cs(graymap(i3),ks)+cs(graymap(i4),ks+1))
            elseif(nsym.eq.4) then
               s2(i)=abs(cs(graymap(i1),ks  ) + &
                  cs(graymap(i2),ks+1) + &
                  cs(graymap(i3),ks+2) + &
                  cs(graymap(i4),ks+3)   &
                  )
            else
               print*,"Error - nsym must be 1, 2, or 4."
            endif
         enddo
         ipt=1+(ks-1)*2
         if(nsym.eq.1) ibmax=1
         if(nsym.eq.2) ibmax=3
         if(nsym.eq.4) ibmax=7
         do ib=0,ibmax
            bm=maxval(s2(0:nt-1),one(0:nt-1,ibmax-ib)) - &
               maxval(s2(0:nt-1),.not.one(0:nt-1,ibmax-ib))
            if(ipt+ib.gt.2*NN) cycle
            bitmetrics(ipt+ib,nseq)=bm
         enddo
      enddo
   enddo

   bitmetrics(205:206,2)=bitmetrics(205:206,1)
   bitmetrics(201:204,3)=bitmetrics(201:204,2)
   bitmetrics(205:206,3)=bitmetrics(205:206,1)

   call normalizebmet(bitmetrics(:,1),2*NN)
   call normalizebmet(bitmetrics(:,2),2*NN)
   call normalizebmet(bitmetrics(:,3),2*NN)
   return

end subroutine get_ft4_bitmetrics
