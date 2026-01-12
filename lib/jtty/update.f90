subroutine update(total_time,ic1,ic2)

  ! When the audio streams are active, this routine gets called
  ! approximately every 100 ms -- determined by this statement
  ! near the end of C function jttyaudio_():
  !            Pa_Sleep(100);

  ! It functions somewhat like the GUIupdate() loop in WSJT-X, calling
  ! Rx or Tx routines as needed.

  use jttycom
  real*8 total_time
  integer*2 id(30000)
  logical transmitted,level
  character*50 line
  character*80 umsg
  character cdatetime*17
  logical synced,eom
  data nt0/-1/,transmitted/.false./,snr/-99.0/,iwrite00/9999999/
  data level/.false./
  data synced/.false./,eom/.false./
  data umsg/' '/,m/0/
  save nt0,transmitted,level,snr,iwrite00,iwrite0,synced,m

  if(ndebug.gt.0 .and. ntransmitting.eq.0 .and. &
       (abs(iwrite-iwrite00).ge.12000 .or. iwrite.lt.iwrite00)) then
     write(*,1000) iwrite,ntxok,ic1,ic2,ndebug
1000 format('Receiving iwrite:',i8,4i5)
     iwrite00=iwrite
  endif

! Some keyboard Scan Codes: ic1=0, ic2 given here
!    13  27  59  60 61 62 63 64 65 66 67  68
!    =  ESC  F1  F2 F3 F4 F5 F6 F7 F8 F9 F10
  
  if(ic1.ne.0 .or. ic2.ne.0) then
     if(ic1.eq.27 .and. ic2.eq.0) ngo=0        !ESC ==> terminate program
     if(nTxOK.eq.0 .and. ntransmitting.eq.0) then
        nfunc=0
        if(ic1.eq.0 .and. ic2.eq.59) nfunc=1   !F1 has Scan Code = 59
        if(ic1.eq.0 .and. ic2.eq.60) nfunc=2   !F2
        if(ic1.eq.0 .and. ic2.eq.61) nfunc=3   !F3
        if(ic1.eq.0 .and. ic2.eq.62) nfunc=4   !F4
        if(ic1.eq.0 .and. ic2.eq.63) nfunc=5   !F5
        if(ic1.eq.0 .and. ic2.eq.64) hiscall=txmsg(1:6)   !F6
        if(nfunc.eq.1 .or. (nfunc.ge.2 .and. hiscall.ne.'      ')) then
           call transmit(nfunc)
        endif
     endif
     if(ic1.eq.13 .and. ic2.eq.0) hiscall=hiscall_next  !Enter key
     if((ic1.eq.97 .or. ic1.eq.65) .and. ic2.eq.0) autoseq=.not.autoseq  !a or A
     if(ic1.eq.76 .and. ic2.eq.0) level=.not.level     !l or L

     if(ic2.eq.0) then
        if(ic1.eq.13) then
           call transmit(0)
           m=0
        elseif(ic1.eq.8) then
           txmsg(m:m)=' '
           if(m.ge.1) m=m-1
           call putchar(ic1)
           call putchar(32)
           call putchar(ic1)
        else
           m=m+1
           if(ic1.ge.97 .and. ic1.le.122) ic1=ic1-32
           txmsg(m:m)=char(ic1)
           call putchar(ic1)
        endif
     endif
  endif

  if(ntransmitting.eq.1) transmitted=.true.
  if(transmitted .and. ntransmitting.eq.0) then
     i1=0
     if(tx_once .and. transmitted) stop
     transmitted=.false.
  endif

  nt=total_time/0.5       !Level estimates at 0.5 s steps
  if(nt.gt.nt0 .or. ic1.ne.0 .or. ic2.ne.0) then
     if(level) then
! Measure and display the average level of signal plus noise in past 0.5 s
        k=iwrite-6000
        if(k.lt.1) k=k+NMAX
        sq=0.
        do i=1,6000
           k=k+1
           if(k.gt.NMAX) k=k-NMAX
           x=y1(k)
           sq=sq + x*x
        enddo
        sigdb=0.
        if(sq.gt.0.0) sigdb=10.0*log10((sq/6000.0))
        n=0.5*sigdb
        if(n.lt.1) n=1
        if(n.gt.50) n=50
        line=' '
        line(n:n)='*'
        write(*,1030) cdatetime(),sigdb,ntxed,nt,iwrite,iwrite-iwrite0,  &
             autoseq,QSO_in_progress,trim(line)
1030    format(a17,f6.1,i3,3i8,2L2,1x,a)
     endif  !level

     ! Call the jtty decoder here, using code from rjtty.
     ! ### Maybe call rjtty_sub(y1,iwrite,line1)  ??? ###

     noise=100
     k=iwrite-12000
     if(k.lt.1) k=k+NMAX
     do i=1,12000
        k=k+1
        if(k.gt.NMAX) k=k-NMAX
!        id(i)=y1(k)
        id(i)=y1(k) + noise*gran()
     enddo
     nutc=0
     nfqso=1500
     ndecodes=0
     if(maxval(abs(id)).gt.0) then
        nrx=-1        
        k0=0
        k1=0
        iz=30240  !### TEMPORARY ###
        eom=.false.
        f0=1500.0
        ftol=100.0
        smin=0
        xdt=0.
        f1=0.
!### WORK NEEDED HERE ###
        call jtty_decode(id,iz,f0,ftol,smin,synced,xdt,f1,snr,umsg)
!        fname=cdatetime()
!        fname(14:17)='.wav'
!        open(13,file=fname,status='unknown',access='stream')
!        h=default_header(12000,nwave)
!        write(13) h,id
!        close(13)
        if(autoseq .and.nrx.eq.2) QSO_in_progress=.true.
        if(autoseq .and. QSO_in_progress .and. nrx.ge.1 .and. nrx.le.4) then
           lrx(nrx)=.true.
           if(ntxed.eq.1) then
              if(nrx.eq.2) then
                 call transmit(3)
              else
                 call transmit(1)
              endif
           endif
           if(ntxed.eq.2) then
              if(nrx.eq.3) then
                 call transmit(4)
                 QSO_in_progress=.false.
                 write(*,1032)
1032             format('QSO complete: S+P side')
              else
                 call transmit(2)
              endif
           endif
           if(ntxed.eq.3) then
              if(nrx.eq.4) then
                 QSO_in_progress=.false.
                 write(*,1034)
1034             format('QSO complete: CQ side')
              else
                 call transmit(3)
              endif
           endif
        endif
     endif
     nt0=nt
  endif
  iwrite0=iwrite

  return
end subroutine update
