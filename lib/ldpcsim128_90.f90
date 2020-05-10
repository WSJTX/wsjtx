program ldpcsim128_90

! Simulate the performance of the (128,90) code that is used in
! the second incarnation of MSK144.

  use packjt77
  integer, parameter:: N=128, K=90, M=N-K
!  character*12 recent_calls(NRECENT)
  character*37 msg,msgsent,msgreceived
  character*77 c77 
  character*8 arg
  integer*1 codeword(N), message77(77)
  integer*1 apmask(N),cw(N)
  integer*1 msgbits(77)
  integer*4 i4Msg6BitWords(13)
  integer nerrtot(0:N),nerrdec(0:N)
  logical unpk77_success
  real*8 rxdata(N), rxavgd(N)
  real llr(N),llra(N)

  do i=1,MAXNRECENT
    recent_calls(i)='            '
  enddo
  nerrtot=0
  nerrdec=0

  nargs=iargc()
  if(nargs.ne.5) then
    print*,'Usage: ldpcsim  niter ndeep  navg  #trials  s '
    print*,'eg:    ldpcsim    10    2     1     1000    0.75'
    return
  endif
  call getarg(1,arg)
  read(arg,*) max_iterations 
  call getarg(2,arg)
  read(arg,*) ndeep
  call getarg(3,arg)
  read(arg,*) navg
  call getarg(4,arg)
  read(arg,*) ntrials
  call getarg(5,arg)
  read(arg,*) s

  rate=real(77)/real(N)

  write(*,*) "rate: ",rate
  write(*,*) "niter= ",max_iterations," navg= ",navg," s= ",s

  msg="K1ABC RR73; W9XYZ <KH1/KH7Z> -12"
  i3=0
  n3=1
  call pack77(msg,i3,n3,c77)
  call unpack77(c77,0,msgsent,unpk77_success)
  read(c77,'(77i1)') msgbits

  write(*,*) "message sent ",msgsent

  write(*,*) 'msgbits'
  write(*,'(77i1)') msgbits

! msgbits is the 77-bit message, codeword is 128 bits
  call encode_128_90(msgbits,codeword)

!  call init_random_seed()

  write(*,*) "Eb/N0  SNR2500   ngood  nundetected  sigma    psymerr"
  do idb = 6,6,-1 
    db=idb/2.0-1.0
    sigma=1/sqrt( 2*rate*(10**(db/10.0)) )
    ngood=0
    nue=0
    nbadcrc=0
    nsumerr=0

    do itrial=1, ntrials
      rxavgd=0d0
      do iav=1,navg
!        call sgran()
! Create a realization of a noisy received word
        do i=1,N
          rxdata(i) = 2.0*codeword(i)-1.0 + sigma*gran()
        enddo
        rxavgd=rxavgd+rxdata
      enddo
      rxdata=rxavgd
      nerr=0
      do i=1,N
        if( rxdata(i)*(2*codeword(i)-1.0) .lt. 0 ) nerr=nerr+1
      enddo
      nerrtot(nerr)=nerrtot(nerr)+1

      rxav=sum(rxdata)/N
      rx2av=sum(rxdata*rxdata)/N
      rxsig=sqrt(rx2av-rxav*rxav)
      rxdata=rxdata/rxsig
! The s parameter can be tuned to trade a few tenth's dB of threshold for an order of
! magnitude in UER 
      if( s .lt. 0 ) then
        ss=sigma
      else 
        ss=s
      endif

      llr=2.0*rxdata/(ss*ss)

      apmask=0
! max_iterations is max number of belief propagation iterations

      call bpdecode128_90(llr, apmask, max_iterations, message77, cw, nharderrors, niterations)

      if(ndeep.ge.0 .and. nharderrors.lt.0) then
        call osd128_90(llr, apmask, ndeep, message77, cw, nharderrors, dmin)
      endif
 
! If the decoder finds a valid codeword, nharderrors will be .ge. 0.
      if( nharderrors .ge. 0 ) then
        write(c77,'(77i1)') message77
        call unpack77(c77,1,msgreceived,unpk77_success)
        nhw=count(cw.ne.codeword)
        if(nhw.eq.0) then ! this is a good decode
          ngood=ngood+1
          nerrdec(nerr)=nerrdec(nerr)+1 
        else              ! this is an undetected error
          nue=nue+1
        endif
      endif
      nsumerr=nsumerr+nerr
    enddo

    snr2500=db+10*log10(rate*2000.0/2500.0) ! symbol rate is 2000 s^-1 and ref BW is 2500 Hz.
    pberr=real(nsumerr)/real(ntrials*N)
    write(*,"(f4.1,4x,f5.1,1x,i8,1x,i8,7x,f5.2,3x,e10.3)") db,snr2500,ngood,nue,ss,pberr
  
  enddo

  open(unit=23,file='nerrhisto.dat',status='unknown')
  do i=0,N
    write(23,'(i4,2x,i10,i10,f10.2)') i,nerrdec(i),nerrtot(i),real(nerrdec(i))/real(nerrtot(i)+1e-10)
  enddo
  close(23)

end program ldpcsim128_90
