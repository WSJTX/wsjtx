program fst280d

! Decode fst280 data read from *.c2 or *.wav files.

   use packjt77
   include 'fst280_params.f90'
   character arg*8,infile*80,fname*16,datetime*11
!   character ch1*1,ch4*4,cseq*31
!   character*22 decodes(100)
   character*37 msg
   character*120 data_dir
   character*77 c77
   character*1 tr_designator
   complex, allocatable :: c2(:)
   complex, allocatable :: cframe(:)
   complex, allocatable :: c_bigfft(:)          !Complex waveform
   real, allocatable :: r_data(:)
   real*8 fMHz
   real llr(280),llra(280),llrb(280),llrc(280),llrd(280)
   real candidates(100,3)
   real bitmetrics(328,4)
   integer hmod,ihdr(11)
   integer*2, allocatable :: iwave(:)
   integer*1 apmask(280),cw(280)
   integer*1 hbits(328)
   integer*1 message101(101),message74(74)
   logical badsync,unpk77_success

   hmod=1
   Keff=91
   ndeep=3
   iwspr=0

   nargs=iargc()
   if(nargs.lt.1) then
      print*,'Usage:   fst280d [-a <data_dir>] [-f fMHz] [-h hmod] [-k Keff] [-d depth] [-t t/r type] file1 [file2 ...]'
      go to 999
   endif
   iarg=1
   data_dir="."
   call getarg(iarg,arg)
   if(arg(1:2).eq.'-a') then
      call getarg(iarg+1,data_dir)
      iarg=iarg+2
      call getarg(iarg,arg)
   endif
   if(arg(1:2).eq.'-f') then
      call getarg(iarg+1,arg)
      read(arg,*) fMHz
      iarg=iarg+2
      call getarg(iarg,arg)
   endif
   if(arg(1:2).eq.'-h') then
      call getarg(iarg+1,arg)
      read(arg,*) hmod
      if(hmod.ne.1.and.hmod.ne.2.and.hmod.ne.4.and.hmod.ne.8) then
         print*,'invalid modulation index. h must be 1, 2, 4, or 8'
         goto 999
      endif
      iarg=iarg+2
      call getarg(iarg,arg)
   endif
   if(arg(1:2).eq.'-k') then
      call getarg(iarg+1,arg)
      read(arg,*) Keff
      if(Keff.le.74) iwspr=1
      iarg=iarg+2
      call getarg(iarg,arg)
   endif
   if(arg(1:2).eq.'-d') then
      call getarg(iarg+1,arg)
      read(arg,*) ndeep
      iarg=iarg+2
      call getarg(iarg,arg)
   endif
   if(arg(1:2).eq.'-t') then
      call getarg(iarg+1,arg)
      read(arg,*) tr_designator 
      iarg=iarg+2
   endif

   nmax=15*12000
   select case (tr_designator)
      case('A')
         nsps=800
         nmax=15*12000
         ndown=20/hmod
         if(hmod.eq.8) ndown=2
      case('B')
         nsps=1680
         nmax=30*12000
         ndown=42/hmod   ! 40 Sa/symbol
         if(hmod.eq.4) ndown=10 
         if(hmod.eq.8) ndown=5
      case('C')
         nsps=4000
         nmax=60*12000
         ndown=100/hmod    ! 40 Sa/symbol
         if(hmod.eq.8) ndown=16
      case('D')
         nsps=8400
         nmax=120*12000
         ndown=200/hmod
      case('E')
         nsps=21504
         nmax=300*12000
         ndown=512/hmod
   end select
   nss=nsps/ndown
   fs=12000.0                       !Sample rate
   fs2=fs/ndown
   nspsec=nint(fs2)
   dt=1.0/fs                        !Sample interval (s)
   dt2=1.0/fs2
   tt=nsps*dt                       !Duration of "itone" symbols (s)

   nfft1=2*int(nmax/2)
   nh1=nfft1/2
   allocate( r_data(1:nfft1+2) )
   allocate( c_bigfft(0:nfft1/2) )

   nfft2=nfft1/ndown
   allocate( c2(0:nfft2-1) ) 
   allocate( cframe(0:164*nss-1) )
   allocate( iwave(nmax) ) 

