program jtty

! Basic implementation of the JTTY protocol for use from the command line.

  use jtty_mod
  use jtty_fec
  integer jttyaudio                         !C function for JTTY portaudio
  character*20 pttport
  logical allok
  include 'gcom1.f90'
  common/jttycom/nwave0

  allok=.true.
  verbose=.false.
! Get home-station details
  open(10,file='jtty.ini',status='old',err=1)
  go to 2
1 print*,'Cannot open jtty.ini'
  allok=.false.
2 read(10,*,err=3) mycall,mygrid,ndevin,ndevout,pttport,exch
  go to 4
3 print*,'Error reading jtty.ini'
  allok=.false.
4 if(index(pttport,'/').lt.1) read(pttport,*) nport
  hiscall='      '
  hiscall_next='      '
  idevin=ndevin
  idevout=ndevout
  call padevsub(idevin,idevout)          !Configure the portaudio devices
  
  txsnrdb=99.0
  open(12,file='all_jtty.txt',status='unknown',position='append')
  hiscall='K9AN'
  exch='157'
  if(idevin.ne.ndevin .or. idevout.ne.ndevout) allok=.false.

  npabuf=1152
  nright=1
  iwrite=0
  iwave=0
  nfsample=12000
  ngo=1
  npabuf=384        !PortAudio: 384 frames per buffer, 31.25 baud, 0.032 s
  ntxok=0
  ntransmitting=0
  tx_once=.false.
  snrdb=99.0
  txmsg='CQ K1JT FN20'
  ltx=.false.
  lrx=.false.
  autoseq=.false.
  QSO_in_progress=.false.
  ntxed=0
  nwave=nwave0

! Start the input and output audio streams. Note that in normal use this call
! does not return until an error or end-of-program occurs.

  ierr=jttyaudio(idevin,idevout,npabuf,nright,y1,y2,NRING,iwrite,itx,     &
       iwave,nwave+3*npabuf,nfsample,nTxOK,nTransmitting,ngo)

  if(ierr.ne.0) then
     print*,'Error',ierr,' starting audio input or output.'
  endif

end program jtty
