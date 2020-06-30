module fst240_decode

   type :: fst240_decoder
      procedure(fst240_decode_callback), pointer :: callback
   contains
      procedure :: decode
   end type fst240_decoder

   abstract interface
      subroutine fst240_decode_callback (this,nutc,sync,nsnr,dt,freq,    &
         decoded,nap,qual,ntrperiod,lwspr)
         import fst240_decoder
         implicit none
         class(fst240_decoder), intent(inout) :: this
         integer, intent(in) :: nutc
         real, intent(in) :: sync
         integer, intent(in) :: nsnr
         real, intent(in) :: dt
         real, intent(in) :: freq
         character(len=37), intent(in) :: decoded
         integer, intent(in) :: nap
         real, intent(in) :: qual
         integer, intent(in) :: ntrperiod
         logical, intent(in) :: lwspr
      end subroutine fst240_decode_callback
   end interface

contains

   subroutine decode(this,callback,iwave,nutc,nQSOProgress,nfqso,    &
      nfa,nfb,nsubmode,ndeep,ntrperiod,nexp_decode,ntol,nzhsym,    &
      emedelay,lapcqonly,napwid,mycall,hiscall,nfsplit,iwspr)

      use timer_module, only: timer
      use packjt77
      include 'fst240/fst240_params.f90'
      parameter (MAXCAND=100)
      class(fst240_decoder), intent(inout) :: this
      procedure(fst240_decode_callback) :: callback
      character*37 decodes(100)
      character*37 msg,msgsent
      character*77 c77
      character*12 mycall,hiscall
      character*12 mycall0,hiscall0
      complex, allocatable :: c2(:)
      complex, allocatable :: cframe(:)
      complex, allocatable :: c_bigfft(:)          !Complex waveform
      real, allocatable :: r_data(:)
      real llr(240),llra(240),llrb(240),llrc(240),llrd(240)
      real candidates(100,4)
      real bitmetrics(320,4)
      real s4(0:3,NN)
      real minsync
      logical lapcqonly
      integer itone(NN)
      integer hmod
      integer*1 apmask(240),cw(240)
      integer*1 hbits(320)
      integer*1 message101(101),message74(74),message77(77)
      integer*1 rvec(77)
      integer apbits(240)
      integer nappasses(0:5)   ! # of decoding passes for QSO states 0-5
      integer naptypes(0:5,4)  ! (nQSOProgress,decoding pass)
      integer mcq(29),mrrr(19),m73(19),mrr73(19)

      logical badsync,unpk77_success,single_decode
      logical first,nohiscall,lwspr

      integer*2 iwave(300*12000)

      data   mcq/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0/
      data  mrrr/0,1,1,1,1,1,1,0,1,0,0,1,0,0,1,0,0,0,1/
      data   m73/0,1,1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,0,1/
      data mrr73/0,1,1,1,1,1,1,0,0,1,1,1,0,1,0,1,0,0,1/
      data  rvec/0,1,0,0,1,0,1,0,0,1,0,1,1,1,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0, &
         1,0,0,1,0,1,1,0,0,0,0,1,0,0,0,1,0,1,0,0,1,1,1,1,0,0,1,0,1, &
         0,1,0,1,0,1,1,0,1,1,1,1,1,0,0,0,1,0,1/
      data first/.true./
      save first,apbits,nappasses,naptypes,mycall0,hiscall0

      this%callback => callback

      dxcall13=hiscall   ! initialize for use in packjt77
      mycall13=mycall

      if(first) then
         mcq=2*mod(mcq+rvec(1:29),2)-1
         mrrr=2*mod(mrrr+rvec(59:77),2)-1
         m73=2*mod(m73+rvec(59:77),2)-1
         mrr73=2*mod(mrr73+rvec(59:77),2)-1

         nappasses(0)=2
         nappasses(1)=2
         nappasses(2)=2
         nappasses(3)=2
         nappasses(4)=2
         nappasses(5)=3

