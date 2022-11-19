subroutine q65b(nutc,nqd,nxant,fcenter,nfcal,nfsample,ikhz,mousedf,ntol,xpol, &
     mycall0,mygrid,hiscall0,hisgrid,mode_q65,f0,fqso,newdat,nagain,          &
     max_drift,nhsym,idec)

! This routine provides an interface between MAP65 and the Q65 decoder
! in WSJT-X.  All arguments are input data obtained from the MAP65 GUI.
! Raw Rx data are available as the 96 kHz complex spectrum ca(MAXFFT1)
! in common/cacb.  If xpol is true, we also have cb(MAXFFT1) for the
! orthogonal polarization.  Decoded messages are sent back to the GUI
! on stdout.

!  use wavhdr
  use q65_decode
  use wideband_sync
  use timer_module, only: timer

  parameter (MAXFFT1=5376000)              !56*96000
  parameter (MAXFFT2=336000)               !56*6000 (downsampled by 1/16)
  parameter (NMAX=60*12000)
  parameter (RAD=57.2957795)
!  type(hdr) h                            !Header for the .wav file
  integer*2 iwave(60*12000)
  complex ca(MAXFFT1),cb(MAXFFT1)          !FFTs of raw x,y data
  complex cx(0:MAXFFT2-1),cy(0:MAXFFT2-1),cz(0:MAXFFT2)
  logical xpol,ldecoded
  integer ipk1(1)
  real*8 fcenter,freq0,freq1
  character*12 mycall0,hiscall0
  character*12 mycall,hiscall
  character*6 mygrid,hisgrid
  character*4 grid4
  character*28 msg00
  character*80 line
  character*80 wsjtx_dir
  character*1 cp,cmode*2
  common/cacb/ca,cb
  common/early/nhsym1,nhsym2,ldecoded(32768)
  common/decodes/ndecodes
  data nutc00/-1/,msg00/'                            '/
  save

  if(newdat.eq.1) nutc00=-1
  open(9,file='wsjtx_dir.txt',status='old')
  read(9,'(a)') wsjtx_dir                      !Establish the working directory
  close(9)

  if(mycall0(1:1).ne.' ') mycall=mycall0
  if(hiscall0(1:1).ne.' ') hiscall=hiscall0
  if(hisgrid(1:4).ne.'    ') grid4=hisgrid(1:4)

! Find best frequency and ipol from sync_dat, the "orange sync curve".
  df3=96000.0/32768.0
  ifreq=nint((1000.0*f0)/df3)
  ia=nint(ifreq-ntol/df3)
  ib=nint(ifreq+ntol/df3)
  ipk1=maxloc(sync(ia:ib)%ccfmax)
  ipk=ia+ipk1(1)-1
  if(ldecoded(ipk)) go to 900
  snr1=sync(ipk)%ccfmax
  ipol=1
  if(xpol) ipol=sync(ipk)%ipol

  nfft1=MAXFFT1
  nfft2=MAXFFT2
  df=96000.0/NFFT1
  if(nfsample.eq.95238) then
     nfft1=5120000
     nfft2=322560
     df=96000.0/nfft1
  endif
  nh=nfft2/2
  f_mouse=1000.0*(fqso+48.0) + mousedf
  f_ipk=ipk*df3
  k0=nint((ipk*df3-1000.0)/df)
  if(nagain.eq.1) k0=nint((f_mouse-1000.0)/df)

  if(k0.lt.nh .or. k0.gt.MAXFFT1-nfft2+1) go to 900
  if(snr1.lt.1.5) go to 900                      !### Threshold needs work? ###

  fac=1.0/nfft2
  cx(0:nfft2-1)=ca(k0:k0+nfft2-1)
  cx=fac*cx
  if(xpol) then
     cy(0:nfft2-1)=cb(k0:k0+nfft2-1)
     cy=fac*cy
  endif

! Here cx and cy (if xpol) are frequency-domain data around the selected
! QSO frequency, taken from the full-length FFT computed in filbig().
! Values for fsample, nfft1, nfft2, df, and the downsampled data rate
! are as follows:

!  fSample  nfft1       df        nfft2  fDownSampled
!    (Hz)              (Hz)                 (Hz)
!----------------------------------------------------
!   96000  5376000  0.017857143  336000   6000.000
!   95238  5120000  0.018601172  322560   5999.994

  poldeg=0.
  if(xpol) then
     poldeg=sync(ipk)%pol
     cz(0:MAXFFT2-1)=cos(poldeg/RAD)*cx + sin(poldeg/RAD)*cy
  else
     cz(0:MAXFFT2-1)=cx
  endif

  cz(MAXFFT2)=0.
