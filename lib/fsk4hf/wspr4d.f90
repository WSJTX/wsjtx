program wspr4d

! Decode WSPR4 data read from *.c2 or *.wav files.

   use packjt77
   include 'wspr4_params.f90'
   parameter (NSPS2=NSPS/32)
   character arg*8,cbits*50,infile*80,fname*16,datetime*11
   character ch1*1,ch4*4,cseq*31
   character*22 decodes(100)
   character*37 msg
   character*120 data_dir
   character*77 c77
   complex c2(0:NMAX/32-1)              !Complex waveform
   complex cframe(0:103*NSPS2-1)               !Complex waveform
   complex cd(0:103*16-1)                   !Complex waveform
   real*8 fMHz
   real llr(174),llra(174),llrb(174),llrc(174)
   real candidates(100,2)
   real bitmetrics(206,3)
   integer ihdr(11)
   integer*2 iwave(NMAX)                 !Generated full-length waveform
   integer*1 apmask(174),cw(174)
   integer*1 hbits(206)
   integer*1 message74(74)
   integer*1 message101(101)
   logical badsync,unpk77_success

   fs=12000.0/NDOWN                       !Sample rate
   dt=1.0/fs                              !Sample interval (s)
   tt=NSPS*dt                             !Duration of "itone" symbols (s)
   txt=NZ*dt                              !Transmission length (s)

   nargs=iargc()
   if(nargs.lt.1) then
      print*,'Usage:   wspr4d [-a <data_dir>] [-f fMHz] file1 [file2 ...]'
      go to 999
   endif
   iarg=1
   data_dir="."
   call getarg(iarg,arg)
   if(arg(1:2).eq.'-a') then
      call getarg(iarg+1,data_dir)
      iarg=iarg+2
   endif
   call getarg(iarg,arg)
   if(arg(1:2).eq.'-f') then
      call getarg(iarg+1,arg)
      read(arg,*) fMHz
      iarg=iarg+2
   endif

   open(13,file=trim(data_dir)//'/ALL_WSPR.TXT',status='unknown',   &
      position='append')

   ngood=0
   do ifile=iarg,nargs
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
         call wspr4_downsample(iwave,c2)
      else
         print*,'Wrong file format?'
         go to 999
      endif
      close(10)

      fa=-100.0
      fb=100.0
      fs=12000.0/32.0
      npts=120*12000.0/32.0

      call getcandidate4(c2,npts,fs,fa,fb,ncand,candidates)         !First approx for freq

      ndecodes=0
      do icand=1,ncand
         fc0=candidates(icand,1)
         xsnr=candidates(icand,2)

         do isync=0,1

            del=1.5*fs/416.0
            if(isync.eq.0) then
               fc1=fc0-del
               is0=375
               ishw=300
               dis=50
               ifhw=10
               df=.1
            else if(isync.eq.1) then
               fc1=fc2
               is0=isbest
               ishw=100
               dis=10
               ifhw=10
               df=.02
            endif
            smax=0.0
            do if=-ifhw,ifhw
               fc=fc1+df*if
               do istart=max(1,is0-ishw),is0+ishw,dis
                  call coherent_sync(c2,istart,fc,1,sync)
                  if(sync.gt.smax) then
                     fc2=fc
                     isbest=istart
                     smax=sync
                  endif
               enddo
            enddo
!            write(*,*) ifile,icand,isync,fc1+del,fc2+del,isbest,smax
         enddo

!         if(smax .lt. 100.0 ) cycle
!isbest=375
!fc2=-del
         idecoded=0
         do ijitter=0,2
            if(idecoded.eq.1) exit
            if(ijitter.eq.0) ioffset=0
            if(ijitter.eq.1) ioffset=50
            if(ijitter.eq.2) ioffset=-50
            is0=isbest+ioffset
            if(is0.lt.0) cycle
            cframe=c2(is0:is0+103*416-1)
            call downsample4(cframe,fc2,cd)
            s2=sum(cd*conjg(cd))/(16*103)
            cd=cd/sqrt(s2)
            call get_wspr4_bitmetrics(cd,bitmetrics,badsync)

            hbits=0
            where(bitmetrics(:,1).ge.0) hbits=1
            ns1=count(hbits(  1:  8).eq.(/0,0,0,1,1,0,1,1/))
            ns2=count(hbits( 67: 74).eq.(/0,1,0,0,1,1,1,0/))
            ns3=count(hbits(133:140).eq.(/1,1,1,0,0,1,0,0/))
            ns4=count(hbits(199:206).eq.(/1,0,1,1,0,0,0,1/))
            nsync_qual=ns1+ns2+ns3+ns4
