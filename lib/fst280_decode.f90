module fst280_decode
  type :: fst280_decoder
     procedure(fst280_decode_callback), pointer :: callback
   contains
     procedure :: decode
  end type fst280_decoder

  abstract interface
     subroutine fst280_decode_callback (this,nutc,sync,nsnr,dt,freq,    &
          decoded,nap,qual,ntrperiod)
       import fst280_decoder
       implicit none
       class(fst280_decoder), intent(inout) :: this
       integer, intent(in) :: nutc
       real, intent(in) :: sync
       integer, intent(in) :: nsnr
       real, intent(in) :: dt
       real, intent(in) :: freq
       character(len=37), intent(in) :: decoded
       integer, intent(in) :: nap
       real, intent(in) :: qual
       integer, intent(in) :: ntrperiod
     end subroutine fst280_decode_callback
  end interface

contains

 subroutine decode(this,callback,iwave,nutc,nQSOProgress,nfqso,    &
      nfa,nfb,nsubmode,ndeep,ntrperiod,nexp_decode,ntol)

   use timer_module, only: timer
   use packjt77
   include 'fst280/fst280_params.f90'
   parameter (MAXCAND=100)
   class(fst280_decoder), intent(inout) :: this
   procedure(fst280_decode_callback) :: callback
   character*37 decodes(100)
   character*37 msg
   character*77 c77
   complex, allocatable :: c2(:)
   complex, allocatable :: cframe(:)
   complex, allocatable :: c_bigfft(:)          !Complex waveform
   real, allocatable :: r_data(:)
   real llr(280),llra(280),llrb(280),llrc(280),llrd(280)
   real candidates(100,4)
   real bitmetrics(328,4)
   real s4(0:3,NN)
   integer itone(NN)
   integer hmod
   integer*1 apmask(280),cw(280)
   integer*1 hbits(328)
   integer*1 message101(101),message74(74)
   logical badsync,unpk77_success,single_decode
   integer*2 iwave(300*12000)

   this%callback => callback
   hmod=2**nsubmode
   if(nfqso+nqsoprogress.eq.-999) return
   Keff=91
   iwspr=0
   nmax=15*12000
   single_decode=iand(nexp_decode,32).eq.32
   if(ntrperiod.eq.15) then
      nsps=800
      nmax=15*12000
      ndown=20/hmod
      if(hmod.eq.8) ndown=2
   else if(ntrperiod.eq.30) then
      nsps=1680
      nmax=30*12000
      ndown=42/hmod
      if(hmod.eq.4) ndown=10
      if(hmod.eq.8) ndown=5
   else if(ntrperiod.eq.60) then
      nsps=3888
      nmax=60*12000
      ndown=96/hmod
      if(hmod.eq.1) ndown=108
   else if(ntrperiod.eq.120) then
      nsps=8200
      nmax=120*12000
      if(hmod.eq.1) ndown=205
      ndown=100/hmod
   else if(ntrperiod.eq.300) then
      nsps=21168
      nmax=300*12000
      ndown=504/hmod
   end if
   nss=nsps/ndown
   fs=12000.0                       !Sample rate
   fs2=fs/ndown
   nspsec=nint(fs2)
   dt=1.0/fs                        !Sample interval (s)
   dt2=1.0/fs2
   tt=nsps*dt                       !Duration of "itone" symbols (s)
   baud=1.0/tt
   nfft1=2*int(nmax/2)
   nh1=nfft1/2
   allocate( r_data(1:nfft1+2) )
   allocate( c_bigfft(0:nfft1/2) )

   nfft2=nfft1/ndown
   allocate( c2(0:nfft2-1) ) 
   allocate( cframe(0:164*nss-1) )

   npts=nmax
   if(single_decode) then
      fa=max(100,nint(nfqso+1.5*hmod*baud-ntol))
      fb=min(4800,nint(nfqso+1.5*hmod*baud+ntol))
   else
      fa=max(100,nfa)
      fb=min(4800,nfb)
   endif

   if(ndeep.eq.3) then
      ntmax=4      ! number of block sizes to try
      jittermax=2
      norder=3
   elseif(ndeep.eq.2) then
      ntmax=3
      jittermax=2
      norder=3
   elseif(ndeep.eq.1) then
      ntmax=1
      jittermax=2
      norder=2
   endif

   ! The big fft is done once and is used for calculating the smoothed spectrum 
! and also for downconverting/downsampling each candidate.
   r_data(1:nfft1)=iwave(1:nfft1)
   r_data(nfft1+1:nfft1+2)=0.0
   call four2a(r_data,nfft1,1,-1,0)
   c_bigfft=cmplx(r_data(1:nfft1+2:2),r_data(2:nfft1+2:2))