! Roll off below 500 Hz and above 2500 Hz.
  ja=nint(500.0/df)
  jb=nint(2500.0/df)
  do i=0,ja
     r=0.5*(1.0+cos(i*3.14159/ja))
     cz(ja-i)=r*cz(ja-i)
     cz(jb+i)=r*cz(jb+i)
  enddo
 cz(ja+jb+1:)=0.

!Transform to time domain (real), fsample=12000 Hz
  call four2a(cz,2*nfft2,1,1,-1)
  do i=0,nfft2-1
     j=nfft2-1-i
     iwave(2*i+2)=nint(real(cz(j)))       !Note the reversed order!
     iwave(2*i+1)=nint(aimag(cz(j)))
  enddo
  iwave(2*nfft2+1:)=0

!  open(30,file='000000_0001.wav',status='unknown',access='stream')
!  h=default_header(12000,NMAX)
!  write(30) h,iwave
!  close(30)

  nsubmode=mode_q65-1
  nfa=990                   !Tight limits around ipk for the wideband decode
  nfb=1010
  if(nagain.eq.1) then      !For nagain=1, use limits of +/- ntol
     nfa=max(100,1000-ntol)
     nfb=min(2500,1000+ntol)
  endif
  nsnr0=-99             !Default snr for no decode

! NB: Frequency of ipk is now shifted to 1000 Hz.
  call map65_mmdec(nutc,iwave,nqd,nsubmode,nfa,nfb,1000,ntol,     &
       newdat,nagain,max_drift,mycall,hiscall,hisgrid)

  MHz=fcenter
  freq0=MHz + 0.001d0*ikhz

  if(nsnr0.gt.-99) then
     ldecoded(ipk)=.true.
     nq65df=nint(1000*(0.001*k0*df+nkhz_center-48.0+1.000-1.27046-ikhz))-nfcal
     nq65df=nq65df + nfreq0 - 1000
     npol=nint(poldeg)
     if(nxant.ne.0) then
        npol=npol-45
        if(npol.lt.0) npol=npol+180
     endif
     call txpol(xpol,msg0(1:22),mygrid,npol,nxant,ntxpol,cp)
     ikhz1=ikhz
     ndf=nq65df
     if(ndf.gt.500) ikhz1=ikhz + (nq65df+500)/1000
     if(ndf.lt.-500) ikhz1=ikhz + (nq65df-500)/1000
     ndf=nq65df - 1000*(ikhz1-ikhz)
     if(nqd.eq.1 .and. abs(nq65df-mousedf).lt.ntol) then
        write(line,1020) ikhz1,ndf,npol,nutc,xdt0,nsnr0,msg0(1:27),cq0,  &
             ntxpol,cp
1020    format('!',i3.3,i5,i4,i6.4,f5.1,i5,' : ',a27,a3,i4,1x,a1)
        write(*,1100) trim(line)
1100    format(a)
     endif

! Write to lu 26, for Messages and Band Map windows
     cmode=': '
     cmode(2:2)=char(ichar('A') + mode_q65-1)
     freq1=freq0 + 0.001d0*(ikhz1-ikhz)
     write(26,1014) freq1,ndf,0,0,0,xdt0,npol,0,nsnr0,nutc,msg0(1:22),   &
          ':',cp,cmode
1014 format(f8.3,i5,3i3,f5.1,i4,i3,i4,i5.4,4x,a22,1x,2a1,2x,a2)

! Suppress writing duplicates (same time, decoded message, and frequency)
! to map65_rx.log
     if(nutc.ne.nutc00 .or. msg0(1:28).ne.msg00 .or. freq1.ne.freq1_00) then
! Write to file map65_rx.log:
        ndecodes=ndecodes+1
        write(21,1110)  freq1,ndf,xdt0,npol,nsnr0,nutc,msg0(1:28),cq0
1110    format(f8.3,i5,f5.1,2i4,i5.4,2x,a28,': A',2x,a3)
        nutc00=nutc
        msg00=msg0(1:28)
        freq1_00=freq1
     endif
  endif

900 close(13)
  close(17)
  call flush(6)
  idec=-1
  read(cq0(2:2),*) idec

  return
end subroutine q65b
