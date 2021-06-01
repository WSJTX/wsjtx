module fst4_decode

   type :: fst4_decoder
      procedure(fst4_decode_callback), pointer :: callback
   contains
      procedure :: decode
   end type fst4_decoder

   abstract interface
      subroutine fst4_decode_callback (this,nutc,sync,nsnr,dt,freq,    &
         decoded,nap,qual,ntrperiod,lwspr,fmid,w50)
         import fst4_decoder
         implicit none
         class(fst4_decoder), intent(inout) :: this
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
         real, intent(in) :: fmid
         real, intent(in) :: w50
      end subroutine fst4_decode_callback
   end interface

contains

   subroutine decode(this,callback,iwave,nutc,nQSOProgress,nfa,nfb,nfqso, &
      ndepth,ntrperiod,nexp_decode,ntol,emedelay,lagain,lapcqonly,mycall, &
      hiscall,iwspr)

      use prog_args
      use timer_module, only: timer
      use packjt77
      use, intrinsic :: iso_c_binding
      include 'fst4/fst4_params.f90'
      parameter (MAXCAND=100,MAXWCALLS=100)
      class(fst4_decoder), intent(inout) :: this
      procedure(fst4_decode_callback) :: callback
      character*37 decodes(100)
      character*37 msg,msgsent
      character*20 wcalls(MAXWCALLS), wpart
      character*77 c77
      character*12 mycall,hiscall
      character*12 mycall0,hiscall0
      complex, allocatable :: c2(:)
      complex, allocatable :: cframe(:)
      complex, allocatable :: c_bigfft(:)          !Complex waveform
      real llr(240),llrs(240,4)
      real candidates0(200,5),candidates(200,5)
      real bitmetrics(320,4)
      real s4(0:3,NN)
      real minsync
      logical lagain,lapcqonly
      integer itone(NN)
      integer hmod
      integer*1 apmask(240),cw(240),hdec(240)
      integer*1 message101(101),message74(74),message77(77)
      integer*1 rvec(77)
      integer apbits(240)
      integer nappasses(0:5)   ! # of decoding passes for QSO states 0-5
      integer naptypes(0:5,4)  ! (nQSOProgress,decoding pass)
      integer mcq(29),mrrr(19),m73(19),mrr73(19)

      logical badsync,unpk77_success,single_decode
      logical first,nohiscall,lwspr
      logical new_callsign,plotspec_exists,wcalls_exists,do_k50_decode
      logical decdata_exists

      integer*2 iwave(30*60*12000)

      data   mcq/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0/
      data  mrrr/0,1,1,1,1,1,1,0,1,0,0,1,0,0,1,0,0,0,1/
      data   m73/0,1,1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,0,1/
      data mrr73/0,1,1,1,1,1,1,0,0,1,1,1,0,1,0,1,0,0,1/
      data  rvec/0,1,0,0,1,0,1,0,0,1,0,1,1,1,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0, &
         1,0,0,1,0,1,1,0,0,0,0,1,0,0,0,1,0,1,0,0,1,1,1,1,0,0,1,0,1, &
         0,1,0,1,0,1,1,0,1,1,1,1,1,0,0,0,1,0,1/
      data first/.true./,hmod/1/
      save first,apbits,nappasses,naptypes,mycall0,hiscall0
      save wcalls,nwcalls

      this%callback => callback
      dxcall13=hiscall   ! initialize for use in packjt77
      mycall13=mycall

      if(iwspr.ne.0.and.iwspr.ne.1) return
      if(lagain) continue ! use lagain to keep compiler happy 

      if(first) then