! Get first approximation of candidate frequencies
   call get_candidates_fst280(c_bigfft,nfft1,nsps,hmod,fs,fa,fb,     &
        ncand,candidates,base)

   ndecodes=0
   decodes=' '

   isbest1=0
   isbest8=0
   fc21=0.
   fc28=0.
   do icand=1,ncand
      fc0=candidates(icand,1)
      detmet=candidates(icand,2)

! Downconvert and downsample a slice of the spectrum centered on the
! rough estimate of the candidates frequency.
! Output array c2 is complex baseband sampled at 12000/ndown Sa/sec.
! The size of the downsampled c2 array is nfft2=nfft1/ndown

      call fst280_downsample(c_bigfft,nfft1,ndown,fc0,c2)
      
      call timer('sync280 ',0)
      do isync=0,1
         if(isync.eq.0) then
            fc1=0.0
            is0=1.5*nint(fs2)
            ishw=1.5*is0
            isst=4*hmod
            ifhw=12
            df=.1*baud
         else if(isync.eq.1) then
            fc1=fc21
            if(hmod.eq.1) fc1=fc28
            is0=isbest1
            if(hmod.eq.1) is0=isbest8
            ishw=4*hmod
            isst=1*hmod
            ifhw=7
            df=.02*baud
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
      enddo
      call timer('sync280 ',1)

      if(smax8/smax1 .lt. 0.65 ) then
         fc2=fc21
         isbest=isbest1
         if(hmod.gt.1) ntmax=1
         njitter=2
      else
         fc2=fc28
         isbest=isbest8
         if(hmod.gt.1) ntmax=1
         njitter=2
      endif
      fc_synced = fc0 + fc2
      dt_synced = (isbest-fs2)*dt2  !nominal dt is 1 second so frame starts at sample fs2
      candidates(icand,3)=fc_synced
      candidates(icand,4)=isbest
   enddo 