!          if(nsync_qual.lt. 20) cycle

            scalefac=2.83
            llra(  1: 58)=bitmetrics(  9: 66, 1)
            llra( 59:116)=bitmetrics( 75:132, 1)
            llra(117:174)=bitmetrics(141:198, 1)
            llra=scalefac*llra
            llrb(  1: 58)=bitmetrics(  9: 66, 2)
            llrb( 59:116)=bitmetrics( 75:132, 2)
            llrb(117:174)=bitmetrics(141:198, 2)
            llrb=scalefac*llrb
            llrc(  1: 58)=bitmetrics(  9: 66, 3)
            llrc( 59:116)=bitmetrics( 75:132, 3)
            llrc(117:174)=bitmetrics(141:198, 3)
            llrc=scalefac*llrc
            apmask=0
            max_iterations=40

            do itry=3,1,-1
               if(itry.eq.1) llr=llra
               if(itry.eq.2) llr=llrb
               if(itry.eq.3) llr=llrc
               nhardbp=0
               nhardosd=0
               dmin=0.0
!               call bpdecode174_74(llr,apmask,max_iterations,message74,cw,nhardbp,niterations,nchecks)
               call bpdecode174_101(llr,apmask,max_iterations,message101,cw,nhardbp,niterations,ncheck101)
               Keff=91
!               if(nhardbp.lt.0) call osd174_74(llr,Keff,apmask,5,message74,cw,nhardosd,dmin)
!               if(nhardbp.lt.0) call osd174_101(llr,Keff,apmask,5,message74,cw,nhardosd,dmin)
               maxsuperits=2
!               ndeep=4 
               ndeep=3 
               if(nhardbp.lt.0) then
!                  call decode174_74(llr,Keff,ndeep,apmask,maxsuperits,message74,cw,nhardosd,iter,ncheck,dmin,isuper)
                  call decode174_101(llr,Keff,ndeep,apmask,maxsuperits,message101,cw,nhardosd,iter,ncheck,dmin,isuper)
               endif
               if(nhardbp.ge.0 .or. nhardosd.ge.0) then
!                  write(c77,'(50i1)') message74(1:50)
!                  c77(51:77)='000000000000000000000110000'
                  write(c77,'(77i1)') message101(1:77)
                  call unpack77(c77,0,msg,unpk77_success)
                  if(unpk77_success .and. index(msg,'K9AN').gt.0) then
                     idecoded=1
                     ngood=ngood+1
                     write(*,1100) ifile,xsnr,isbest/375.0-1.0,1500.0+fc2+del,msg(1:14),itry,nhardbp,nhardosd,dmin,ijitter
1100                 format(i5,2x,f6.1,2x,f6.2,2x,f8.2,2x,a14,i4,i4,i4,f7.2,i6)
                     exit
                  else
                     cycle
                  endif
               endif
            enddo  ! metrics
         enddo  ! istart jitter
      enddo !candidate list
   enddo !files
   nfiles=nargs-iarg+1
   write(*,*) 'nfiles: ',nfiles,' ngood: ',ngood
   write(*,1120)
1120 format("<DecodeFinished>")

999 end program wspr4d

subroutine coherent_sync(cd0,i0,f0,itwk,sync)