write(*,*) 'nsps: ',nsps
write(*,*) 'nmax: ',nmax
write(*,*) 'nss : ',nss
write(*,*) 'nspsec: ',fs2
write(*,*) 'nfft1 : ',nfft1
write(*,*) 'nfft2 : ',nfft2

   ngood=0
   ngoodsync=0
   nfile=0
   do ifile=iarg,nargs
      nfile=nfile+1
      call getarg(ifile,infile)
      open(10,file=infile,status='old',access='stream')
      j1=index(infile,'.c2')
      j2=index(infile,'.wav')
      if(j1.gt.0) then
         read(10,end=999) fname,ntrmin,fMHz,c2
         read(fname(8:11),*) nutc
         write(datetime,'(i11)') nutc
      else if(j2.gt.0) then
         read(10,end=999) ihdr,iwave
         read(infile(j2-4:j2-1),*) nutc
         datetime=infile(j2-11:j2-1)
      else
         print*,'Wrong file format?'
         go to 999
      endif
      close(10)

      npts=nmax
      fa=100.0
      fb=3500.0

! The big fft is done once and is used for calculating the smoothed spectrum 
! and also for downconverting/downsampling each candidate.
      r_data(1:nfft1)=iwave(1:nfft1)
      r_data(nfft1+1:nfft1+2)=0.0
      call four2a(r_data,nfft1,1,-1,0)
      c_bigfft=cmplx(r_data(1:nfft1+2:2),r_data(2:nfft1+2:2))

! Get first approximation of candidate frequencies
      call get_candidates_fst280(c_bigfft,nfft1,nsps,hmod,fs,fa,fb,ncand,candidates)
      
      ndecodes=0
      isbest1=0
      isbest8=0
      fc21=fc0
      fc28=fc0
      do icand=1,ncand
         fc0=candidates(icand,1)
         xsnr=candidates(icand,2)

! Downconvert and downsample a slice of the spectrum centered on the
! rough estimate of the candidates frequency.
! Output array c2 is complex baseband sampled at 12000/ndown Sa/sec.
! The size of the downsampled c2 array is nfft2=nfft1/ndown

         call fst280_downsample(c_bigfft,nfft1,ndown,fc0,c2)
!         write(*,3001) c2(nfft2/3),candidates(icand,1:2)
!3001     format(2e15.6,2f10.3)

         do isync=0,1
            if(isync.eq.0) then
               fc1=0.0
               is0=nint(fs2)
               ishw=is0
               isst=4
               ifhw=10
               df=.1*8400/nsps
            else if(isync.eq.1) then
               fc1=fc28
               is0=isbest8
               ishw=4
               isst=1
               ifhw=10
               df=.02*8400/nsps
            endif

            smax1=0.0
            smax8=0.0
            do if=-ifhw,ifhw
               fc=fc1+df*if
               do istart=max(1,is0-ishw),is0+ishw,isst
                  call sync_fst280(c2,istart,fc,hmod,1,nfft2,nss,fs2,sync1)
                  call sync_fst280(c2,istart,fc,hmod,8,nfft2,nss,fs2,sync8)
                  if(sync8.gt.smax8) then
                     fc28=fc
                     isbest8=istart
                     smax8=sync8
                  endif
                  if(sync1.gt.smax1) then
                     fc21=fc
                     isbest1=istart
                     smax1=sync1
                  endif
               enddo
            enddo
!            write(*,1022) ifile,icand,isync,fc1,   &
!               fc21,isbest1,smax1,fc28,isbest8,smax8
!1022        format(i5,1x,i4,1x,i4,1x,f7.2,1x,2(1x,f7.2,1x,i5,1x,e9.3))
         enddo

         if(smax8/smax1 .lt. 0.65 ) then
            fc2=fc21
            isbest=isbest1
            ntmax=4
            if(hmod .gt. 1) ntmax=1
            ntmin=1
            njitter=2
         else
            fc2=fc28
            isbest=isbest8
            ntmax=4
            if(hmod .gt. 1) ntmax=1
            ntmin=1
            njitter=2
         endif
         fc_synced = fc0 + fc2
         dt_synced = (isbest-fs2)*dt2  !nominal dt is 1 second so frame starts at sample fs2
         call fst280_downsample(c_bigfft,nfft1,ndown,fc_synced,c2)

         if(abs((isbest-fs2)/nss) .lt. 0.2 .and. abs(fc_synced-1500.0).lt.0.4) then
            ngoodsync=ngoodsync+1
         endif

         do ijitter=0,2
            if(ijitter.eq.0) ioffset=0
            if(ijitter.eq.1) ioffset=1
            if(ijitter.eq.2) ioffset=-1
            is0=isbest+ioffset
            if(is0.lt.0) cycle
            cframe=c2(is0:is0+164*nss-1)
            s2=sum(cframe*conjg(cframe))
            cframe=cframe/sqrt(s2)
            call get_fst280_bitmetrics(cframe,nss,hmod,bitmetrics,badsync)

            hbits=0
            where(bitmetrics(:,1).ge.0) hbits=1
            ns1=count(hbits(  1:  8).eq.(/0,0,0,1,1,0,1,1/))
            ns2=count(hbits(  9: 16).eq.(/0,1,0,0,1,1,1,0/))
            ns3=count(hbits(157:164).eq.(/0,0,0,1,1,0,1,1/))
            ns4=count(hbits(165:172).eq.(/0,1,0,0,1,1,1,0/))
            ns5=count(hbits(313:320).eq.(/0,0,0,1,1,0,1,1/))
            ns6=count(hbits(321:328).eq.(/0,1,0,0,1,1,1,0/))
            nsync_qual=ns1+ns2+ns3+ns4+ns5+ns6