! iaptype
!------------------------
!   1        CQ     ???    ???           (29 ap bits)
!   2        MyCall ???    ???           (29 ap bits)
!   3        MyCall DxCall ???           (58 ap bits)
!   4        MyCall DxCall RRR           (77 ap bits)
!   5        MyCall DxCall 73            (77 ap bits)
!   6        MyCall DxCall RR73          (77 ap bits)
!********

         naptypes(0,1:4)=(/1,2,0,0/) ! Tx6 selected (CQ)
         naptypes(1,1:4)=(/2,3,0,0/) ! Tx1
         naptypes(2,1:4)=(/2,3,0,0/) ! Tx2
         naptypes(3,1:4)=(/3,6,0,0/) ! Tx3
         naptypes(4,1:4)=(/3,6,0,0/) ! Tx4
         naptypes(5,1:4)=(/3,1,2,0/) ! Tx5

         mycall0=''
         hiscall0=''
         first=.false.
      endif

      l1=index(mycall,char(0))
      if(l1.ne.0) mycall(l1:)=" "
      l1=index(hiscall,char(0))
      if(l1.ne.0) hiscall(l1:)=" "
      if(mycall.ne.mycall0 .or. hiscall.ne.hiscall0) then
         apbits=0
         apbits(1)=99
         apbits(30)=99

         if(len(trim(mycall)) .lt. 3) go to 10

         nohiscall=.false.
         hiscall0=hiscall
         if(len(trim(hiscall0)).lt.3) then
            hiscall0=mycall  ! use mycall for dummy hiscall - mycall won't be hashed.
            nohiscall=.true.
         endif
         msg=trim(mycall)//' '//trim(hiscall0)//' RR73'
         i3=-1
         n3=-1
         call pack77(msg,i3,n3,c77)
         call unpack77(c77,1,msgsent,unpk77_success)
         if(i3.ne.1 .or. (msg.ne.msgsent) .or. .not.unpk77_success) go to 10
         read(c77,'(77i1)') message77
         message77=mod(message77+rvec,2)
         call encode174_91(message77,cw)
         apbits=2*cw-1
         if(nohiscall) apbits(30)=99

10       continue
         mycall0=mycall
         hiscall0=hiscall
      endif
!************************************

      hmod=2**nsubmode
      if(nfqso+nqsoprogress.eq.-999) return
      Keff=91
      nmax=15*12000
      single_decode=iand(nexp_decode,32).eq.32
      if(ntrperiod.eq.15) then
         nsps=720
         nmax=15*12000
         ndown=18/hmod !nss=40,80,160,400
         if(hmod.eq.4) ndown=4
         if(hmod.eq.8) ndown=2
         nfft1=int(nmax/ndown)*ndown
      else if(ntrperiod.eq.30) then
         nsps=1680
         nmax=30*12000
         ndown=42/hmod !nss=40,80,168,336
         nfft1=359856
         if(hmod.eq.4) then
            ndown=10
            nfft1=nmax
         endif
         if(hmod.eq.8) then
            ndown=5
            nfft1=nmax
         endif
      else if(ntrperiod.eq.60) then
         nsps=3888
         nmax=60*12000
         ndown=96/hmod !nss=36,81,162,324
         if(hmod.eq.1) ndown=108
         nfft1=int(719808/ndown)*ndown
      else if(ntrperiod.eq.120) then
         nsps=8200
         nmax=120*12000
         ndown=200/hmod !nss=40,82,164,328
         if(hmod.eq.1) ndown=205
         nfft1=int(nmax/ndown)*ndown
      else if(ntrperiod.eq.300) then
         nsps=21504
         nmax=300*12000
         ndown=512/hmod !nss=42,84,168,336
         nfft1=int((nmax-200)/ndown)*ndown
      end if
      nss=nsps/ndown
      fs=12000.0                       !Sample rate
      fs2=fs/ndown
      nspsec=nint(fs2)
      dt=1.0/fs                        !Sample interval (s)
      dt2=1.0/fs2
      tt=nsps*dt                       !Duration of "itone" symbols (s)
      baud=1.0/tt
      sigbw=4.0*hmod*baud
      nfft2=nfft1/ndown                !make sure that nfft1 is exactly nfft2*ndown
      nfft1=nfft2*ndown
      nh1=nfft1/2

      allocate( r_data(1:nfft1+2) )
      allocate( c_bigfft(0:nfft1/2) )

      allocate( c2(0:nfft2-1) )
      allocate( cframe(0:160*nss-1) )


      if(ndeep.eq.3) then
         nblock=1
         if(hmod.eq.1) nblock=4      ! number of block sizes to try
         jittermax=2
         norder=3
      elseif(ndeep.eq.2) then
         nblock=1
         if(hmod.eq.1) nblock=3
         jittermax=0
         norder=3
      elseif(ndeep.eq.1) then
         nblock=1
         jittermax=0
         norder=3
      endif

