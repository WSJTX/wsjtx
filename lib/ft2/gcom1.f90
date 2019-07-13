! Variable              Purpose
!---------------------------------------------------------------------------
integer NRING          !Length of Rx ring buffer
integer NTZ            !Length of Tx waveform in samples
parameter(NRING=230400) !Ring buffer at 12000 samples/sec
parameter(NTZ=23040)   !144*160
parameter(NMAX=30000)  !2.5*12000
real snrdb
integer ndevin         !Device# for audio input
integer ndevout        !Device# for audio output
integer iwrite         !Pointer to Rx ring buffer
integer itx            !Pointer to Tx buffer
integer ngo            !Set to 0 to terminate audio streams
integer nTransmitting  !Actually transmitting?
integer nTxOK          !OK to transmit?
integer nport          !COM port for PTT
logical tx_once        !Transmit one message, then exit
logical ltx            !True if msg i has been transmitted
logical lrx            !True if msg i has been received
logical autoseq
logical QSO_in_progress
integer*2 y1           !Ring buffer for audio channel 0
integer*2 y2           !Ring buffer for audio channel 1
integer*2 iwave        !Data for Tx audio
character*6 mycall
character*6 hiscall
character*6 hiscall_next
character*4 mygrid
character*3 exch
character*37 txmsg

common/gcom1/snrdb,ndevin,ndevout,iwrite,itx,ngo,nTransmitting,nTxOK,nport,   &
     ntxed,tx_once,y1(NRING),y2(NRING),iwave(NTZ+3*1152),ltx(5),lrx(5),      &
     autoseq,QSO_in_progress,mycall,hiscall,hiscall_next,mygrid,exch,txmsg
