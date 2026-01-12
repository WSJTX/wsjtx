module jttycom

  implicit none

! Variable                   Purpose
!---------------------------------------------------------------------------
  integer NMAX
  parameter(NMAX=30*12000) !Ring buffer at 12000 samples/sec
  integer iwrite           !Pointer to Rx ring buffer
  integer itx              !Pointer to Tx buffer
  integer ngo              !Set to 0 to terminate audio streams
  integer nTransmitting    !Actually transmitting?
  integer nTxOK            !OK to transmit?
  integer nport            !COM port for PTT
  logical tx_once          !Transmit one message, then exit
  logical ltx(5)           !True if msg i has been transmitted
  logical lrx(5)           !True if msg i has been received
  logical autoseq
  logical QSO_in_progress
  integer*2 y1(NMAX)       !Ring buffer for audio channel 0
  integer*2 y2(NMAX)       !Ring buffer for audio channel 1
  integer*2 iwave(NMAX)    !Data for Tx audio
  character*6 mycall
  character*6 hiscall
  character*6 hiscall_next
  character*4 mygrid
  character*4 exch
  character*80 txmsg

end module jttycom