! Compute sync power for a complex, downsampled FT4 signal.

   include 'wspr4_params.f90'
   parameter(NP=NMAX/NDOWN,NSS=NSPS/NDOWN)
   complex cd0(0:NP-1)
   complex csynca(4*NSS),csyncb(4*NSS),csyncc(4*NSS),csyncd(4*NSS)
   complex csync2(4*NSS)
   complex ctwk(4*NSS)
   complex z1,z2,z3,z4
   logical first
   integer icos4a(0:3),icos4b(0:3),icos4c(0:3),icos4d(0:3)
   data icos4a/0,1,3,2/
   data icos4b/1,0,2,3/
   data icos4c/2,3,1,0/
   data icos4d/3,2,0,1/
   data first/.true./
   save first,twopi,csynca,csyncb,csyncc,csyncd,fac

   p(z1)=(real(z1*fac)**2 + aimag(z1*fac)**2)**0.5          !Statement function for power

   if( first ) then
      twopi=8.0*atan(1.0)
      k=1
      phia=0.0
      phib=0.0
      phic=0.0
      phid=0.0
      do i=0,3
         dphia=twopi*icos4a(i)/real(NSS)
         dphib=twopi*icos4b(i)/real(NSS)
         dphic=twopi*icos4c(i)/real(NSS)
         dphid=twopi*icos4d(i)/real(NSS)
         do j=1,NSS
            csynca(k)=cmplx(cos(phia),sin(phia))
            csyncb(k)=cmplx(cos(phib),sin(phib))
            csyncc(k)=cmplx(cos(phic),sin(phic))
            csyncd(k)=cmplx(cos(phid),sin(phid))
            phia=mod(phia+dphia,twopi)
            phib=mod(phib+dphib,twopi)
            phic=mod(phic+dphic,twopi)
            phid=mod(phid+dphid,twopi)
            k=k+1
         enddo
      enddo
      first=.false.
      fac=1.0/(4.0*NSS)
   endif

   i1=i0                            !four Costas arrays
   i2=i0+33*NSS
   i3=i0+66*NSS
   i4=i0+99*NSS

   z1=0.
   z2=0.
   z3=0.
   z4=0.

   if(itwk.eq.1) then
      dt=1/(12000.0/32.0)
      dphi=twopi*f0*dt
      phi=0.0
      do i=1,4*NSS
         ctwk(i)=cmplx(cos(phi),sin(phi))
         phi=mod(phi+dphi,twopi)
      enddo
   endif

   if(itwk.eq.1) csync2=ctwk*csynca      !Tweak the frequency
   z1=0.
   if(i1.ge.0 .and. i1+4*NSS-1.le.NP-1) then
      z1=sum(cd0(i1:i1+4*NSS-1)*conjg(csync2))
   elseif( i1.lt.0 ) then
      npts=(i1+4*NSS-1)/2
      if(npts.le.32) then
         z1=0.
      else
         z1=sum(cd0(0:i1+4*NSS-1)*conjg(csync2(4*NSS-npts:)))
      endif
   endif

   if(itwk.eq.1) csync2=ctwk*csyncb      !Tweak the frequency
   if(i2.ge.0 .and. i2+4*NSS-1.le.NP-1) z2=sum(cd0(i2:i2+4*NSS-1)*conjg(csync2))

   if(itwk.eq.1) csync2=ctwk*csyncc      !Tweak the frequency
   if(i3.ge.0 .and. i3+4*NSS-1.le.NP-1) z3=sum(cd0(i3:i3+4*NSS-1)*conjg(csync2))

   if(itwk.eq.1) csync2=ctwk*csyncd      !Tweak the frequency
   z4=0.
   if(i4.ge.0 .and. i4+4*NSS-1.le.NP-1) then
      z4=sum(cd0(i4:i4+4*NSS-1)*conjg(csync2))
   elseif( i4+4*NSS-1.gt.NP-1 ) then
      npts=(NP-1-i4+1)
      if(npts.le.32) then
         z4=0.
      else
         z4=sum(cd0(i4:i4+npts-1)*conjg(csync2(1:npts)))
      endif
   endif

   sync = p(z1) + p(z2) + p(z3) + p(z4)

   return
end subroutine coherent_sync

