subroutine transmit(nfunc)

  use jttycom
  parameter (MAX_TONES=85*7)             !Max number of channel symbols
  integer itone(MAX_TONES)               !Channel symbols (tone frequencies)
  character*17 cdatetime
  real wave(NMAX)
  complex cwave(NMAX)

  if(ntransmitting.eq.1) return          !Ignore if we're already transmitting

!  if(nfunc.eq.1) txmsg='CQ '//trim(mycall)//' '//mygrid
!  if(nfunc.eq.2) txmsg=trim(hiscall)//' '//trim(mycall)//     &
!       ' 599 '//trim(exch)
!  if(nfunc.eq.3) txmsg=trim(hiscall)//' '//trim(mycall)//     &
!       ' R 599 '//trim(exch)
!  if(nfunc.eq.4) txmsg=trim(hiscall)//' '//trim(mycall)//' RR73'
!  if(nfunc.eq.5) txmsg='TNX 73 GL'

  if(nfunc.eq.1) txmsg='CQ '//trim(mycall)//' CQ'
  if(nfunc.eq.2) txmsg=mycall
  if(nfunc.eq.3) txmsg=trim(hiscall)//' 599 123'
  if(nfunc.eq.4) txmsg='599 456'
  if(nfunc.eq.5) txmsg=trim(hiscall)//' TU '//trim(mycall)//' CQ'

  call  genjtty(txmsg,itone,nsym)

  nsps=384                                    !Samples per symbol at 12000 Hz
  nwave=nsps*nsym                  !Length of i*2 data written to *.wav file
  txt=nwave/12000.0

  icmplx=0
  bt=2.0
  fsample=12000.0
  f0=ftx
  call gen_jttywave(itone,nsym,nsps,bt,fsample,f0,cwave,wave,icmplx,nwave)

  iwave(1:nwave)=1000.0*wave(1:nwave)
  iwave(nwave:)=0
  call set_tx_length(nwave)
  ntxok=1
  n=len(trim(txmsg))

  write(*,1010) trim(txmsg)
1010 format('Tx: ',a)

  write(12,1020) cdatetime(),0,0.0,nint(ftx),trim(txmsg)
1020 format(a17,i4,f6.2,i5,' Tx ',a)
  if(nfunc.ge.1 .and. nfunc.le.4) ntxed=nfunc
  if(nfunc.ge.1 .and. nfunc.le.5) ltx(nfunc)=.true.
  if(nfunc.eq.2 .or. nfunc.eq.3) QSO_in_progress=.true.
  txmsg=""

  return
end subroutine transmit