! The big fft is done once and is used for calculating the smoothed spectrum
! and also for downconverting/downsampling each candidate.
      r_data(1:nfft1)=iwave(1:nfft1)
      r_data(nfft1+1:nfft1+2)=0.0
      call four2a(r_data,nfft1,1,-1,0)
      c_bigfft=cmplx(r_data(1:nfft1+2:2),r_data(2:nfft1+2:2))
iwspr=1
      if(iwspr.eq.0) then
         itype1=1
         itype2=1
      elseif( iwspr.eq.1 ) then
         itype1=2
         itype2=2
      elseif( iwspr.eq.2 ) then
         itype1=1
         itype2=2
      endif

      do iqorw=itype1,itype2  ! iqorw=1 for QSO mode and iqorw=2 for wspr-type messages
         if( iwspr.lt.2 ) then
            if( single_decode ) then
               fa=max(100,nint(nfqso+1.5*hmod*baud-ntol))
               fb=min(4800,nint(nfqso+1.5*hmod*baud+ntol))
            else
               fa=max(100,nfa)
               fb=min(4800,nfb)
            endif
         elseif( iwspr.eq.2 .and. iqorw.eq.1 ) then
            fa=max(100,nfa)
            fb=nfsplit
         elseif( iwspr.eq.2 .and. iqorw.eq.2 ) then
            fa=nfsplit
            fb=min(4800,nfb)
         endif

         minsync=1.25
         if(iqorw.eq.2) then
            minsync=1.2
         endif

