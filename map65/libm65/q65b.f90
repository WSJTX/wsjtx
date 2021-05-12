subroutine q65b(nutc,fcenter,nfcal,nfsample,ikhz,mousedf,ntol,xpol,  &
     mycall0,hiscall0,hisgrid,mode_q65)

! This routine provides an interface between MAP65 and the Q65 decoder
! in WSJT-X.  All arguments are input data obtained from the MAP65 GUI.
! Raw Rx data are available as the 96 kHz complex spectrum ca(MAXFFT1)
! in common/cacb.  If xpol is true, we also have cb(MAXFFT1) for the
! orthogonal polarization.  Decoded messages are sent back to the GUI
! on stdout.

!  use wavhdr
  use q65_decode
  use wideband2_sync
  use timer_module, only: timer

  parameter (MAXFFT1=5376000)              !56*96000
  parameter (MAXFFT2=336000)               !56*6000 (downsampled by 1/16)
  parameter (NMAX=60*12000)
!  type(hdr) h                            !Header for the .wav file
  integer*2 iwave(60*12000)
  complex ca(MAXFFT1),cb(MAXFFT1)          !FFTs of raw x,y data
  complex cx(0:MAXFFT2-1),cy(0:MAXFFT2-1),cz(0:MAXFFT2)
  logical xpol
  integer ipk1(1)
  real*8 fcenter,freq0
  character*12 mycall0,hiscall0
  character*12 mycall,hiscall
  character*6 hisgrid
  character*4 grid4
  character*80 line
  character*80 wsjtx_dir
  common/cacb/ca,cb
  save

  open(9,file='wsjtx_dir.txt',status='old')
  read(9,'(a)') wsjtx_dir                      !Establish the working directory
  close(9)

  if(mycall0(1:1).ne.' ') mycall=mycall0
  if(hiscall0(1:1).ne.' ') hiscall=hiscall0
  if(hisgrid(1:4).ne.'    ') grid4=hisgrid(1:4)

! Find best frequency and  ipol from sync_dat, the "orange sync curve".
  df3=96000.0/32768.0
  ff=ikhz+0.001*(mousedf+nfcal+1270.459)       !Supposed freq of sync tone
  ifreq=nint(1000.0*(ff-nkhz_center+48)/df3)   !Freq index into ss(4,322,32768)
  ia=nint(ifreq-ntol/df3)
  ib=nint(ifreq+ntol/df3)

!###
  ipk1=maxloc(sync(ia:ib)%ccfmax)
  ipk=ia+ipk1(1)-1
  snr1=sync(ipk)%ccfmax
  ipol=1
  if(xpol) ipol=sync(ipk)%ipol
!  print*,'BBB',ipk00,ipk,snr1,ipol
!###
  
  nfft1=MAXFFT1
  nfft2=MAXFFT2
  df=96000.0/NFFT1
  if(nfsample.eq.95238) then
     nfft1=5120000
     nfft2=322560
     df=96000.0/nfft1
  endif
  nh=nfft2/2
  ikhz0=nint(1000.0*(fcenter-int(fcenter)))
  k0=nint((ipk*df3-1000.0)/df)

  if(k0.lt.nh .or. k0.gt.nfft1-nh) go to 900
  if(snr1.lt.2.0) go to 900

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

  if(ipol.eq.1) cz(0:MAXFFT2-1)=cx
  if(ipol.eq.2) cz(0:MAXFFT2-1)=0.707*(cx+cy)
  if(ipol.eq.3) cz(0:MAXFFT2-1)=cy
  if(ipol.eq.4) cz(0:MAXFFT2-1)=0.707*(cx-cy)
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

!###
!  h=default_header(12000,NMAX)  
!  open(60,file='000000_0001.wav',access='stream',status='unknown')
!  write(60) h,iwave
!  close(60)
!###

  nsubmode=mode_q65-1
  nfa=max(100,1000-ntol)
  nfb=min(4900,1000+ntol)
  newdat=1
  nagain=0
  nsnr0=-99             !Default snr for no decode

  call timer('mmdec   ',0)
! NB: Frequency of ipk is now shifted to 1000 Hz.
  call map65_mmdec(nutc,iwave,nsubmode,nfa,nfb,1000,ntol,     &
       newdat,nagain,mycall,hiscall,hisgrid)
  call timer('mmdec   ',1)

  nfreq=nfreq0 + nhz + mousedf - 1000
  freq0=144.0 + 0.001*ikhz
  if(nsnr0.gt.-99) then
     write(line,1020) ikhz,nfreq,45*(ipol-1),nutc,xdt0,nsnr0,msg0(1:27),cq0
1020 format('!',i3.3,i5,i4,i6.4,f5.1,i5,' : ',a27,a3)
     write(*,1100) trim(line)
1100 format(a)

! Should write to lu 26 here, for Messages and Band Map windows ?
     write(26,1014) freq0,nfreq0,0,0,0,xdt0,ipol0,0,                   &
          nsnr0,nutc,msg0(1:22),':',char(ichar('A') + mode_q65-1)
1014 format(f8.3,i5,3i3,f5.1,i4,i3,i4,i5.4,4x,a22,2x,a1,3x,':',a1)

! Write to file map65_rx.log:
     write(21,1110)  freq0,nfreq,xdt0,45*(ipol-1),nsnr0,nutc,msg0(1:28),cq0
1110 format(f8.3,i5,f5.1,2i4,i5.4,2x,a28,': A',2x,a3)
  endif

900 close(13)
  close(17)
  call flush(6)

  return
end subroutine q65b