! remove duplicate candidates
   do icand=1,ncand
      fc=candidates(icand,3)
      isbest=nint(candidates(icand,4))
      do ic2=1,ncand
         fc2=candidates(ic2,3)
         isbest2=nint(candidates(ic2,4))
         if(ic2.ne.icand .and. fc2.gt.0.0) then
            if(abs(fc2-fc).lt.0.05*baud) then ! same frequency
               if(abs(isbest2-isbest).le.2) then
                  candidates(ic2,3)=-1
               endif
            endif
         endif
      enddo
   enddo

   ic=0
   do icand=1,ncand
      if(candidates(icand,3).gt.0) then
         ic=ic+1
         candidates(ic,:)=candidates(icand,:)
      endif
   enddo
   ncand=ic
   do icand=1,ncand
      sync=candidates(icand,2)
      fc_synced=candidates(icand,3)
      isbest=nint(candidates(icand,4))   
      xdt=(isbest-nspsec)/fs2

      call fst280_downsample(c_bigfft,nfft1,ndown,fc_synced,c2)

      do ijitter=0,jittermax
         if(ijitter.eq.0) ioffset=0
         if(ijitter.eq.1) ioffset=1
         if(ijitter.eq.2) ioffset=-1
         is0=isbest+ioffset
         if(is0.lt.0) cycle
         cframe=c2(is0:is0+164*nss-1)
         bitmetrics=0
         call get_fst280_bitmetrics(cframe,nss,hmod,ntmax,bitmetrics,s4,badsync)
         if(badsync) cycle

         hbits=0
         where(bitmetrics(:,1).ge.0) hbits=1
         ns1=count(hbits( 71: 78).eq.(/0,0,0,1,1,0,1,1/))
         ns2=count(hbits( 79: 86).eq.(/0,1,0,0,1,1,1,0/))
         ns3=count(hbits(157:164).eq.(/0,0,0,1,1,0,1,1/))
         ns4=count(hbits(165:172).eq.(/0,1,0,0,1,1,1,0/))
         ns5=count(hbits(243:250).eq.(/0,0,0,1,1,0,1,1/))
         ns6=count(hbits(251:258).eq.(/0,1,0,0,1,1,1,0/))
         nsync_qual=ns1+ns2+ns3+ns4+ns5+ns6
         if(nsync_qual.lt. 26) cycle                   !### Value ?? ###

         scalefac=2.83
         llra(  1: 14)=bitmetrics(  1: 14, 1)
         llra( 15: 28)=bitmetrics(315:328, 1)
         llra( 29: 42)=bitmetrics( 15: 28, 1)
         llra( 43: 56)=bitmetrics(301:314, 1)
         llra( 57: 98)=bitmetrics( 29: 70, 1)
         llra( 99:168)=bitmetrics( 87:156, 1)
         llra(169:238)=bitmetrics(173:242, 1)
         llra(239:280)=bitmetrics(259:300, 1)
         llra=scalefac*llra
         llrb(  1: 14)=bitmetrics(  1: 14, 2)
         llrb( 15: 28)=bitmetrics(315:328, 2)
         llrb( 29: 42)=bitmetrics( 15: 28, 2)
         llrb( 43: 56)=bitmetrics(301:314, 2)
         llrb( 57: 98)=bitmetrics( 29: 70, 2)
         llrb( 99:168)=bitmetrics( 87:156, 2)
         llrb(169:238)=bitmetrics(173:242, 2)
         llrb(239:280)=bitmetrics(259:300, 2)
         llrb=scalefac*llrb
         llrc(  1: 14)=bitmetrics(  1: 14, 3)
         llrc( 15: 28)=bitmetrics(315:328, 3)
         llrc( 29: 42)=bitmetrics( 15: 28, 3)
         llrc( 43: 56)=bitmetrics(301:314, 3)
         llrc( 57: 98)=bitmetrics( 29: 70, 3)
         llrc( 99:168)=bitmetrics( 87:156, 3)
         llrc(169:238)=bitmetrics(173:242, 3)
         llrc(239:280)=bitmetrics(259:300, 3)
         llrc=scalefac*llrc
         llrd(  1: 14)=bitmetrics(  1: 14, 4)
         llrd( 15: 28)=bitmetrics(315:328, 4)
         llrd( 29: 42)=bitmetrics( 15: 28, 4)
         llrd( 43: 56)=bitmetrics(301:314, 4)
         llrd( 57: 98)=bitmetrics( 29: 70, 4)
         llrd( 99:168)=bitmetrics( 87:156, 4)
         llrd(169:238)=bitmetrics(173:242, 4)
         llrd(239:280)=bitmetrics(259:300, 4)
         llrd=scalefac*llrd
         apmask=0

         do itry=1,ntmax
            if(itry.eq.1) llr=llra
            if(itry.eq.2) llr=llrb
            if(itry.eq.3) llr=llrc
            if(itry.eq.4) llr=llrd
            dmin=0.0
            nharderrors=-1
            unpk77_success=.false.
            if(iwspr.eq.0) then
               maxosd=2
               call timer('d280_101',0)
               call decode280_101(llr,Keff,maxosd,norder,apmask,message101, &
                    cw,ntype,nharderrors,dmin)
               call timer('d280_101',1)
            else
               maxosd=2
               call timer('d280_74 ',0)
               call decode280_74(llr,Keff,maxosd,norder,apmask,message74,cw, &
                    ntype,nharderrors,dmin)
               call timer('d280_74 ',1)
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
               if(unpk77_success) then
                  idupe=0
                  do i=1,ndecodes
                     if(decodes(i).eq.msg) idupe=1
                  enddo
                  if(idupe.eq.1) exit
                  ndecodes=ndecodes+1
                  decodes(ndecodes)=msg
                  if(iwspr.eq.0) then
                     call get_fst280_tones_from_bits(message101,itone,iwspr)
                     xsig=0
                     do i=1,NN
                        xsig=xsig+s4(itone(i),i)**2
                     enddo
                     arg=400.0*(xsig/base)-1.0
                     if(arg.gt.0.0) then
                        xsnr=10*log10(arg)-21.0-11.7*log10(nsps/800.0)
                     else
                        xsnr=-99.9
                     endif
                  endif
                  nsnr=nint(xsnr)
                  iaptype=0
                  qual=0.
                  fsig=fc_synced - 1.5*hmod*baud
!write(21,'(8i4,f7.1,f7.2,3f7.1,1x,a37)') &
!  nutc,icand,itry,iaptype,ijitter,ntype,nsync_qual,nharderrors,dmin,sync,xsnr,xdt,fsig,msg
                  call this%callback(nutc,smax1,nsnr,xdt,fsig,msg,    &
                       iaptype,qual,ntrperiod)
                  goto 2002
               else
                  cycle
               endif
            endif
         enddo  ! metrics
      enddo  ! istart jitter
2002  continue
   enddo !candidate list!ws

   return
 end subroutine decode

 subroutine sync_fst280(cd0,i0,f0,hmod,ncoh,np,nss,fs,sync)

