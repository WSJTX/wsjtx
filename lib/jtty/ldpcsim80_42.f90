program ldpcsim80_42

! Simulate the performance of the (80,42) code.
   use jtty_mod
   use jtty_fec
   integer, parameter:: N=80, K=42, M=N-K
   character*8 arg
   character*32 c32(16)
   character*80 textmessage
   integer*1 codeword(N), message32(32)
   integer*1 cw(N)
   integer modtype, channeltype, graymap(0:3)
   integer itone(40)
   integer lmax(1)
   real*8 rxdata(N)
   real bitmetrics(80)
   real llr(N)
   real s2(0:3)
   complex cs(0:3)
   logical one(0:3,0:1)

   data graymap/0,1,3,2/

   nargs=iargc()
   if(nargs.ne.6) then
      print*,'Usage: ldpcsim  niter ndeep #trials  s    modtype  channel `'
      print*,'eg:    ldpcsim    25    2    1000   0.69    1         1'
      print*,'niter   : maximum number of decoder iterations.'
      print*,'ndeep   : decode depth, -1 through 4 are valid, -1 is BP only )'
      print*,'s       : if negative, then value is ignored and sigma is calculated from SNR.'
      print*,'modtype : 0 coherent BPSK, 1 4FSK'
      print*,'channel : 0 AWGN, 1 Rayleigh (4FSK only)'
      return
   endif
   call getarg(1,arg)
   read(arg,*) max_iterations
   call getarg(2,arg)
   read(arg,*) ndeep 
   if((ndeep.lt.-1) .or. (ndeep.gt.4)) then
      print*,'invalid ndeep value: -1 through 4 are valid, -1 is BP only )'
      return
   endif
   call getarg(3,arg)
   read(arg,*) ntrials
   call getarg(4,arg)
   read(arg,*) s
   call getarg(5,arg)
   read(arg,*) modtype
   call getarg(6,arg)
   read(arg,*) channeltype
   if((channeltype.ne.0) .and. (channeltype.ne.1)) then
      print*,'invalid channeltype value: 0 (AWGN) or 1 (Rayleigh) are valid'
      return
   endif

   if(modtype .eq. 0 .and. channeltype .eq. 1) then
      channeltype=0
      print*,"Warning: Rayleigh channel is not available for BPSK simulation - channel set to AWGN."
   endif

   rate=real(K)/real(N)

   write(*,*) "rate: ",rate
   write(*,*) "niter= ",max_iterations," ndeep= ",ndeep," s= ",s
   if(modtype.eq.0) then
      iq=1    ! bits per symbol
      write(*,*) "coherent BPSK"
   else
      iq=2    ! bits per symbol
      write(*,*) "noncoherent 4FSK"
   endif
   if(channeltype.eq.0) then
      write(*,*) "AWGN channel"
   else
      write(*,*) "Rayleigh fading"
   endif

   textmessage="CQ K9AN CQ"
   call pack_jtty(textmessage,c32,nframes)
   read(c32(1),'(32i1)') message32(1:32)

   write(*,*) '32 bit message'
   write(*,'(32i1)') message32

   call encode_80_32(message32,codeword)

   write(*,*) 'codeword'
   write(*,'(80i1)') codeword

   one=.false.
   do i=0,3
      do j=0,1
         if(iand(i,2**j).ne.0) one(i,j)=.true.
      enddo
   enddo

   if(modtype .eq. 1 ) then
      do i=1,40
         is=codeword(2*i) + 2*codeword(2*i-1)
         itone(i) = graymap(is)
      enddo
      write(*,*) 'channel symbols'
      write(*,'(40i1)') itone
   endif

!  call init_random_seed()