! Get first approximation of candidate frequencies
         call get_candidates_fst240(c_bigfft,nfft1,nsps,hmod,fs,fa,fb,     &
            minsync,ncand,candidates,base)

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

            call fst240_downsample(c_bigfft,nfft1,ndown,fc0,sigbw,c2)

            call timer('sync240 ',0)
            do isync=0,1
               if(isync.eq.0) then
                  fc1=0.0
                  if(emedelay.lt.0.1) then  ! search offsets from 0 s to 2 s
                     is0=1.5*nspsec
                     ishw=1.5*nspsec
                  else                      ! search plus or minus 1.5 s centered on emedelay
                     is0=nint(emedelay*nspsec)
                     ishw=1.5*nspsec
                  endif
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
                     call sync_fst240(c2,istart,fc,hmod,1,nfft2,nss,fs2,sync1)
                     call sync_fst240(c2,istart,fc,hmod,8,nfft2,nss,fs2,sync8)
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
            call timer('sync240 ',1)

            if(smax8/smax1 .lt. 0.65 ) then
               fc2=fc21
               isbest=isbest1
               njitter=2
            else
               fc2=fc28
               isbest=isbest8
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
                  if(abs(fc2-fc).lt.0.10*baud) then ! same frequency
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
         xsnr=0.

         do icand=1,ncand
            sync=candidates(icand,2)
            fc_synced=candidates(icand,3)
            isbest=nint(candidates(icand,4))
            xdt=(isbest-nspsec)/fs2
            if(ntrperiod.eq.15) xdt=(isbest-real(nspsec)/2.0)/fs2
            call fst240_downsample(c_bigfft,nfft1,ndown,fc_synced,sigbw,c2)

            do ijitter=0,jittermax
               if(ijitter.eq.0) ioffset=0
               if(ijitter.eq.1) ioffset=1
               if(ijitter.eq.2) ioffset=-1
               is0=isbest+ioffset
               if(is0.lt.0) cycle
               cframe=c2(is0:is0+160*nss-1)
               bitmetrics=0
               call get_fst240_bitmetrics(cframe,nss,hmod,nblock,bitmetrics,s4,badsync)
               if(badsync) cycle

               hbits=0
               where(bitmetrics(:,1).ge.0) hbits=1
               ns1=count(hbits(  1: 16).eq.(/0,0,0,1,1,0,1,1,0,1,0,0,1,1,1,0/))
               ns2=count(hbits( 77: 92).eq.(/1,1,1,0,0,1,0,0,1,0,1,1,0,0,0,1/))
               ns3=count(hbits(153:168).eq.(/0,0,0,1,1,0,1,1,0,1,0,0,1,1,1,0/))
               ns4=count(hbits(229:244).eq.(/1,1,1,0,0,1,0,0,1,0,1,1,0,0,0,1/))
               ns5=count(hbits(305:320).eq.(/0,0,0,1,1,0,1,1,0,1,0,0,1,1,1,0/))
               nsync_qual=ns1+ns2+ns3+ns4+ns5
               if(nsync_qual.lt. 44) cycle                   !### Value ?? ###

               scalefac=2.83
               llra(  1: 60)=bitmetrics( 17: 76, 1)
               llra( 61:120)=bitmetrics( 93:152, 1)
               llra(121:180)=bitmetrics(169:228, 1)
               llra(181:240)=bitmetrics(245:304, 1)
               llra=scalefac*llra
               llrb(  1: 60)=bitmetrics( 17: 76, 2)
               llrb( 61:120)=bitmetrics( 93:152, 2)
               llrb(121:180)=bitmetrics(169:228, 2)
               llrb(181:240)=bitmetrics(245:304, 2)
               llrb=scalefac*llrb
               llrc(  1: 60)=bitmetrics( 17: 76, 3)
               llrc( 61:120)=bitmetrics( 93:152, 3)
               llrc(121:180)=bitmetrics(169:228, 3)
               llrc(181:240)=bitmetrics(245:304, 3)
               llrc=scalefac*llrc
               llrd(  1: 60)=bitmetrics( 17: 76, 4)
               llrd( 61:120)=bitmetrics( 93:152, 4)
               llrd(121:180)=bitmetrics(169:228, 4)
               llrd(181:240)=bitmetrics(245:304, 4)
               llrd=scalefac*llrd

               apmag=maxval(abs(llra))*1.1
               ntmax=nblock+nappasses(nQSOProgress)
               if(lapcqonly) ntmax=nblock+1
               if(ndeep.eq.1) ntmax=nblock
               apmask=0

               if(iqorw.eq.2) then ! 50-bit msgs, no ap decoding
                  nblock=4
                  ntmax=nblock
               endif

               do itry=1,ntmax
                  if(itry.eq.1) llr=llra
                  if(itry.eq.2.and.itry.le.nblock) llr=llrb
                  if(itry.eq.3.and.itry.le.nblock) llr=llrc
                  if(itry.eq.4.and.itry.le.nblock) llr=llrd
                  if(itry.le.nblock) then
                     apmask=0
                     iaptype=0
                  endif
                  napwid=1.2*(4.0*baud*hmod)

                  if(itry.gt.nblock) then
                     if(nblock.eq.1) llr=llra
                     if(nblock.gt.1) llr=llrc
                     iaptype=naptypes(nQSOProgress,itry-nblock)
                     if(lapcqonly) iaptype=1
                     if(iaptype.ge.2 .and. apbits(1).gt.1) cycle  ! No, or nonstandard, mycall
                     if(iaptype.ge.3 .and. apbits(30).gt.1) cycle ! No, or nonstandard, dxcall
                     if(iaptype.eq.1) then   ! CQ
                        apmask=0
                        apmask(1:29)=1
                        llr(1:29)=apmag*mcq(1:29)
                     endif

                     if(iaptype.eq.2) then  ! MyCall ??? ???
                        apmask=0
                        apmask(1:29)=1
                        llr(1:29)=apmag*apbits(1:29)
                     endif

                     if(iaptype.eq.3) then  ! MyCall DxCall ???
                        apmask=0
                        apmask(1:58)=1
                        llr(1:58)=apmag*apbits(1:58)
                     endif

                     if(iaptype.eq.4 .or. iaptype.eq.5 .or. iaptype .eq.6) then
                        apmask=0
                        apmask(1:77)=1
                        llr(1:58)=apmag*apbits(1:58)
                        if(iaptype.eq.4) llr(59:77)=apmag*mrrr(1:19)
                        if(iaptype.eq.5) llr(59:77)=apmag*m73(1:19)
                        if(iaptype.eq.6) llr(59:77)=apmag*mrr73(1:19)
                     endif
                  endif

                  dmin=0.0
                  nharderrors=-1
                  unpk77_success=.false.
                  if(iqorw.eq.1) then
                     maxosd=2
                     Keff=91
                     norder=3
                     call timer('d240_101',0)
                     call decode240_101(llr,Keff,maxosd,norder,apmask,message101, &
                        cw,ntype,nharderrors,dmin)
                     call timer('d240_101',1)
                  elseif(iqorw.eq.2) then
                     maxosd=2
                     call timer('d240_74 ',0)
                     Keff=64
                     norder=4
                     call decode240_74(llr,Keff,maxosd,norder,apmask,message74,cw, &
                        ntype,nharderrors,dmin)
                     call timer('d240_74 ',1)
                  endif

                  if(nharderrors .ge.0) then
                     if(iqorw.eq.1) then
                        write(c77,'(77i1)') mod(message101(1:77)+rvec,2)
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

                        if(iqorw.eq.1) then
                           call get_fst240_tones_from_bits(message101,itone,0)
                        else
                           call get_fst240_tones_from_bits(message74,itone,1)
                        endif
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
                     qual=0.
                     fsig=fc_synced - 1.5*hmod*baud
