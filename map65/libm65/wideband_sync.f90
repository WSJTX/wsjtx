module wideband_sync

  type candidate
     real :: snr          !Relative S/N of sync detection
     real :: f            !Freq of sync tone, 0 to 96000 Hz
     real :: xdt          !DT of matching sync pattern, -1.0 to +4.0 s
     real :: pol          !Polarization angle, degrees
     integer :: ipol      !Polarization angle, 1 to 4 ==> 0, 45, 90, 135 deg
     integer :: iflip     !Sync type: JT65 = +/- 1, Q65 = 0
     integer :: indx
  end type candidate
  type sync_dat
     real :: ccfmax
     real :: xdt
     real :: pol
     integer :: ipol
     integer :: iflip
     logical :: birdie
  end type sync_dat

  parameter (NFFT=32768)
  parameter (MAX_CANDIDATES=50)
  parameter (SNR1_THRESHOLD=4.5)
  type(sync_dat) :: sync(NFFT)
  integer nkhz_center

  contains

subroutine get_candidates(ss,savg,xpol,jz,nfa,nfb,nts_jt65,nts_q65,cand,ncand)

! Search symbol spectra ss() over frequency range nfa to nfb (in kHz) for
! JT65 and Q65 sync patterns. The nts_* variables are the submode tone
! spacings: 1 2 4 8 16 for A B C D E.  Birdies are detected and
! excised.  Candidates are returned in the structure array cand().

  parameter (MAX_PEAKS=100)
  real ss(4,322,NFFT),savg(4,NFFT)
  real pavg(-20:20)
  integer indx(NFFT)
  logical xpol,skip,ldecoded
  type(candidate) :: cand(MAX_CANDIDATES)
  common/early/nhsym1,nhsym2,ldecoded(32768)

  call wb_sync(ss,savg,xpol,jz,nfa,nfb)          !Output to sync() array

  tstep=2048.0/11025.0        !0.185760 s: 0.5*tsym_jt65, 0.3096*tsym_q65
  df3=96000.0/NFFT
  ia=nint(1000*nfa/df3) + 1
  ib=nint(1000*nfb/df3) + 1
  if(ia.lt.1) ia=1
  if(ib.gt.NFFT-1) ib=NFFT-1
  iz=ib-ia+1
  
  call indexx(sync(ia:ib)%ccfmax,iz,indx)   !Sort by relative snr

  k=0
  do i=1,MAX_PEAKS
     n=indx(iz+1-i) + ia - 1
     f0=0.001*(n-1)*df3
     snr1=sync(n)%ccfmax
     if(snr1.lt.SNR1_THRESHOLD) exit
     flip=sync(n)%iflip
     if(flip.ne.0.0 .and. nts_jt65.eq.0) cycle
     if(flip.eq.0.0 .and. nts_q65.eq.0) cycle
     if(sync(n)%birdie) cycle

! Test for signal outside of TxT range and set bw for this signal type   
     j1=(sync(n)%xdt + 1.0)/tstep - 1.0
     j2=(sync(n)%xdt + 52.0)/tstep + 1.0
     if(flip.ne.0) j2=(sync(n)%xdt + 47.811)/tstep + 1.0
     ipol=sync(n)%ipol
     pavg=0.
     do j=1,j1
        pavg=pavg + ss(ipol,j,n-20:n+20)
     enddo
     do j=j2,jz
        pavg=pavg + ss(ipol,j,n-20:n+20)
     enddo
     jsum=j1 + (jz-j2+1)
     pmax=maxval(pavg(-2:2))              !### Why not just pavg(0) ?
     base=(sum(pavg)-pmax)/jsum
     pmax=pmax/base
     if(pmax.gt.5.0) cycle
     skip=.false.
     do m=1,k                              !Skip false syncs within signal bw
        diffhz=1000.0*(f0-cand(m)%f)
        bw=nts_q65*110.0
        if(cand(m)%iflip.ne.0) bw=nts_jt65*178.0
        if(diffhz.gt.-0.03*bw .and. diffhz.lt.1.03*bw) skip=.true.
     enddo
     if(skip) cycle
     k=k+1
     cand(k)%snr=snr1
     cand(k)%f=f0
     cand(k)%xdt=sync(n)%xdt
     cand(k)%pol=sync(n)%pol
     cand(k)%ipol=sync(n)%ipol
     cand(k)%iflip=nint(flip)
     cand(k)%indx=n