subroutine downsample4(ci,f0,co)
   parameter(NI=103*416,NH=NI/2,NO=NI/26)  ! downsample from 416 samples per symbol to 16
   complex ci(0:NI-1),ct(0:NI-1)
   complex co(0:NO-1)
   fs=12000.0/32.0
   df=fs/NI
   ct=ci
   call four2a(ct,NI,1,-1,1)             !c2c FFT to freq domain
   i0=nint(f0/df)
   ct=cshift(ct,i0)
   co=0.0
   co(0)=ct(0)
   b=16.0
   do i=1,NO/2
      arg=(i*df/b)**2
      filt=exp(-arg)
      co(i)=ct(i)*filt
      co(NO-i)=ct(NI-i)*filt
   enddo
   co=co/NO
   call four2a(co,NO,1,1,1)            !c2c FFT back to time domain
   return
end subroutine downsample4

subroutine getcandidate4(c,npts,fs,fa,fb,ncand,candidates)
   parameter(NFFT1=120*12000/32,NH1=NFFT1/2,NFFT2=120*12000/320,NH2=NFFT2/2)
   complex c(0:npts-1)                   !Complex waveform
   complex cc(0:NFFT1-1)
   complex csfil(0:NFFT2-1)
   complex cwork(0:NFFT2-1)
   real bigspec(0:NFFT2-1)
   complex c2(0:NFFT1-1)                 !Short spectra
   real s(-NH1+1:NH1)                    !Coarse spectrum
   real ss(-NH1+1:NH1)                   !Smoothed coarse spectrum
   real candidates(100,2)
   integer indx(NFFT2-1)
   logical first
   data first/.true./
   save first,w,df,csfil

   if(first) then
      df=10*fs/NFFT1
      csfil=cmplx(0.0,0.0)
      do i=0,NFFT2-1
         csfil(i)=exp(-((i-NH2)/32.0)**2)  ! revisit this
      enddo
      csfil=cshift(csfil,NH2)
      call four2a(csfil,NFFT2,1,-1,1)
      first=.false.
   endif

   cc=cmplx(0.0,0.0)
   cc(0:npts-1)=c;
   call four2a(cc,NFFT1,1,-1,1)
   cc=abs(cc)**2
   call four2a(cc,NFFT1,1,-1,1)
   cwork(0:NH2)=cc(0:NH2)*conjg(csfil(0:NH2))
   cwork(NH2+1:NFFT2-1)=cc(NFFT1-NH2+1:NFFT1-1)*conjg(csfil(NH2+1:NFFT2-1))

   call four2a(cwork,NFFT2,1,+1,1)
   bigspec=cshift(real(cwork),-NH2)
   il=NH2+fa/df
   ih=NH2+fb/df
   nnl=ih-il+1
   call indexx(bigspec(il:il+nnl-1),nnl,indx)
   xn=bigspec(il-1+indx(nint(0.3*nnl)))
   bigspec=bigspec/xn
   ncand=0
   do i=il,ih
      if((bigspec(i).gt.bigspec(i-1)).and. &
         (bigspec(i).gt.bigspec(i+1)).and. &
         (bigspec(i).gt.1.15).and.ncand.lt.100) then
         ncand=ncand+1
         candidates(ncand,1)=df*(i-NH2)
         candidates(ncand,2)=10*log10(bigspec(i)-1)-28.5
      endif
   enddo
   return
end subroutine getcandidate4

subroutine wspr4_downsample(iwave,c)

! Input: i*2 data in iwave() at sample rate 12000 Hz
! Output: Complex data in c(), sampled at 375 Hz

   include 'wspr4_params.f90'
   parameter (NFFT2=NMAX/32)
   integer*2 iwave(NMAX)
   complex c(0:NMAX/32-1)
   complex c1(0:NFFT2-1)
   complex cx(0:NMAX/2)
   real x(NMAX)
   equivalence (x,cx)

   df=12000.0/NMAX
   x=iwave
   call four2a(x,NMAX,1,-1,0)             !r2c FFT to freq domain
   i0=nint(1500.0/df)
   c1(0)=cx(i0)
   do i=1,NFFT2/2
      c1(i)=cx(i0+i)
      c1(NFFT2-i)=cx(i0-i)
   enddo
   c1=c1/NFFT2
   call four2a(c1,NFFT2,1,1,1)            !c2c FFT back to time domain
   c=c1(0:NMAX/32-1)
   return
end subroutine wspr4_downsample