!write(21,'(i6,7i6,f7.1,f9.2,3f7.1,1x,a37)') &
!  nutc,icand,itry,iaptype,ijitter,ntype,nsync_qual,nharderrors,dmin,sync,xsnr,x!dt,fsig,msg
                     call this%callback(nutc,smax1,nsnr,xdt,fsig,msg,    &
                        iaptype,qual,ntrperiod,lwspr)
                     goto 2002
                  endif
               enddo  ! metrics
            enddo  ! istart jitter
2002     enddo !candidate list
      enddo ! iqorw

      return
   end subroutine decode

   subroutine sync_fst240(cd0,i0,f0,hmod,ncoh,np,nss,fs,sync)

! Compute sync power for a complex, downsampled FST240 signal.

      include 'fst240/fst240_params.f90'
      complex cd0(0:np-1)
      complex, allocatable, save ::  csync1(:),csync2(:)
      complex, allocatable, save ::  csynct1(:),csynct2(:)
      complex ctwk(8*nss)
      complex z1,z2,z3,z4,z5
      logical first
      integer hmod,isyncword1(0:7),isyncword2(0:7)
      real f0save
      data isyncword1/0,1,3,2,1,0,2,3/
      data isyncword2/2,3,1,0,3,2,0,1/
      data first/.true./,f0save/-99.9/,nss0/-1/
      save first,twopi,dt,fac,f0save,nss0
      p(z1)=(real(z1*fac)**2 + aimag(z1*fac)**2)**0.5     !Compute power

      if(nss.ne.nss0 .and. allocated(csync1)) deallocate(csync1,csync2,csynct1,csynct2)
      if(first .or. nss.ne.nss0) then
         allocate( csync1(8*nss), csync2(8*nss) )
         allocate( csynct1(8*nss), csynct2(8*nss) )
         twopi=8.0*atan(1.0)
         dt=1/fs
         k=1
         phi1=0.0
         phi2=0.0
         do i=0,7
            dphi1=twopi*hmod*(isyncword1(i)-1.5)/real(nss)
            dphi2=twopi*hmod*(isyncword2(i)-1.5)/real(nss)
            do j=1,nss
               csync1(k)=cmplx(cos(phi1),sin(phi1))
               csync2(k)=cmplx(cos(phi2),sin(phi2))
               phi1=mod(phi1+dphi1,twopi)
               phi2=mod(phi2+dphi2,twopi)
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
         csynct1=ctwk*csync1
         csynct2=ctwk*csync2
         f0save=f0
      endif

      i1=i0                            !Costas arrays
      i2=i0+38*nss
      i3=i0+76*nss
      i4=i0+114*nss
      i5=i0+152*nss

      s1=0.0
      s2=0.0
      s3=0.0
      s4=0.0
      s5=0.0

      nsec=8/ncoh
      do i=1,nsec
         is=(i-1)*ncoh*nss
         z1=0
         if(i1+is.ge.1) then
            z1=sum(cd0(i1+is:i1+is+ncoh*nss-1)*conjg(csynct1(is+1:is+ncoh*nss)))
         endif
         z2=sum(cd0(i2+is:i2+is+ncoh*nss-1)*conjg(csynct2(is+1:is+ncoh*nss)))
         z3=sum(cd0(i3+is:i3+is+ncoh*nss-1)*conjg(csynct1(is+1:is+ncoh*nss)))
         z4=sum(cd0(i4+is:i4+is+ncoh*nss-1)*conjg(csynct2(is+1:is+ncoh*nss)))
         z5=0
         if(i5+is+ncoh*nss-1.le.np) then
            z5=sum(cd0(i5+is:i5+is+ncoh*nss-1)*conjg(csynct1(is+1:is+ncoh*nss)))
         endif
         s1=s1+abs(z1)/(8*nss)
         s2=s2+abs(z2)/(8*nss)
         s3=s3+abs(z3)/(8*nss)
         s4=s4+abs(z4)/(8*nss)
         s5=s5+abs(z5)/(8*nss)
      enddo
      sync = s1+s2+s3+s4+s5

      return
   end subroutine sync_fst240

   subroutine fst240_downsample(c_bigfft,nfft1,ndown,f0,sigbw,c1)

