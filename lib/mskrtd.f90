subroutine mskrtd(id2,nutc0,tsec,ntol,nrxfreq,ndepth,mycall,hiscall,      &
     bshmsg,btrain,pcoeffs,bswl,datadir,line)

! Real-time decoder for MSK144.  
! Analysis block size = NZ = 7168 samples, t_block = 0.597333 s 
! Called from hspec() at half-block increments, about 0.3 s

  use packjt77

  parameter (NZ=7168)                !Block size
  parameter (NSPM=864)               !Number of samples per message frame
  parameter (NFFT1=8192)             !FFT size for making analytic signal
  parameter (NPATTERNS=4)            !Number of frame averaging patterns to try
  parameter (NSHMEM=50)              !Number of recent SWL messages to remember

  character*4 decsym                 !"&" for mskspd or "^" for long averages
  character*37 msgreceived           !Decoded message
  character*37 msglast,msglastswl    !Used for dupechecking
  character*80 line                  !Formatted line with UTC dB T Freq Msg
  character*12 mycall,hiscall
  character*37 recent_shmsgs(NSHMEM)
  character*(*) datadir

  complex cdat(NFFT1)                !Analytic signal
  complex c(NSPM)                    !Coherently averaged complex data
  complex ct(NSPM)

  integer*2 id2(NZ)                  !Raw 16-bit data
  integer iavmask(8)
  integer iavpatterns(8,NPATTERNS)
  integer npkloc(10)
  integer nhasharray(MAXRECENT,MAXRECENT)
  integer nsnrlast,nsnrlastswl

  real d(NFFT1)
  real pow(8)
  real softbits(144)
  real xmc(NPATTERNS)
  real*8 pcoeffs(5)

  logical*1 bshmsg,btrain,bswl
  logical*1 first
  logical*1 bshdecode
  logical*1 seenb4
  logical*1 bflag
  logical*1 bvar
 
  data first/.true./
  data iavpatterns/ &
       1,1,1,1,0,0,0,0, &
       0,0,1,1,1,1,0,0, &
       1,1,1,1,1,0,0,0, &
       1,1,1,1,1,1,1,0/
  data xmc/2.0,4.5,2.5,3.5/     !Used to set time at center of averaging mask
  save first,tsec0,nutc00,pnoise,cdat,msglast,msglastswl,     &
       nsnrlast,nsnrlastswl,nhasharray,recent_shmsgs
!       nsnrlast,nsnrlastswl,nhasharray,recent_shmsgs,mycall13

  if(first) then
     tsec0=tsec
     nutc00=nutc0
     pnoise=-1.0
     do i=1,MAXRECENT
       recent_calls(i)(1:13)=' '
     enddo
     do i=1,nshmem
       recent_shmsgs(i)(1:37)=' '
     enddo
     msglast='                                     '
     msglastswl='                                     '
     nsnrlast=-99
     nsnrlastswl=-99
     mycall13=mycall//' '
     dxcall13=hiscall//' '
     first=.false.
  endif

  fc=nrxfreq

! Reset if mycall or dxcall changes
  if(mycall13(1:12).ne.mycall .or. dxcall13(1:12).ne.hiscall) first=.true.

! Dupe checking setup 
  if(nutc00.ne.nutc0 .or. tsec.lt.tsec0) then ! reset dupe checker
    msglast='                                     '
    msglastswl='                                     '
    nsnrlast=-99
    nsnrlastswl=-99
    nutc00=nutc0
  endif
  
  tframe=float(NSPM)/12000.0 
  line=char(0)
  msgreceived='                                     '
  max_iterations=10
  niterations=0
  d(1:NZ)=id2
  rms=sqrt(sum(d(1:NZ)*d(1:NZ))/NZ)
  if(rms.lt.1.0) go to 999
  fac=1.0/rms
  d(1:NZ)=fac*d(1:NZ)
  d(NZ+1:NFFT1)=0.
  bvar=.true.
  if( btrain ) bvar=.false.   ! if training, turn off rx eq
  call analytic(d,NZ,NFFT1,cdat,pcoeffs,bvar)  

! Calculate average power for each frame and for the entire block.
! If decode is successful, largest power will be taken as signal+noise.
! If no decode, entire-block average will be used to update noise estimate.
  pmax=-99
  do i=1,8 
     ib=(i-1)*NSPM+1
     ie=ib+NSPM-1
     pow(i)=real(dot_product(cdat(ib:ie),cdat(ib:ie)))*rms**2
     pmax=max(pmax,pow(i))
  enddo
  pavg=sum(pow)/8.0

