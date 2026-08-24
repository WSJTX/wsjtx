subroutine sfox_remove_ft8(dd,npts)
   use packjt77
   include 'ft8_params.f90'
   parameter (MAXCAND=100)
   parameter (NP2=2812)
   integer itone(NN)
   integer iloc(1), ip(1)
   integer icos7(0:6)
   integer graymap(0:7)
   integer*1 cw(174),apmask(174)
   integer*1 message91(91)
   integer*1 message77(77)

   character*77 c77
   character*37 msg37

   real s8(0:7,NN)
   real s2(0:7)
   real ss(9)
   real candidate(3,MAXCAND)
   real sbase(NH1)
   real dd(npts)
   real bmeta(174),bmetd(174)
   real llra(174),llrd(174)
   complex cd0(0:3199)
   complex csymb(32)
   complex ctwk(32)
   complex cs(0:7,NN)
   data icos7/3,1,4,0,6,5,2/
   logical first,newdat
   logical one(0:7,0:2)
   logical unpk77_success
   data first/.true./
   data graymap/0,1,3,2,5,6,4,7/
   save one,first,twopi

   if(first) then
      one=.false.
      do i=0,7
         do j=0,2
            if(iand(i,2**j).ne.0) one(i,j)=.true.
         enddo
      enddo
      first=.false.
      twopi=8.0*atan(1.0)
   endif

   fs2=12000.0/NDOWN
   dt2=1.0/fs2
   nfa=500
   nfb=2500
   syncmin=1.5
   nfqso=750
   call sync8(dd,npts,nfa,nfb,syncmin,nfqso,MAXCAND,candidate,ncand,sbase)
   newdat=.true.
   do icand=1,ncand
      f1=candidate(1,icand)
      xdt=candidate(2,icand)
      xbase=10.0**(0.1*(sbase(nint(f1/3.125))-40.0))
      call ft8_downsample(dd,newdat,f1,cd0)
      newdat=.false.
      i0=nint((xdt+0.5)*fs2)
      smax=0.0
      do idt=i0-10,i0+10
         call sync8d(cd0,idt,ctwk,0,sync)
         if(sync.gt.smax) then
            smax=sync
            ibest=idt
         endif
      enddo

      smax=0.0
      delfbest=0.0
      do ifr=-5,5                              !Search over +/- 2.5 Hz
         delf=ifr*0.5
         dphi=twopi*delf*dt2
         phi=0.0
         do i=1,32
            ctwk(i)=cmplx(cos(phi),sin(phi))
            phi=mod(phi+dphi,twopi)
         enddo
         call sync8d(cd0,ibest,ctwk,1,sync)
         if( sync .gt. smax ) then
            smax=sync
            delfbest=delf
         endif
      enddo

      f1=f1+delfbest                           !Improved estimate of DF

      call ft8_downsample(dd,newdat,f1,cd0)   !Mix f1 to baseband and downsample

      smax=0.0
      do idt=-4,4                         !Search over +/- one quarter symbol
         call sync8d(cd0,ibest+idt,ctwk,0,sync)
         ss(idt+5)=sync
      enddo
      smax=maxval(ss)
      iloc=maxloc(ss)
      ibest=iloc(1)-5+ibest
      xdt=(ibest-1)*dt2
      sync=smax

      do k=1,NN
         i1=ibest+(k-1)*32
         csymb=cmplx(0.0,0.0)
         if( i1.ge.0 .and. i1+31 .le. NP2-1 ) csymb=cd0(i1:i1+31)
         call four2a(csymb,32,1,-1,1)
         cs(0:7,k)=csymb(1:8)/1e3
         s8(0:7,k)=abs(csymb(1:8))
      enddo

! sync quality check
      is1=0
      is2=0
      is3=0
      do k=1,7
         ip=maxloc(s8(:,k))
         if(icos7(k-1).eq.(ip(1)-1)) is1=is1+1
         ip=maxloc(s8(:,k+36))
         if(icos7(k-1).eq.(ip(1)-1)) is2=is2+1
         ip=maxloc(s8(:,k+72))
         if(icos7(k-1).eq.(ip(1)-1)) is3=is3+1
      enddo
! hard sync sum - max is 21
      nsync=is1+is2+is3
      if(nsync .le. 6) then ! bail out
         nbadcrc=1
         cycle
      endif

      nsym=1
      nt=2**(3*nsym)
      do ihalf=1,2
         do k=1,29,nsym
            if(ihalf.eq.1) then
               ks=k+7
            else
               ks=k+43
            endif
            amax=-1.0
            do i=0,nt-1
               i3=iand(i,7)
               s2(i)=abs(cs(graymap(i3),ks))
            enddo
            i32=1+(k-1)*3+(ihalf-1)*87
            ibmax=2
            do ib=0,ibmax
               bm=maxval(s2(0:nt-1),one(0:nt-1,ibmax-ib)) - &
                  maxval(s2(0:nt-1),.not.one(0:nt-1,ibmax-ib))
               if(i32+ib .gt.174) cycle
               bmeta(i32+ib)=bm
               den=max(maxval(s2(0:nt-1),one(0:nt-1,ibmax-ib)), &
                  maxval(s2(0:nt-1),.not.one(0:nt-1,ibmax-ib)))
               if(den.gt.0.0) then
                  cm=bm/den
               else ! erase it
                  cm=0.0
               endif
               bmetd(i32+ib)=cm
            enddo
         enddo
      enddo

      call normalizebmet(bmeta,174)
      call normalizebmet(bmetd,174)

      scalefac=2.83
      llra=scalefac*bmeta
      llrd=scalefac*bmetd

      cw=0
      dmin=0
      norder=2
      maxosd=-1
      Keff=91
      apmask=0
      message91=0
      cw=0
      call decode174_91(llra,Keff,maxosd,norder,apmask,message91,cw,   &
         ntype,nharderrors,dmin)

      if(nharderrors.ge.0) then
         message77=message91(1:77)
      else
         cycle
      endif

      if(count(cw.eq.0).eq.174) cycle

      write(c77,'(77i1)') message77
      read(c77(72:74),'(b3)') n3
      read(c77(75:77),'(b3)') i3
      if(i3.gt.5 .or. (i3.eq.0.and.n3.gt.6)) cycle
      if(i3.eq.0 .and. n3.eq.2) cycle

      call unpack77(c77,1,msg37,unpk77_success)
!      write(77,*) 'FT8 interference: ',msg37
      if(.not.unpk77_success) cycle
! Message structure: S7 D29 S7 D29 S7
      itone(1:7)=icos7
      itone(36+1:36+7)=icos7
      itone(NN-6:NN)=icos7
      k=7
      do j=1,ND
         i=3*j -2
         k=k+1
         if(j.eq.30) k=k+7
         indx=cw(i)*4 + cw(i+1)*2 + cw(i+2)
         itone(k)=graymap(indx)
      enddo
      call subtractft8(dd,itone,f1,xdt,.true.)
!      return
   enddo
   return
end subroutine sfox_remove_ft8