! Compute sync power for a complex, downsampled FST280 signal.

   include 'fst280/fst280_params.f90'
   complex cd0(0:np-1)
   complex, allocatable, save ::  csync(:)
   complex, allocatable, save ::  csynct(:)
   complex ctwk(8*nss)
   complex z1,z2,z3
   logical first
   integer hmod,isyncword(0:7)
   real f0save
   data isyncword/0,1,3,2,1,0,2,3/
   data first/.true./,f0save/0.0/,nss0/-1/
   save first,twopi,dt,fac,f0save,nss0
   p(z1)=(real(z1*fac)**2 + aimag(z1*fac)**2)**0.5     !Compute power

   if(nss.ne.nss0 .and. allocated(csync)) deallocate(csync,csynct)
   if(first .or. nss.ne.nss0) then
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
      nss0=nss
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

   i1=i0+35*nss                            !Costas arrays
   i2=i0+78*nss
   i3=i0+121*nss

   s1=0.0
   s2=0.0
   s3=0.0
   nsec=8/ncoh
   do i=1,nsec
      is=(i-1)*ncoh*nss
      z1=0
      if(i1+is.ge.1) then
         z1=sum(cd0(i1+is:i1+is+ncoh*nss-1)*conjg(csynct(is+1:is+ncoh*nss)))
      endif
      z2=sum(cd0(i2+is:i2+is+ncoh*nss-1)*conjg(csynct(is+1:is+ncoh*nss)))
      z3=0
      if(i3+is+ncoh*nss-1.le.np) then
         z3=sum(cd0(i3+is:i3+is+ncoh*nss-1)*conjg(csynct(is+1:is+ncoh*nss)))
      endif
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

 subroutine get_candidates_fst280(c_bigfft,nfft1,nsps,hmod,fs,fa,fb,   &
      ncand,candidates,base)

   complex c_bigfft(0:nfft1/2)
   integer hmod
   integer indx(100)
   real candidates(100,4)
   real candidates0(100,4)
   real snr_cand(100)
   real s(18000)
   real s2(18000)
   data nfft1z/-1/
   save nfft1z

   nh1=nfft1/2
   df1=fs/nfft1
   baud=fs/nsps
   df2=baud/2.0
   nd=df2/df1
   ndh=nd/2
   ia=nint(max(100.0,fa)/df2)
   ib=nint(min(4800.0,fb)/df2)
   signal_bw=4*(12000.0/nsps)*hmod
   analysis_bw=min(4800.0,fb)-max(100.0,fa)
   noise_bw=10.0*signal_bw
   if(analysis_bw.gt.noise_bw) then
      ina=ia
      inb=ib
   else
      fcenter=(fa+fb)/2.0
      fl = max(100.0,fcenter-noise_bw/2.)/df2
      fh = min(4800.0,fcenter+noise_bw/2.)/df2
      ina=nint(fl)
      inb=nint(fh)
   endif
   s=0.
   do i=ina,inb   ! noise analysis window includes signal analysis window
      j0=nint(i*df2/df1)
      do j=j0-ndh,j0+ndh
         s(i)=s(i) + real(c_bigfft(j))**2 + aimag(c_bigfft(j))**2
      enddo
   enddo
   ina=max(ina,1+3*hmod) 
   inb=min(inb,18000-3*hmod)
   s2=0.
   do i=ina,inb
      s2(i)=s(i-hmod*3) + s(i-hmod) +s(i+hmod) +s(i+hmod*3)
   enddo
   call pctile(s2(ina+hmod*3:inb-hmod*3),inb-ina+1-hmod*6,30,base)
   s2=s2/base
   thresh=1.25

   ncand=0
   candidates=0
   if(ia.lt.3) ia=3
   if(ib.gt.18000-2) ib=18000-2
   do i=ia,ib
      if((s2(i).gt.s2(i-2)).and. &
           (s2(i).gt.s2(i+2)).and. &
           (s2(i).gt.thresh).and.ncand.lt.100) then
         ncand=ncand+1
         candidates(ncand,1)=df2*i
         candidates(ncand,2)=s2(i)
      endif
   enddo

   snr_cand=0.
   snr_cand(1:ncand)=candidates(1:ncand,2)
   call indexx(snr_cand,ncand,indx)
   nmax=min(ncand,20)
   do i=1,nmax
      j=indx(ncand+1-i)
      candidates0(i,1:4)=candidates(j,1:4)
   enddo
   ncand=nmax
   candidates(1:ncand,1:4)=candidates0(1:ncand,1:4)
   candidates(ncand+1:,1:4)=0.
   return 
 end subroutine get_candidates_fst280

end module fst280_decode
