subroutine q65b(nutc,nqd,fcenter,nfcal,nfsample,ikhz,mousedf,ntol,xpol,  &
     mycall0,hiscall0,hisgrid,mode_q65)

  use wavhdr
  parameter (MAXFFT1=5376000)              !56*96000
  parameter (MAXFFT2=336000)               !56*6000 (downsampled by 1/16)
  parameter (NMAX=60*12000)
  type(hdr) h                              !Header for the .wav file
  integer*2 iwave(60*12000)
  complex ca(MAXFFT1),cb(MAXFFT1)          !FFTs of raw x,y data
  complex cx(0:MAXFFT2-1),cy(0:MAXFFT2-1),cz(0:MAXFFT2)
  logical xpol,first
  real*8 fcenter
  character*12 mycall0,hiscall0
  character*12 mycall,hiscall
  character*6 hisgrid
  character*4 grid4
  character*125 cmnd
  character*62 line
  character*80 line2
  character*40 msg40
  character*15 fname
  character*80 wsjtx_dir
  common/cacb/ca,cb
  data first/.true./
  save

  if(first) then
     open(9,file='wsjtx_dir.txt',status='old')
     read(9,*) wsjtx_dir
     close(9)
     first=.false.
  endif

  mycall='K1JT'
  hiscall='IV3NWV'
  grid4='AA00'
  if(mycall0(1:1).ne.' ') mycall=mycall0
  if(hiscall0(1:1).ne.' ') hiscall=hiscall0
  if(hisgrid(1:4).ne.'    ') grid4=hisgrid(1:4)

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
  k0=((ikhz-ikhz0+48.0+0.27)*1000.0+nfcal+mousedf)/df
  if(k0.lt.nh .or. k0.gt.nfft1-nh) go to 900

  fac=1.0/nfft2
  cx(0:nh)=ca(k0:k0+nh)
  cx(nh+1:nfft2-1)=ca(k0-nh+1:k0-1)
  cx=fac*cx
  if(xpol) then
     cy(0:nh)=cb(k0:k0+nh)
     cy(nh+1:nfft2-1)=cb(k0-nh+1:k0-1)
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

!                1         2         3         4         5         6         7         8         9        10
!       12345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901'
  cmnd='jt9 -3 -X 32 -f 1079 -F 1000 -c MyCall      -x HisCall     -g FN42 -b A 000000_0001.wav  > q65_decodes.txt'
  write(cmnd(17:20),'(i4)') 1000
  write(cmnd(25:28),'(i4)') ntol
  write(cmnd(33:44),'(a12)') mycall
  write(cmnd(48:59),'(a12)') hiscall
  write(cmnd(63:66),'(a4)') grid4
  write(cmnd(71:71),'(a1)') char(ichar('A') + mode_q65-1)
  fname='000000_0001.wav'
  npol=1
  if(xpol) npol=4
  do ipol=1,npol
     if(ipol.eq.1) cz(0:MAXFFT2-1)=cx
     if(ipol.eq.2) cz(0:MAXFFT2-1)=0.707*(cx+cy)
     if(ipol.eq.3) cz(0:MAXFFT2-1)=cy
     if(ipol.eq.4) cz(0:MAXFFT2-1)=0.707*(cx-cy)
     cz(MAXFFT2)=0.
!Transform to time domain (real), fsample=12000 Hz
     call four2a(cz,2*nfft2,1,1,-1)
     do i=0,nfft2-1
        j=nfft2-1-i
        iwave(2*i+1)=nint(real(cz(j)))
        iwave(2*i+2)=nint(aimag(cz(j)))
     enddo
     iwave(2*nfft2+1:)=0
     h=default_header(12000,NMAX)
     write(fname(11:11),'(i1)') ipol
     open(25,file=fname,access='stream',status='unknown')
     write(25) h,iwave
     close(25)
     write(cmnd(83:83),'(i1)') ipol
     if(ipol.eq.2) cmnd(89:89)='>'
     call execute_command_line(trim(trim(wsjtx_dir)//cmnd))
  enddo

  open(24,file='q65_decodes.txt',status='unknown')
!           1         2         3         4         5         6
!  1234567890123456789012345678901234567890123456789012345678901234567
!  0001 -22  2.9 1081 :  EA2AGZ IK4WLV -16                     q0
!  110  101   2  1814  2.9  -11 # QRZ HB9Q JN47          1    0   30 H
  nsnr0=-99
  line2=' '
  do i=1,8
     read(24,1002,end=100) line
1002 format(a62)
     if(line(1:4).eq.'<Dec') cycle
     read(line,1010) nsnr,xdt,nfreq,msg40
1010 format(4x,i4,f5.1,i5,4x,a40)
     nfreq=nfreq+mousedf
     if(nsnr.gt.nsnr0) then
        ipol0=(i/2)*45
        nsnr0=nsnr
        write(line2,1020) ikhz,nfreq-1000,ipol0,nutc,xdt,nsnr0,msg40(1:30),msg40(39:40)
1020    format('!',i3.3,i5,i4,i6.4,f5.1,i5,' : ',a30,2x,a2)
        nsnr0=nsnr
        freq0=144.0 + 0.001*ikhz
        nfreq0=nfreq-1000
        xdt0=xdt
     endif
  enddo
  
100 if(nsnr0.gt.-40) then
     write(*,1100) trim(line2)
1100 format(a)

! Should write to lu 26 here, for Messages and Band Map windows ?

     write(21,1110)  freq0,nfreq0,xdt0,ipol0,nsnr0,nutc,msg40(1:28),msg40(39:40)
1110 format(f8.3,i5,f5.1,2i4,i5.4,2x,a28,': A',2x,a2)
  endif
  close(24,status='delete')
  
900 return
end subroutine q65b