!          if(nsync_qual.lt. 20) cycle

            scalefac=2.83
            llra(  1:140)=bitmetrics( 17:156, 1)
            llra(141:280)=bitmetrics(173:312, 1)
            llra=scalefac*llra
            llrb(  1:140)=bitmetrics( 17:156, 2)
            llrb(141:280)=bitmetrics(173:312, 2)
            llrb=scalefac*llrb
            llrc(  1:140)=bitmetrics( 17:156, 3)
            llrc(141:280)=bitmetrics(173:312, 3)
            llrc=scalefac*llrc
            llrd(  1:140)=bitmetrics( 17:156, 4)
            llrd(141:280)=bitmetrics(173:312, 4)
            llrd=scalefac*llrd
            apmask=0

            do itry=ntmax,ntmin,-1
               if(itry.eq.1) llr=llra
               if(itry.eq.2) llr=llrb
               if(itry.eq.3) llr=llrc
               if(itry.eq.4) llr=llrd
               dmin=0.0
               nharderrors=-1
               unpk77_success=.false.
               if(iwspr.eq.0) then
                  maxosd=2
                  call decode280_101(llr,Keff,maxosd,ndeep,apmask,message101,cw,ntype,nharderrors,dmin)
               else
                  maxosd=2
                  call decode280_74(llr,Keff,maxosd,ndeep,apmask,message74,cw,ntype,nharderrors,dmin)
               endif
               if(nharderrors .ge.0) then
                  if(iwspr.eq.0) then
                     write(c77,'(77i1)') message101(1:77)
                     call unpack77(c77,0,msg,unpk77_success)
                  else
                     write(c77,'(50i1)') message74(1:50)
                     c77(51:77)='000000000000000000000110000'
                     call unpack77(c77,0,msg,unpk77_success)
                  endif
                  if(nharderrors .ge.0 .and. unpk77_success) then
                     ngood=ngood+1
                     write(*,1100) nfile,icand,xsnr,dt_synced,fc_synced,          &
                        itry,ntype,nharderrors,dmin,ijitter,nsync_qual,msg(1:22)
1100                 format(i5,i5,f6.1,f6.2,f7.1,i4,i4,i4,f7.2,i6,i6,2x,a22)
                     goto 2002
                  else
                     cycle
                  endif
               endif
            enddo  ! metrics
         enddo  ! istart jitter
2002  continue
      enddo !candidate list
   enddo !files
   nfiles=nargs-iarg+1
   write(*,*) 'nfiles: ',nfiles,' ngood: ',ngood,' ngoodsync: ',ngoodsync
   write(*,1120)
1120 format("<DecodeFinished>")

999 end program fst280d

subroutine sync_fst280(cd0,i0,f0,hmod,ncoh,np,nss,fs,sync)