!     write(50,3050) i,k,m,f0+32.0,diffhz,bw,snr1,db(snr1)
!3050 format(3i5,f8.3,2f8.0,2f8.2)
     if(k.ge.MAX_CANDIDATES) exit
  enddo
  ncand=k

  return
end subroutine get_candidates

subroutine wb_sync(ss,savg,xpol,jz,nfa,nfb)

! Compute "orange sync curve" using the Q65 sync pattern

  use timer_module, only: timer
  parameter (NFFT=32768)
  parameter (LAGMAX=30)
  real ss(4,322,NFFT)
  real savg(4,NFFT)
  real savg_med(4)
  real ccf4(4),ccf4best(4),a(3)
  logical first,xpol
  integer isync(22)
  integer jsync0(63),jsync1(63)
  integer ip(1)

! Q65 sync symbols
  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/
  data jsync0/                                                         &
       1,  4,  5,  9, 10, 11, 12, 13, 14, 16, 18, 22, 24, 25, 28, 32,  &
       33, 34, 37, 38, 39, 40, 42, 43, 45, 46, 47, 48, 52, 53, 55, 57, &
       59, 60, 63, 64, 66, 68, 70, 73, 80, 81, 89, 90, 92, 95, 97, 98, &
       100,102,104,107,108,111,114,119,120,121,122,123,124,125,126/
  data jsync1/                                                         &
       2,  3,  6,  7,  8, 15, 17, 19, 20, 21, 23, 26, 27, 29, 30, 31,  &
       35, 36, 41, 44, 49, 50, 51, 54, 56, 58, 61, 62, 65, 67, 69, 71, &
       72, 74, 75, 76, 77, 78, 79, 82, 83, 84, 85, 86, 87, 88, 91, 93, &
       94, 96, 99,101,103,105,106,109,110,112,113,115,116,117,118/
  data first/.true./
  save first,isync,jsync0,jsync1

  tstep=2048.0/11025.0        !0.185760 s: 0.5*tsym_jt65, 0.3096*tsym_q65
  if(first) then
     fac=0.6/tstep
     do i=1,22                                !Expand the Q65 sync stride
        isync(i)=nint((isync(i)-1)*fac) + 1
     enddo
     do i=1,63
        jsync0(i)=2*(jsync0(i)-1) + 1
        jsync1(i)=2*(jsync1(i)-1) + 1
     enddo
     first=.false.
  endif

  df3=96000.0/NFFT
  ia=nint(1000*nfa/df3) + 1          !Flat frequency range for WSE converters
  ib=nint(1000*nfb/df3) + 1
  if(ia.lt.1) ia=1
  if(ib.gt.NFFT-1) ib=NFFT-1
  npol=1
  if(xpol) npol=4

  do i=1,npol
     call pctile(savg(i,ia:ib),ib-ia+1,50,savg_med(i))
  enddo