! Output: Complex data in c(), sampled at 12000/ndown Hz

      complex c_bigfft(0:nfft1/2)
      complex c1(0:nfft1/ndown-1)

      df=12000.0/nfft1
      i0=nint(f0/df)
      ih=nint( ( f0 + 1.3*sigbw/2.0 )/df)
      nbw=ih-i0+1
      c1=0.
      c1(0)=c_bigfft(i0)
      nfft2=nfft1/ndown
      do i=1,nbw
         if(i0+i.le.nfft1/2) c1(i)=c_bigfft(i0+i)
         if(i0-i.ge.0) c1(nfft2-i)=c_bigfft(i0-i)
      enddo
      c1=c1/nfft2
      call four2a(c1,nfft2,1,1,1)            !c2c FFT back to time domain
      return

   end subroutine fst240_downsample

   subroutine get_candidates_fst240(c_bigfft,nfft1,nsps,hmod,fs,fa,fb,   &
      minsync,ncand,candidates,base)

      complex c_bigfft(0:nfft1/2)              !Full length FFT of raw data
      integer hmod                             !Modulation index (submode)
      integer im(1)                            !For maxloc
      real candidates(100,4)                   !Candidate list
      real s(18000)                            !Low resolution power spectrum
      real s2(18000)                           !CCF of s() with 4 tones
      real xdb(-3:3)                           !Model 4-tone CCF peaks
      real minsync
      data xdb/0.25,0.50,0.75,1.0,0.75,0.50,0.25/

      nh1=nfft1/2
      df1=fs/nfft1
      baud=fs/nsps                             !Keying rate
      df2=baud/2.0
      nd=df2/df1                               !s() sums this many bins of big FFT
      ndh=nd/2
      ia=nint(max(100.0,fa)/df2)               !Low frequency search limit
      ib=nint(min(4800.0,fb)/df2)              !High frequency limit
      signal_bw=4*(12000.0/nsps)*hmod
      analysis_bw=min(4800.0,fb)-max(100.0,fa)
      noise_bw=10.0*signal_bw                  !Is this a good compromise?
      if(analysis_bw.gt.noise_bw) then
         ina=ia
         inb=ib
      else
         fcenter=(fa+fb)/2.0                      !If noise_bw > analysis_bw,
         fl = max(100.0,fcenter-noise_bw/2.)/df2  !we'll search over noise_bw
         fh = min(4800.0,fcenter+noise_bw/2.)/df2
         ina=nint(fl)
         inb=nint(fh)
      endif

      s=0.                                  !Compute low-resloution power spectrum
      do i=ina,inb   ! noise analysis window includes signal analysis window
         j0=nint(i*df2/df1)
         do j=j0-ndh,j0+ndh
            s(i)=s(i) + real(c_bigfft(j))**2 + aimag(c_bigfft(j))**2
         enddo
      enddo

      ina=max(ina,1+3*hmod)                       !Don't run off the ends
      inb=min(inb,18000-3*hmod)
      s2=0.
      do i=ina,inb                                !Compute CCF of s() and 4 tones
         s2(i)=s(i-hmod*3) + s(i-hmod) +s(i+hmod) +s(i+hmod*3)
      enddo
      call pctile(s2(ina+hmod*3:inb-hmod*3),inb-ina+1-hmod*6,30,base)
      s2=s2/base                                  !Normalize wrt noise level

      ncand=0
      candidates=0
      if(ia.lt.3) ia=3
      if(ib.gt.18000-2) ib=18000-2

! Find candidates, using the CLEAN algorithm to remove a model of each one
! from s2() after it has been found.
      pval=99.99
      do while(ncand.lt.100 .and. pval.gt.minsync)
         im=maxloc(s2(ia:ib))
         iploc=ia+im(1)-1                         !Index of CCF peak
         pval=s2(iploc)                           !Peak value
         if(s2(iploc).gt.thresh) then             !Is this a possible candidate?
            do i=-3,+3                            !Remove 0.9 of a model CCF at
               k=iploc+2*hmod*i                   !this frequency from s2()
               if(k.ge.ia .and. k.le.ib) then
                  s2(k)=max(0.,s2(k)-0.9*pval*xdb(i))
               endif
            enddo
            ncand=ncand+1
            candidates(ncand,1)=df2*iploc         !Candidate frequency
            candidates(ncand,2)=pval              !Rough estimate of SNR
         endif
      enddo

      return
   end subroutine get_candidates_fst240

end module fst240_decode
