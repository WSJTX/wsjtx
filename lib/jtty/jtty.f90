program jtty

! Basic implementation of the JTTY protocol for use from the command line.

  use jtty_mod
  use jtty_fec
  use jttycom
  integer jttyaudio                         !C function for JTTY portaudio
  character*12 arg
  data idevin/-1/,idevout/-1/

  narg=iargc()
  if(narg.ne.7) then
     print*,'Usage:   jtty MyCall HisCall devin devout fTol nQSO ndebug'
     print*,'Example: jtty  K1JT   K9AN     2     5     100  123    0'
     call padevsub(idevin,idevout)          !Configure the portaudio devices
     go to 999
  endif
  call getarg(1,mycall)
  call getarg(2,hiscall)
  call getarg(3,arg)
  read(arg,*) idevin
  call getarg(4,arg)
  read(arg,*) idevout
  call getarg(5,arg)
  read(arg,*) ftol
  call getarg(6,arg)
  read(arg,*) nQSO
  call getarg(7,arg)
  read(arg,*) ndebug

  call padevsub(idevin,idevout)          !Configure the portaudio devices

  nright=1                               !Use right channel for audio input
  iwrite=0                               !Set buffer pointer
  nfsample=12000                         !Sample rate (Hz)
  ngo=1                                  !Set to 0 to stop program
  npabuf=384        !PortAudio: 384 frames per buffer, 31.25 baud, 0.032 s
  ntxok=0
  ntransmitting=0
  ltx=.false.
  lrx=.false.
  autoseq=.false.
  QSO_in_progress=.false.
  ntxed=0
  nwave=59*384
  txmsg=''
  ftx=1500.0

! Start the input and output audio streams. Note that in normal use this call
! does not return until an error or end-of-program occurs.

  ierr=jttyaudio(idevin,idevout,npabuf,nright,y1,y2,NMAX,iwrite,itx,     &
       iwave,nwave+3*npabuf,nfsample,nTxOK,nTransmitting,ngo,ndebug)

  if(ierr.ne.0) then
     print*,'Error',ierr,' starting audio input or output.'
  endif

999 end program jtty