!   write(*,*) "Eb/N0  SNR2500   ngood  nundetected  sigma    psymerr      pbiterr"
   write(*,*) "Es/N0   Eb/N0  SNR2500   ngood  nundetected  sigma    psymerr      pbiterr"
   do idb = 40,-4,-1
      esn0db=idb/2.0
      sigma=1/sqrt( 2*(10**(esn0db/10.0)) )  ! dB is Es/N0
      ngood=0
      nue=0
      nbiterr=0
      nsymerr=0

      do itrial=1, ntrials
         if(modtype .eq. 0) then ! BPSK
!           call sgran()
! Create a realization of a noisy received word
            do i=1,N
               rxdata(i) = 2.0*codeword(i)-1.0 + sigma*gran()
            enddo

            nerr=count(rxdata*(2*codeword-1.0) .lt. 0)
            nsymerr=nsymerr+nerr

            rx2=sum(rxdata*rxdata)/N
            rxdata=rxdata/sqrt(rx2)

! The s parameter can be tuned to trade a few tenth's dB of threshold for an order of
! magnitude in UER
            if( s .lt. 0 ) then
               ss=sigma
            else
               ss=s
            endif

            llr=2.0*rxdata/(ss*ss)
         else

! noncoherent MFSK
            do i = 1, 40

               A = 1.0
               do j = 0, 3
                  if(j.eq.itone(i)) then
                     if(channeltype.eq.0) then
                        A = 1.0
                     elseif(channeltype.eq.1) then  ! symbol-to-symbol Rayleigh fading
                        xi=gran()**2+gran()**2
                        A=sqrt(xi/2)
                     endif
                     cs(j) = A + sigma*gran() + cmplx(0,1)*sigma*gran()
                  elseif(j.ne.itone(i)) then
                     cs(j) =     sigma*gran() + cmplx(0,1)*sigma*gran()
                  endif
               enddo

               lmax=maxloc(abs(cs(:)))
               if((lmax(1)-1).ne.itone(i)) nsymerr=nsymerr+1

               do j = 0, 3
                  s2(j) = abs(cs(graymap(j)))
               enddo

               do ib=0,1
                  bm=maxval(s2(0:3),one(0:3,1-ib)) - &
                     maxval(s2(0:3),.not.one(0:3,1-ib))
                  bitmetrics(2*i-1+ib)=bm
               enddo

            enddo

            if( s .lt. 0 ) then
               ss=sigma
            else
               ss=s
            endif

            xn=sqrt(sum(bitmetrics*bitmetrics)/N)
            llr = 2*(bitmetrics/xn)/(ss*ss)
            nerr=count(llr*(2*codeword-1.0) .lt. 0)
         endif

         nbiterr=nbiterr+nerr

! max_iterations is max number of belief propagation iterations
         call bpdecode_80_32(llr, max_iterations, message32, cw, nharderrors)
         if(ndeep.ge.0 .and. nharderrors.lt.0) then 
            call osd80_32(llr, ndeep, message32, cw, nharderrors, dmin)
         endif

! If the decoder finds a valid codeword, nharderrors will be .ge. 0.
         if( nharderrors .ge. 0 ) then
            nhw=count(cw.ne.codeword)
            if(nhw.eq.0) then ! this is a good decode
               ngood=ngood+1
            else              ! this is an undetected error
               nue=nue+1
            endif
         endif
      enddo

      symrate = 31.25  ! baud
      snr2500=esn0db + 10*log10(symrate/2500.0) ! ref BW is 2500 Hz.
      pberr=real(nbiterr)/real(ntrials*N)
      if(modtype.eq.0) then  ! BPSK
         pserr=real(nsymerr)/(real(ntrials*80))
         ebn0db = esn0db - 10*log10(rate)
      else
         pserr=real(nsymerr)/(real(ntrials*40))
         ebn0db = esn0db - 10*log10(2*rate)
      endif
      write(*,"(f4.1,4x,f4.1,4x,f5.1,1x,i8,1x,i8,7x,f5.2,3x,e10.3,3x,e10.3)") esn0db,ebn0db,snr2500,ngood,nue,ss,pserr,pberr

   enddo

end program ldpcsim80_42