! Short ping decoder uses squared-signal spectrum to determine where to
! center a 3-frame analysis window and attempts to decode each of the 
! 3 frames along with 2- and 3-frame averages. 
  np=8*NSPM
  call msk144spd(cdat,np,ntol,ndecodesuccess,msgreceived,fc,fest,tdec,navg,ct, &
                 softbits)
  bshdecode=.false.
  if(ndecodesuccess.eq.0 .and. (bshmsg.or.bswl)) then
     call msk40spd(cdat,np,ntol,mycall,hiscall,bswl,nhasharray,      &
              ndecodesuccess,msgreceived,fc,fest,tdec,navg)
     if(ndecodesuccess .ge.1) bshdecode=.true.
  endif
  if( ndecodesuccess .ge. 1 ) then
    tdec=tsec+tdec
    ipk=0
    is=0
    goto 900
  endif 

! If short ping decoder doesn't find a decode, 
! Fast - short ping decoder only. 
! Normal - try 4-frame averages
! Deep - try 4-, 5- and 7-frame averages. 
  npat=NPATTERNS
  if( ndepth .eq. 1 ) npat=0
  if( ndepth .eq. 2 ) npat=2
  do iavg=1,npat
     iavmask=iavpatterns(1:8,iavg)
     navg=sum(iavmask)
     deltaf=10.0/real(navg)  ! search increment for frequency sync
     npeaks=2
     call msk144sync(cdat(1:8*NSPM),8,ntol,deltaf,iavmask,npeaks,fc,           &
          fest,npkloc,nsyncsuccess,xmax,c)
     if( nsyncsuccess .eq. 0 ) cycle

     do ipk=1,npeaks
        do is=1,3   
           ic0=npkloc(ipk)
           if(is.eq.2) ic0=max(1,ic0-1)
           if(is.eq.3) ic0=min(NSPM,ic0+1)
           ct=cshift(c,ic0-1)
           call msk144decodeframe(ct,softbits,msgreceived,ndecodesuccess)
           if(ndecodesuccess .gt. 0) then
              tdec=tsec+xmc(iavg)*tframe
              goto 900
           endif
        enddo                         !Slicer dither
     enddo                            !Peak loop 
  enddo


  msgreceived=' '

! no decode - update noise level used for calculating displayed snr.  
  if( pnoise .lt. 0 ) then         ! initialize noise level
     pnoise=pavg
  elseif( pavg .gt. pnoise ) then  ! noise level is slow to rise
     pnoise=0.9*pnoise+0.1*pavg
  elseif( pavg .lt. pnoise ) then  ! and quick to fall
     pnoise=pavg
  endif
  go to 999

900 continue
! Successful decode - estimate snr 
  if( pnoise .gt. 0.0 ) then
    snr0=10.0*log10(pmax/pnoise-1.0)
  else
    snr0=0.0
  endif
  nsnr=nint(snr0)

  if(.not. bshdecode) then
    call msk144signalquality(ct,snr0,fest,tdec,softbits,msgreceived,hiscall,   &
                          btrain,datadir,ncorrected,eyeopening,pcoeffs)
  endif

  decsym=' &  '
  if( btrain ) decsym=' ^  '
  if( bshdecode ) then
    ncorrected=0
    eyeopening=0.0
  endif

  if( nsnr .lt. -8 ) nsnr=-8
  if( nsnr .gt. 24 ) nsnr=24

! Dupe check. 
  bflag=ndecodesuccess.eq.1 .and.                                              &
        (msgreceived.ne.msglast .or. nsnr.gt.nsnrlast .or. tsec.lt.tsec0)
  if(bflag) then
     msglast=msgreceived
     nsnrlast=nsnr
     if(.not. bshdecode) then
        call update_msk40_hasharray(nhasharray)
     endif
     write(line,1021) nutc0,nsnr,tdec,nint(fest),decsym,msgreceived,char(0)
1021 format(i6.6,i4,f5.1,i5,a4,a37,a1)
  elseif(bswl .and. ndecodesuccess.ge.2) then 
    seenb4=.false.
    do i=1,nshmem
      if( msgreceived .eq. recent_shmsgs(i) ) then
        seenb4=.true.
      endif
    enddo
    call update_recent_shmsgs(msgreceived,recent_shmsgs,nshmem)
    bflag=seenb4 .and.                                                        &
      (msgreceived.ne.msglastswl .or. nsnr.gt.nsnrlastswl .or. tsec.lt.tsec0) & 
      .and. nsnr.gt.-6
    if(bflag) then
      msglastswl=msgreceived
      nsnrlastswl=nsnr
      write(line,1021) nutc0,nsnr,tdec,nint(fest),decsym,msgreceived,char(0)
    endif
  endif
999 tsec0=tsec

  return
end subroutine mskrtd

subroutine update_recent_shmsgs(message,msgs,nsize)
  character*37 msgs(nsize)
  character*37 message
  logical*1 seen

  seen=.false.
  do i=1,nsize
    if( msgs(i) .eq. message ) seen=.true. 
  enddo

  if( .not. seen ) then
    do i=nsize,2,-1
      msgs(i)=msgs(i-1)
    enddo
    msgs(1)=message
  endif

  return
end subroutine update_recent_shmsgs