! Compute sync power for a complex, downsampled FST280 signal.

   include 'fst280_params.f90'
   complex cd0(0:np-1)
   complex, allocatable, save ::  csync(:)
   complex, allocatable, save ::  csynct(:)
   complex ctwk(8*nss)
   complex z1,z2,z3
   logical first
   integer isyncword(0:7)
   integer hmod
   real f0save
   data isyncword/0,1,3,2,1,0,2,3/
   data first/.true./
   data f0save/0.0/
   save first,twopi,dt,fac,f0save

   p(z1)=(real(z1*fac)**2 + aimag(z1*fac)**2)**0.5          !Statement function for power

   if( first ) then
      allocate( csync(8*nss) )
      allocate( csynct(8*nss) )
      twopi=8.0*atan(1.0)
      dt=1/fs
      k=1
      phi=0.0
      do i=0,7
         dphi=twopi*hmod*(isyncword(i)-1.5)/real(nss)
         do j=1,nss
            csync(k)=cmplx(cos(phi),sin(phi))
            phi=mod(phi+dphi,twopi)
            k=k+1
         enddo
      enddo
      first=.false.
      fac=1.0/(8.0*nss)
   endif

   if(f0.ne.f0save) then
      dphi=twopi*f0*dt
      phi=0.0
      do i=1,8*nss
         ctwk(i)=cmplx(cos(phi),sin(phi))
         phi=mod(phi+dphi,twopi)
      enddo
      csynct=ctwk*csync
      f0save=f0
   endif

   i1=i0                            !Costas arrays
   i2=i0+78*nss
   i3=i0+156*nss

   s1=0.0
   s2=0.0
   s3=0.0
   nsec=8/ncoh
   do i=1,nsec
      is=(i-1)*ncoh*nss
      z1=sum(cd0(i1+is:i1+is+ncoh*nss-1)*conjg(csynct(is+1:is+ncoh*nss)))
      z2=sum(cd0(i2+is:i2+is+ncoh*nss-1)*conjg(csynct(is+1:is+ncoh*nss)))
      z3=sum(cd0(i3+is:i3+is+ncoh*nss-1)*conjg(csynct(is+1:is+ncoh*nss)))
      s1=s1+abs(z1)/(8*nss)
      s2=s2+abs(z2)/(8*nss)
      s3=s3+abs(z3)/(8*nss)
   enddo

   sync = s1+s2+s3

   return
end subroutine sync_fst280

subroutine fst280_downsample(c_bigfft,nfft1,ndown,f0,c1)

! Output: Complex data in c(), sampled at 12000/ndown Hz

   complex c_bigfft(0:nfft1/2)
   complex c1(0:nfft1/ndown-1)

   df=12000.0/nfft1
   i0=nint(f0/df)
   c1(0)=c_bigfft(i0)
   nfft2=nfft1/ndown
   do i=1,nfft2/2
      if(i0+i.le.nfft1/2) c1(i)=c_bigfft(i0+i)
      if(i0-i.ge.0) c1(nfft2-i)=c_bigfft(i0-i)
   enddo
   c1=c1/nfft2
   call four2a(c1,nfft2,1,1,1)            !c2c FFT back to time domain
   return

end subroutine fst280_downsample

subroutine get_candidates_fst280(c_bigfft,nfft1,nsps,hmod,fs,fa,fb,ncand,candidates)

  complex c_bigfft(0:nfft1/2) 
  integer hmod
  real candidates(100,3)
  real s(18000)
  real s2(18000)
  data nfft1z/-1/
  save nfft1z

  nh1=nfft1/2
  df1=fs/nfft1
  baud=fs/nsps
  df2=baud/2.0
  nd=df1/df2
  ndh=nd/2
  ia=fa/df2
  ib=fb/df2
  s=0.
  do i=ia,ib
     j0=nint(i*df2/df1)
     do j=j0-ndh,j0+ndh
        s(i)=s(i) + real(c_bigfft(j))**2 + aimag(c_bigfft(j))**2
     enddo
  enddo
  call pctile(s(ia:ib),ib-ia+1,30,base)
  s=s/base
  nh=hmod
  do i=ia,ib
     s2(i)=s(i-nh*3) + s(i-nh) +s(i+nh) +s(i+nh*3)
     s2(i)=db(s2(i)) - 48.5
  enddo

  if(nh.eq.1) thresh=-29.5
  if(nh.eq.2) thresh=-27.0
  if(nh.eq.4) thresh=-25.0
  
  ncand=0
  do i=ia,ib
     if((s2(i).gt.s2(i-1)).and. &
          (s2(i).gt.s2(i+1)).and. &
          (s2(i).gt.thresh).and.ncand.lt.100) then
        ncand=ncand+1
        candidates(ncand,1)=df2*i
        candidates(ncand,2)=s2(i)
     endif
  enddo

  return
end subroutine get_candidates_fst280