! read the fst4_calls.txt file
         inquire(file=trim(data_dir)//'/fst4w_calls.txt',exist=wcalls_exists)
         if( wcalls_exists ) then
            open(42,file=trim(data_dir)//'/fst4w_calls.txt',status='unknown')
            do i=1,MAXWCALLS
               wcalls(i)=''
               read(42,fmt='(a)',end=2867) wcalls(i)
               wcalls(i)=adjustl(wcalls(i))
               if(len(trim(wcalls(i))).eq.0) exit
            enddo
2867        nwcalls=i-1
            close(42)
         endif

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
         apbits(1:77)=2*message77-1
         if(nohiscall) apbits(30)=99

10       continue
         mycall0=mycall
         hiscall0=hiscall
      endif
!************************************

      if(nfqso+nqsoprogress.eq.-999) return
      Keff=91
      nmax=15*12000
      if(ntrperiod.eq.15) then
         nsps=720
         nmax=15*12000
         ndown=18                  !nss=40,80,160,400
         nfft1=int(nmax/ndown)*ndown
      else if(ntrperiod.eq.30) then
         nsps=1680
         nmax=30*12000
         ndown=42                  !nss=40,80,168,336
         nfft1=359856  !nfft2=8568=2^3*3^2*7*17
      else if(ntrperiod.eq.60) then
         nsps=3888
         nmax=60*12000
         ndown=108
         nfft1=7500*96    ! nfft2=7500=2^2*3*5^4
      else if(ntrperiod.eq.120) then
         nsps=8200
         nmax=120*12000
         ndown=205                 !nss=40,82,164,328
         nfft1=7200*200   ! nfft2=7200=2^5*3^2*5^2
      else if(ntrperiod.eq.300) then
         nsps=21504
         nmax=300*12000
         ndown=512                 !nss=42,84,168,336
         nfft1=7020*512   ! nfft2=7020=2^2*3^3*5*13
      else if(ntrperiod.eq.900) then
         nsps=66560
         nmax=900*12000
         ndown=1664                !nss=40,80,160,320
         nfft1=6480*1664  ! nfft2=6480=2^4*3^4*5
      else if(ntrperiod.eq.1800) then
         nsps=134400
         nmax=1800*12000
         ndown=3360                !nss=40,80,160,320
         nfft1=6426*3360  ! nfft2=6426=2*3^3*7*17
      end if
      nss=nsps/ndown
      fs=12000.0                       !Sample rate
      fs2=fs/ndown
      nspsec=nint(fs2)
      dt=1.0/fs                        !Sample interval (s)
      dt2=1.0/fs2
      tt=nsps*dt                       !Duration of "itone" symbols (s)
      baud=1.0/tt
      sigbw=4.0*baud
      nfft2=nfft1/ndown                !make sure that nfft1 is exactly nfft2*ndown
      nfft1=nfft2*ndown
      nh1=nfft1/2

      allocate( c_bigfft(0:nfft1/2) )
      allocate( c2(0:nfft2-1) )
      allocate( cframe(0:160*nss-1) )

      jittermax=2
      do_k50_decode=.false.
      if(ndepth.eq.3) then
         nblock=4
         jittermax=2
         do_k50_decode=.true.
      elseif(ndepth.eq.2) then
         nblock=4
         jittermax=2
         do_k50_decode=.false.
      elseif(ndepth.eq.1) then
         nblock=4
         jittermax=0
         do_k50_decode=.false.
      endif

! Noise blanker setup
      ndropmax=1
      single_decode=iand(nexp_decode,32).ne.0
      npct=0
      nb=nexp_decode/256 - 3
      if(nb.ge.0) npct=nb
      inb1=20
      inb2=5
      if(nb.eq.-1) then
         inb2=5                !Try NB = 0, 5, 10, 15, 20%
      else if(nb.eq.-2) then
         inb2=2                !Try NB = 0, 2, 4,... 20%
      else if(nb.eq.-3) then
         inb2=1                !Try NB = 0, 1, 2,... 20%
      else
         inb1=0                !Fixed NB value, 0 to 25%
      endif


! nfa,nfb: define the noise-baseline analysis window
!  fa, fb: define the signal search window
! We usually make nfa<fa and nfb>fb so that noise baseline analysis
! window extends outside of the [fa,fb] window where we think the signals are.
!
      if(iwspr.eq.1) then  !FST4W
         nfa=max(100,nfqso-ntol-100)
         nfb=min(4800,nfqso+ntol+100)
         fa=max(100,nint(nfqso+1.5*baud-ntol))  ! signal search window
         fb=min(4800,nint(nfqso+1.5*baud+ntol))
      else if(iwspr.eq.0) then
         if(single_decode) then
            fa=max(100,nint(nfa+1.5*baud))
            fb=min(4800,nint(nfb+1.5*baud))
            ! extend noise fit 100 Hz outside of search window
            nfa=max(100,nfa-100)
            nfb=min(4800,nfb+100)
         else
            fa=max(100,nint(nfa+1.5*baud))
            fb=min(4800,nint(nfb+1.5*baud))
            ! extend noise fit 100 Hz outside of search window
            nfa=max(100,nfa-100)
            nfb=min(4800,nfb+100)
         endif
      endif

      ndecodes=0
      decodes=' '
      new_callsign=.false.
      do inb=0,inb1,inb2
         if(nb.lt.0) npct=inb ! we are looping over blanker settings
         call blanker(iwave,nfft1,ndropmax,npct,c_bigfft)

! The big fft is done once and is used for calculating the smoothed spectrum
! and also for downconverting/downsampling each candidate.
         call four2a(c_bigfft,nfft1,1,-1,0)         !r2c
         nhicoh=1
         nsyncoh=8
         minsync=1.20
         if(ntrperiod.eq.15) minsync=1.15

! Get first approximation of candidate frequencies
         call get_candidates_fst4(c_bigfft,nfft1,nsps,hmod,fs,fa,fb,nfa,nfb,  &
            minsync,ncand,candidates0)
         isbest=0
         fc2=0.
         do icand=1,ncand
            fc0=candidates0(icand,1)
            if(iwspr.eq.0 .and. nb.lt.0 .and. npct.ne.0 .and.            &
               abs(fc0-(nfqso+1.5*baud)).gt.ntol) cycle  ! blanker loop only near nfqso
            detmet=candidates0(icand,2)

! Downconvert and downsample a slice of the spectrum centered on the
! rough estimate of the candidates frequency.
! Output array c2 is complex baseband sampled at 12000/ndown Sa/sec.
! The size of the downsampled c2 array is nfft2=nfft1/ndown
            call timer('dwnsmpl ',0)
            call fst4_downsample(c_bigfft,nfft1,ndown,fc0,sigbw,c2)
            call timer('dwnsmpl ',1)

            call timer('sync240 ',0)
            call fst4_sync_search(c2,nfft2,hmod,fs2,nss,ntrperiod,nsyncoh,emedelay,sbest,fcbest,isbest)
            call timer('sync240 ',1)

            fc_synced = fc0 + fcbest
            dt_synced = (isbest-fs2)*dt2  !nominal dt is 1 second so frame starts at sample fs2
            candidates0(icand,3)=fc_synced
            candidates0(icand,4)=isbest
         enddo

! remove duplicate candidates
         do icand=1,ncand
            fc=candidates0(icand,3)
            isbest=nint(candidates0(icand,4))
            do ic2=icand+1,ncand
               fc2=candidates0(ic2,3)
               isbest2=nint(candidates0(ic2,4))
               if(fc2.gt.0.0) then
                  if(abs(fc2-fc).lt.0.10*baud) then ! same frequency
                     if(abs(isbest2-isbest).le.2) then
                        candidates0(ic2,3)=-1
                     endif
                  endif
               endif
            enddo
         enddo
         ic=0
         do icand=1,ncand
            if(candidates0(icand,3).gt.0) then
               ic=ic+1
               candidates0(ic,:)=candidates0(icand,:)
            endif
         enddo
         ncand=ic

! If FST4 mode and Single Decode is not checked, then find candidates
! within 20 Hz of nfqso and put them at the top of the list
         if(iwspr.eq.0 .and. .not.single_decode) then
            nclose=count(abs(candidates0(:,3)-(nfqso+1.5*baud)).le.20)
            k=0
            do i=1,ncand
               if(abs(candidates0(i,3)-(nfqso+1.5*baud)).le.20) then
                  k=k+1
                  candidates(k,:)=candidates0(i,:)
               endif
            enddo
            do i=1,ncand
               if(abs(candidates0(i,3)-(nfqso+1.5*baud)).gt.20) then
                  k=k+1
                  candidates(k,:)=candidates0(i,:)
               endif
            enddo
         else
            candidates=candidates0
         endif

         xsnr=0.
         do icand=1,ncand
            sync=candidates(icand,2)
            fc_synced=candidates(icand,3)
            isbest=nint(candidates(icand,4))
            xdt=(isbest-nspsec)/fs2
            if(ntrperiod.eq.15) xdt=(isbest-real(nspsec)/2.0)/fs2
            call timer('dwnsmpl ',0)
            call fst4_downsample(c_bigfft,nfft1,ndown,fc_synced,sigbw,c2)
            call timer('dwnsmpl ',1)

            do ijitter=0,jittermax
               if(ijitter.eq.0) ioffset=0
               if(ijitter.eq.1) ioffset=1
               if(ijitter.eq.2) ioffset=-1
               is0=isbest+ioffset
               iend=is0+160*nss-1
               if( is0.lt.0 .or. iend.gt.(nfft2-1) ) cycle
               cframe=c2(is0:iend)
               bitmetrics=0
               call timer('bitmetrc',0)
               call get_fst4_bitmetrics(cframe,nss,bitmetrics, &
                  s4,nsync_qual,badsync)
               call timer('bitmetrc',1)
               if(badsync) cycle

               do il=1,4
                  llrs(  1: 60,il)=bitmetrics( 17: 76, il)
                  llrs( 61:120,il)=bitmetrics( 93:152, il)
                  llrs(121:180,il)=bitmetrics(169:228, il)
                  llrs(181:240,il)=bitmetrics(245:304, il)
               enddo

               apmag=maxval(abs(llrs(:,4)))*1.1
               ntmax=nblock+nappasses(nQSOProgress)
               if(lapcqonly) ntmax=nblock+1
               if(ndepth.eq.1) ntmax=nblock ! no ap for ndepth=1
               apmask=0

               if(iwspr.eq.1) then ! 50-bit msgs, no ap decoding
                  nblock=4
                  ntmax=nblock
               endif

               do itry=1,ntmax
                  if(itry.eq.1) llr=llrs(:,1)
                  if(itry.eq.2.and.itry.le.nblock) llr=llrs(:,2)
                  if(itry.eq.3.and.itry.le.nblock) llr=llrs(:,3)
                  if(itry.eq.4.and.itry.le.nblock) llr=llrs(:,4)
                  if(itry.le.nblock) then
                     apmask=0
                     iaptype=0
                  endif

                  if(itry.gt.nblock .and. iwspr.eq.0) then ! do ap passes
                     llr=llrs(:,nblock)  ! Use largest blocksize as the basis for AP passes
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
                  if(iwspr.eq.0) then
                     maxosd=2
                     Keff=91
                     norder=3
                     call timer('d240_101',0)
                     call decode240_101(llr,Keff,maxosd,norder,apmask,message101, &
                        cw,ntype,nharderrors,dmin)
                     call timer('d240_101',1)
                     if(count(cw.eq.1).eq.0) then
                        nharderrors=-nharderrors
                        cycle
                     endif
                     write(c77,'(77i1)') mod(message101(1:77)+rvec,2)
                     call unpack77(c77,1,msg,unpk77_success)
                  elseif(iwspr.eq.1) then
! Try decoding with Keff=66
                     maxosd=2
                     call timer('d240_74 ',0)
                     Keff=66
                     norder=3
                     call decode240_74(llr,Keff,maxosd,norder,apmask,message74,cw, &
                        ntype,nharderrors,dmin)
                     call timer('d240_74 ',1)
                     if(nharderrors.lt.0) goto 3465
                     if(count(cw.eq.1).eq.0) then
                        nharderrors=-nharderrors
                        cycle
                     endif
                     write(c77,'(50i1)') message74(1:50)
                     c77(51:77)='000000000000000000000110000'
                     call unpack77(c77,1,msg,unpk77_success)
                     if(unpk77_success .and. do_k50_decode) then
! If decode was obtained with Keff=66, save call/grid in fst4w_calls.txt if not there already.
                        i1=index(msg,' ')
                        i2=i1+index(msg(i1+1:),' ')
                        wpart=trim(msg(1:i2))
! Only save callsigns/grids from type 1 messages
                        if(index(wpart,'/').eq.0 .and. index(wpart,'<').eq.0) then
                           ifound=0
                           do i=1,nwcalls
                              if(index(wcalls(i),wpart).ne.0) ifound=1
                           enddo

                           if(ifound.eq.0) then ! This is a new callsign
                              new_callsign=.true.
                              if(nwcalls.lt.MAXWCALLS) then
                                 nwcalls=nwcalls+1
                                 wcalls(nwcalls)=wpart
                              else
                                 wcalls(1:nwcalls-1)=wcalls(2:nwcalls)
                                 wcalls(nwcalls)=wpart
                              endif
                           endif
                        endif
                     endif
3465                 continue

! If no decode then try Keff=50
                     iaptype=0
                     if( .not. unpk77_success .and. do_k50_decode ) then
                        maxosd=1
                        call timer('d240_74 ',0)
                        Keff=50
                        norder=4
                        call decode240_74(llr,Keff,maxosd,norder,apmask,message74,cw, &
                           ntype,nharderrors,dmin)
                        call timer('d240_74 ',1)
                        if(count(cw.eq.1).eq.0) then
                           nharderrors=-nharderrors
                           cycle
                        endif
                        write(c77,'(50i1)') message74(1:50)
                        c77(51:77)='000000000000000000000110000'
                        call unpack77(c77,1,msg,unpk77_success)
! No CRC in this mode, so only accept the decode if call/grid have been seen before
                        if(unpk77_success) then
                           unpk77_success=.false.
                           do i=1,nwcalls
                              if(index(msg,trim(wcalls(i))).gt.0) then
                                 unpk77_success=.true.
                              endif
                           enddo
                        endif
                     endif

                  endif

                  if(nharderrors .ge.0 .and. unpk77_success) then
                     idupe=0
                     do i=1,ndecodes
                        if(decodes(i).eq.msg) idupe=1
                     enddo
                     if(idupe.eq.1) goto 800
                     ndecodes=ndecodes+1
                     decodes(ndecodes)=msg

                     if(iwspr.eq.0) then
                        call get_fst4_tones_from_bits(message101,itone,0)
                     else
                        call get_fst4_tones_from_bits(message74,itone,1)
                     endif
                     inquire(file='plotspec',exist=plotspec_exists)
                     fmid=-999.0
                     call timer('dopsprd ',0)
                     if(plotspec_exists) then
                        call dopspread(itone,iwave,nsps,nmax,ndown,hmod,  &
                           isbest,fc_synced,fmid,w50)
                     endif
                     call timer('dopsprd ',1)
                     xsig=0
                     do i=1,NN
                        xsig=xsig+s4(itone(i),i)
                     enddo
                     base=candidates(icand,5)
                     arg=600.0*(xsig/base)-1.0
                     if(arg.gt.0.0) then
                        xsnr=10*log10(arg)-35.5-12.5*log10(nsps/8200.0)
                        if(ntrperiod.eq.  15) xsnr=xsnr+2
                        if(ntrperiod.eq.  30) xsnr=xsnr+1
                        if(ntrperiod.eq. 900) xsnr=xsnr+1
                        if(ntrperiod.eq.1800) xsnr=xsnr+2
                     else
                        xsnr=-99.9
                     endif
                     nsnr=nint(xsnr)
                     qual=0.0
                     fsig=fc_synced - 1.5*baud
                     inquire(file=trim(data_dir)//'/decdata',exist=decdata_exists)
                     if(decdata_exists) then
                        hdec=0
                        where(llrs(:,1).ge.0.0) hdec=1
                        nhp=count(hdec.ne.cw) ! # hard errors wrt N=1 soft symbols
                        hd=sum(ieor(hdec,cw)*abs(llrs(:,1))) ! weighted distance wrt N=1 symbols
                        open(21,file=trim(data_dir)//'/fst4_decodes.dat',status='unknown',position='append')
                        write(21,3021) nutc,icand,itry,nsyncoh,iaptype,  &
                           ijitter,npct,ntype,Keff,nsync_qual,nharderrors,dmin,nhp,hd,  &
                           sync,xsnr,xdt,fsig,w50,trim(msg)
3021                    format(i6.6,i4,6i3,3i4,f6.1,i4,f6.1,f9.2,f6.1,f6.2,f7.1,f7.3,1x,a)
                        close(21)
                     endif
                     call this%callback(nutc,smax1,nsnr,xdt,fsig,msg,    &
                        iaptype,qual,ntrperiod,lwspr,fmid,w50)
!                     if(iwspr.eq.0 .and. nb.lt.0) go to 900
                     goto 800
                  endif
               enddo  ! metrics
            enddo  ! istart jitter
800      enddo !candidate list
      enddo ! noise blanker loop

      if(new_callsign .and. do_k50_decode) then ! re-write the fst4w_calls.txt file
         open(42,file=trim(data_dir)//'/fst4w_calls.txt',status='unknown')
         do i=1,nwcalls
            write(42,'(a20)') trim(wcalls(i))
         enddo
         close(42)
      endif

      return
   end subroutine decode

   subroutine sync_fst4(cd0,i0,f0,hmod,ncoh,np,nss,ntr,fs,sync)

! Compute sync power for a complex, downsampled FST4 signal.

      use timer_module, only: timer
      include 'fst4/fst4_params.f90'
      complex cd0(0:np-1)
      complex csync1,csync2,csynct1,csynct2
      complex ctwk(3200)
      complex z1,z2,z3,z4,z5
      integer hmod,isyncword1(0:7),isyncword2(0:7)
      real f0save
      common/sync240com/csync1(3200),csync2(3200),csynct1(3200),csynct2(3200)
      data isyncword1/0,1,3,2,1,0,2,3/
      data isyncword2/2,3,1,0,3,2,0,1/
      data f0save/-99.9/,nss0/-1/,ntr0/-1/
      save twopi,dt,fac,f0save,nss0,ntr0

      p(z1)=(real(z1*fac)**2 + aimag(z1*fac)**2)**0.5     !Compute power

      nz=8*nss
      call timer('sync240a',0)
      if(nss.ne.nss0 .or. ntr.ne.ntr0) then
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
         fac=1.0/(8.0*nss)
         nss0=nss
         ntr0=ntr
         f0save=-1.e30
      endif

      if(f0.ne.f0save) then
         dphi=twopi*f0*dt
         phi=0.0
         do i=1,nz
            ctwk(i)=cmplx(cos(phi),sin(phi))
            phi=mod(phi+dphi,twopi)
         enddo
         csynct1(1:nz)=ctwk(1:nz)*csync1(1:nz)
         csynct2(1:nz)=ctwk(1:nz)*csync2(1:nz)
         f0save=f0
         nss0=nss
      endif
      call timer('sync240a',1)

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

      if(ncoh.gt.0) then
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
            s1=s1+abs(z1)/nz
            s2=s2+abs(z2)/nz
            s3=s3+abs(z3)/nz
            s4=s4+abs(z4)/nz
            s5=s5+abs(z5)/nz
         enddo
      else
         nsub=-ncoh
         nps=nss/nsub
         do i=1,8
            do isub=1,nsub
               is=(i-1)*nss+(isub-1)*nps
               z1=0.0
               if(i1+is.ge.1) then
                  z1=sum(cd0(i1+is:i1+is+nps-1)*conjg(csynct1(is+1:is+nps)))
               endif
               z2=sum(cd0(i2+is:i2+is+nps-1)*conjg(csynct2(is+1:is+nps)))
               z3=sum(cd0(i3+is:i3+is+nps-1)*conjg(csynct1(is+1:is+nps)))
               z4=sum(cd0(i4+is:i4+is+nps-1)*conjg(csynct2(is+1:is+nps)))
               z5=0.0
               if(i5+is+ncoh*nss-1.le.np) then
                  z5=sum(cd0(i5+is:i5+is+nps-1)*conjg(csynct1(is+1:is+nps)))
               endif
               s1=s1+abs(z1)/(8*nss)
               s2=s2+abs(z2)/(8*nss)
               s3=s3+abs(z3)/(8*nss)
               s4=s4+abs(z4)/(8*nss)
               s5=s5+abs(z5)/(8*nss)
            enddo
         enddo
      endif
      sync = s1+s2+s3+s4+s5
      return
   end subroutine sync_fst4

   subroutine fst4_downsample(c_bigfft,nfft1,ndown,f0,sigbw,c1)

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

   end subroutine fst4_downsample

   subroutine get_candidates_fst4(c_bigfft,nfft1,nsps,hmod,fs,fa,fb,nfa,nfb,   &
      minsync,ncand,candidates)

      complex c_bigfft(0:nfft1/2)              !Full length FFT of raw data
      integer hmod                             !Modulation index (submode)
      integer im(1)                            !For maxloc
      real candidates(200,5)                   !Candidate list
      real, allocatable :: s(:)                !Low resolution power spectrum
      real, allocatable :: s2(:)               !CCF of s() with 4 tones
      real, allocatable :: sbase(:)            !noise baseline estimate
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
      ina=nint(max(100.0,real(nfa))/df2)       !Low freq limit for noise baseline fit
      inb=nint(min(4800.0,real(nfb))/df2)      !High freq limit for noise fit
      if(ia.lt.ina) ia=ina
      if(ib.gt.inb) ib=inb

      nnw=nint(48000.*nsps*2./fs)
      allocate (s(nnw))
      s=0.                                  !Compute low-resolution power spectrum
      do i=ina,inb   ! noise analysis window includes signal analysis window
         j0=nint(i*df2/df1)
         do j=j0-ndh,j0+ndh
            s(i)=s(i) + real(c_bigfft(j))**2 + aimag(c_bigfft(j))**2
         enddo
      enddo

      ina=max(ina,1+3*hmod)                       !Don't run off the ends
      inb=min(inb,nnw-3*hmod)
      allocate (s2(nnw))
      allocate (sbase(nnw))
      s2=0.
      do i=ina,inb                                !Compute CCF of s() and 4 tones
         s2(i)=s(i-hmod*3) + s(i-hmod) +s(i+hmod) +s(i+hmod*3)
      enddo
      npctile=30
      call fst4_baseline(s2,nnw,ina+hmod*3,inb-hmod*3,npctile,sbase)
      if(any(sbase(ina:inb).le.0.0)) return
      s2(ina:inb)=s2(ina:inb)/sbase(ina:inb)             !Normalize wrt noise level

      ncand=0
      candidates=0
      if(ia.lt.3) ia=3
      if(ib.gt.nnw-2) ib=nnw-2

! Find candidates, using the CLEAN algorithm to remove a model of each one
! from s2() after it has been found.
      pval=99.99
      do while(ncand.lt.200)
         im=maxloc(s2(ia:ib))
         iploc=ia+im(1)-1                         !Index of CCF peak
         pval=s2(iploc)                           !Peak value
         if(pval.lt.minsync) exit
         do i=-3,+3                            !Remove 0.9 of a model CCF at
            k=iploc+2*hmod*i                   !this frequency from s2()
            if(k.ge.ia .and. k.le.ib) then
               s2(k)=max(0.,s2(k)-0.9*pval*xdb(i))
            endif
         enddo
         ncand=ncand+1
         candidates(ncand,1)=df2*iploc         !Candidate frequency
         candidates(ncand,2)=pval              !Rough estimate of SNR
         candidates(ncand,5)=sbase(iploc)
      enddo
      return
   end subroutine get_candidates_fst4

   subroutine fst4_sync_search(c2,nfft2,hmod,fs2,nss,ntrperiod,nsyncoh,emedelay,sbest,fcbest,isbest)
      complex c2(0:nfft2-1)
      integer hmod
      nspsec=int(fs2)
      baud=fs2/real(nss)
      fc1=0.0
      if(emedelay.lt.0.1) then  ! search offsets from 0 s to 2 s
         is0=1.5*nspsec
         ishw=1.5*nspsec
      else      ! search plus or minus 1.5 s centered on emedelay
         is0=nint((emedelay+1.0)*nspsec)
         ishw=1.5*nspsec
      endif

      sbest=-1.e30
      do if=-12,12
         fc=fc1 + 0.1*baud*if
         do istart=max(1,is0-ishw),is0+ishw,4*hmod
            call sync_fst4(c2,istart,fc,hmod,nsyncoh,nfft2,nss,   &
               ntrperiod,fs2,sync)
            if(sync.gt.sbest) then
               fcbest=fc
               isbest=istart
               sbest=sync
            endif
         enddo
      enddo

      fc1=fcbest
      is0=isbest
      ishw=4*hmod
      isst=1*hmod

      sbest=0.0
      do if=-7,7
         fc=fc1 + 0.02*baud*if
         do istart=max(1,is0-ishw),is0+ishw,isst
            call sync_fst4(c2,istart,fc,hmod,nsyncoh,nfft2,nss,   &
               ntrperiod,fs2,sync)
            if(sync.gt.sbest) then
               fcbest=fc
               isbest=istart
               sbest=sync
            endif
         enddo
      enddo
   end subroutine fst4_sync_search

   subroutine dopspread(itone,iwave,nsps,nmax,ndown,hmod,i0,fc,fmid,w50)

! On "plotspec" special request, compute Doppler spread for a decoded signal

      include 'fst4/fst4_params.f90'
      complex, allocatable :: cwave(:)       !Reconstructed complex signal
      complex, allocatable :: g(:)           !Channel gain, g(t) in QEX paper
      real,allocatable :: ss(:)              !Computed power spectrum of g(t)
      integer itone(160)                     !Tones for this message
      integer*2 iwave(nmax)                  !Raw Rx data
      integer hmod                           !Modulation index
      data ncall/0/
      save ncall

      ncall=ncall+1
      nfft=2*nmax
      nwave=max(nmax,(NN+2)*nsps)
      allocate(cwave(0:nwave-1))
      allocate(g(0:nfft-1))
      wave=0
      fsample=12000.0
      call gen_fst4wave(itone,NN,nsps,nwave,fsample,hmod,fc,1,cwave,wave)
      cwave=cshift(cwave,-i0*ndown)
      fac=1.0/32768
      g(0:nmax-1)=fac*float(iwave)*conjg(cwave(:nmax-1))
      g(nmax:)=0.
      call four2a(g,nfft,1,-1,1)         !Forward c2c FFT

      df=12000.0/nfft
      ia=1.0/df
      smax=0.
      do i=-ia,ia                        !Find smax in +/- 1 Hz around 0.
         j=i
         if(j.lt.0) j=i+nfft
         s=real(g(j))**2 + aimag(g(j))**2
         smax=max(s,smax)
      enddo

      ia=10.1/df
      allocate(ss(-ia:ia))               !Allocate space for +/- 10 Hz
      sum1=0.
      sum2=0.
      nns=0
      do i=-ia,ia
         j=i
         if(j.lt.0) j=i+nfft
         ss(i)=(real(g(j))**2 + aimag(g(j))**2)/smax
         f=i*df
         if(f.ge.-4.0 .and. f.le.-2.0) then
            sum1=sum1 + ss(i)                  !Power between -2 and -4 Hz
            nns=nns+1
         else if(f.ge.2.0 .and. f.le.4.0) then
            sum2=sum2 + ss(i)                  !Power between +2 and +4 Hz
         endif
      enddo
      avg=min(sum1/nns,sum2/nns)               !Compute avg from smaller sum

      sum1=0.
      do i=-ia,ia
         f=i*df
         if(abs(f).le.1.0) sum1=sum1 + ss(i)-avg !Power in abs(f) < 1 Hz
      enddo

      ia=nint(1.0/df) + 1
      sum2=0.0
      xi1=-999
      xi2=-999
      xi3=-999
      sum2z=0.
      do i=-ia,ia                !Find freq range that has 50% of signal power
         sum2=sum2 + ss(i)-avg
         if(sum2.ge.0.25*sum1 .and. xi1.eq.-999.0) then
            xi1=i - 1 + (sum2-0.25*sum1)/(sum2-sum2z)
         endif
         if(sum2.ge.0.50*sum1 .and. xi2.eq.-999.0) then
            xi2=i - 1 + (sum2-0.50*sum1)/(sum2-sum2z)
         endif
         if(sum2.ge.0.75*sum1) then
            xi3=i - 1 + (sum2-0.75*sum1)/(sum2-sum2z)
            exit
         endif
         sum2z=sum2
      enddo
      xdiff=sqrt(1.0+(xi3-xi1)**2) !Keep small values from fluctuating too widely
      w50=xdiff*df                 !Compute Doppler spread
      fmid=xi2*df                  !Frequency midpoint of signal powere

      do i=-ia,ia                          !Save the spectrum for plotting
         y=ncall-1
         j=i+nint(xi2)
         if(abs(j*df).lt.10.0) y=0.99*ss(i+nint(xi2)) + ncall-1
         write(52,1010) i*df,y
1010     format(f12.6,f12.6)
      enddo

      return
   end subroutine dopspread

end module fst4_decode