!  do i=ia,ib
!     write(14,3014) 0.001*(i-1)*df3,savg(1:npol,i)
!3014 format(5f10.3)
!  enddo

  lagbest=0
  ipolbest=1
  flip=0.

  do i=ia,ib
     ccfmax=0.
     do lag=0,LAGMAX

        ccf=0.
        ccf4=0.
        do j=1,22                        !Test for Q65 sync
           k=isync(j) + lag
           ccf4(1:npol)=ccf4(1:npol) + ss(1:npol,k,i+1) +       &
                ss(1:npol,k+1,i+1) + ss(1:npol,k+2,i+1) 
        enddo
        ccf4(1:npol)=ccf4(1:npol) - savg(1:npol,i+1)*3*22/float(jz)
        ccf=maxval(ccf4)
        ip=maxloc(ccf4)
        ipol=ip(1)
        if(ccf.gt.ccfmax) then
           ipolbest=ipol
           lagbest=lag
           ccfmax=ccf
           ccf4best=ccf4
           flip=0.
        endif

        ccf=0.
        ccf4=0.
        do j=1,63                       !Test for JT65 sync, std msg
           k=jsync0(j) + lag
           ccf4(1:npol)=ccf4(1:npol) + ss(1:npol,k,i+1) + ss(1:npol,k+1,i+1)
        enddo
        ccf4(1:npol)=ccf4(1:npol) - savg(1:npol,i+1)*2*63/float(jz)
        ccf=maxval(ccf4)
        ip=maxloc(ccf4)
        ipol=ip(1)
        if(ccf.gt.ccfmax) then
           ipolbest=ipol
           lagbest=lag
           ccfmax=ccf
           ccf4best=ccf4
           flip=1.0
        endif

        ccf=0.
        ccf4=0.
        do j=1,63                       !Test for JT65 sync, OOO msg
           k=jsync1(j) + lag
           ccf4(1:npol)=ccf4(1:npol) + ss(1:npol,k,i+1) + ss(1:npol,k+1,i+1)
        enddo
        ccf4(1:npol)=ccf4(1:npol) - savg(1:npol,i+1)*2*63/float(jz)
        ccf=maxval(ccf4)
        ip=maxloc(ccf4)
        ipol=ip(1)
        if(ccf.gt.ccfmax) then
           ipolbest=ipol
           lagbest=lag
           ccfmax=ccf
           ccf4best=ccf4
           flip=-1.0
        endif

     enddo  ! lag

     poldeg=0.
     if(xpol .and. ccfmax.ge.SNR1_THRESHOLD) then
        call polfit(ccf4best,4,a)
        poldeg=a(3)
     endif
     sync(i)%ccfmax=ccfmax
     sync(i)%xdt=lagbest*tstep-1.0
     sync(i)%pol=poldeg
     sync(i)%ipol=ipolbest
     sync(i)%iflip=flip
     sync(i)%birdie=.false.
     if(ccfmax/(savg(ipolbest,i)/savg_med(ipolbest)).lt.3.0) sync(i)%birdie=.true.
!     if(sync(i)%iflip.eq.0 .and. sync(i)%ccfmax .gt. 20.0) then
!        write(50,3050) i,lagbest,sync(i)%ccfmax,sync(i)%xdt,sync(i)%ipol,  &
!             sync(i)%birdie,ccf4best
!3050    format(2i5,f10.3,f8.2,i5,1x,L3,4f7.1)
!     endif

  enddo  ! i (frequency bin)

  call pctile(sync(ia:ib)%ccfmax,ib-ia+1,50,base)
  sync(ia:ib)%ccfmax=sync(ia:ib)%ccfmax/base

  bw=65*4*1.66666667                        !Q65-60C bandwidth
  nbw=bw/df3 + 1                            !Number of bins to blank
  syncmin=2.0
  nguard=10
  do i=ia,ib
     if(sync(i)%ccfmax.lt.syncmin) cycle
     spk=maxval(sync(i:i+nbw)%ccfmax)
     ip =maxloc(sync(i:i+nbw)%ccfmax)
     i0=ip(1)+i-1
     ja=min(i,i0-nguard)
     jb=i0+nbw+nguard
     sync(ja:jb)%ccfmax=0.
     sync(i0)%ccfmax=spk
  enddo

!  do i=ia,ib
!     write(15,3015) 0.001*(i-1)*df3+32.0,sync(i)%ccfmax,sync(i)%xdt,  &
!          sync(i)%ipol,sync(i)%iflip,sync(i)%birdie
!3015 format(3f10.3,2i6,L5)
!  enddo

  return
end subroutine wb_sync

end module wideband_sync
