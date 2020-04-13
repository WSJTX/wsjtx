program ldpcsim174_74

! End-to-end test of the (174,74)/crc24 encoder and decoders.

   use crc
   use packjt77

   parameter(N=174, K=74, M=N-K)
   character*8 arg
   character*37 msg0,msg
   character*77 c77
   character*50 cmsg
   character*24 c24
   integer*1 msgbits(74)
   integer*1 apmask(174)
   integer*1 cw(174)
   integer*1 codeword(N),message(50)
   integer ncrc24
   integer nerrtot(174),nerrdec(174),nmpcbad(74)
   real rxdata(N),llr(N)
   real dllr(174),llrd(174)
   logical first,unpk77_success
   data first/.true./

   nerrtot=0
   nerrdec=0
   nmpcbad=0  ! Used to collect the number of errors in the message+crc part of the codeword

   nargs=iargc()
   if(nargs.ne.5 .and. nargs.ne.6) then
      print*,'Usage: ldpcsim        niter ndeep  #trials    s    K      [msg]'
      print*,'e.g.   ldpcsim174_74   20     5     1000    0.85  64 "K9AN EN50 37"'
      print*,'s    : if negative, then value is ignored and sigma is calculated from SNR.'
      print*,'niter: is the number of BP iterations.'
      print*,'ndeep: -1 is BP only, ndeep>=0 is OSD order'
      print*,'K    :is the number of message+CRC bits and must be in the range [50,74]'
      print*,'WSPR-format message is optional'
      return
   endif
   call getarg(1,arg)
   read(arg,*) max_iterations
   call getarg(2,arg)
   read(arg,*) ndeep
   call getarg(3,arg)
   read(arg,*) ntrials
   call getarg(4,arg)
   read(arg,*) s
   call getarg(5,arg)
   read(arg,*) Keff
   msg0='K9AN EN50 37                         '
   if(nargs.eq.6) call getarg(6,msg0)
   call pack77(msg0,i3,n3,c77)
   cmsg=c77(1:50)

   rate=real(Keff)/real(N)

   write(*,*) "code rate: ",rate
   write(*,*) "niter    : ",max_iterations
   write(*,*) "ndeep    : ",ndeep
   write(*,*) "s        : ",s
   write(*,*) "K        : ",Keff

   msgbits=0
   read(cmsg,'(50i1)') msgbits(1:50)
   write(*,*) 'message'
   write(*,'(74i1)') msgbits

   call get_crc24(msgbits,ncrc24)
   write(c24,'(b24.24)') ncrc24
   read(c24,'(24i1)') msgbits(51:74)
   call encode174_74(msgbits,codeword)
   call init_random_seed()
   call sgran()

   write(*,*) 'codeword'
   write(*,'(50i1,1x,24i1,1x,100i1)') codeword

   write(*,*) "Eb/N0    Es/N0   ngood  nundetected   sigma   symbol error rate"
   do idb = 8,-3,-1
      db=idb/2.0-1.0
      sigma=1/sqrt( 2*rate*(10**(db/10.0)) )  ! to make db represent Eb/No
!  sigma=1/sqrt( 2*(10**(db/10.0)) )        ! db represents Es/No
      ngood=0
      nue=0
      nberr=0
      do itrial=1, ntrials
! Create a realization of a noisy received word
         do i=1,N
            rxdata(i) = 2.0*codeword(i)-1.0 + sigma*gran()
         enddo
         nerr=0
         do i=1,N
            if( rxdata(i)*(2*codeword(i)-1.0) .lt. 0 ) nerr=nerr+1
         enddo
         if(nerr.ge.1) nerrtot(nerr)=nerrtot(nerr)+1
         nberr=nberr+nerr

         rxav=sum(rxdata)/N
         rx2av=sum(rxdata*rxdata)/N
         rxsig=sqrt(rx2av-rxav*rxav)
         rxdata=rxdata/rxsig
! To match the metric to the channel, s should be set to the noise standard deviation.
! For now, set s to the value that optimizes decode probability near threshold.
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
         call bpdecode174_74(llr,apmask,max_iterations,message,cw,nharderror,niterations)
         dmin=0.0
         if( (nharderror .lt. 0) .and. (ndeep .ge. 0) ) then
            call osd174_74(llr, Keff, apmask, ndeep, message, cw, nharderror, dmin)
         endif

         if(nharderror.ge.0) then
            n2err=0
            do i=1,N
               if( cw(i)*(2*codeword(i)-1.0) .lt. 0 ) n2err=n2err+1
            enddo
            if(n2err.eq.0) then
               ngood=ngood+1
            else
               nue=nue+1
            endif
         endif
      enddo
!      snr2500=db+10*log10(200.0/116.0/2500.0)
      esn0=db+10*log10(rate)
      pberr=real(nberr)/(real(ntrials*N))
      write(*,"(f4.1,4x,f5.1,1x,i8,1x,i8,8x,f5.2,8x,e10.3)") db,esn0,ngood,nue,ss,pberr

      if(first) then
         write(c77,'(50i1)') message
         c77(51:77)='000000000000000000000110000'
         call unpack77(c77,0,msg,unpk77_success)
         if(unpk77_success) then
            write(*,1100) msg(1:14)
1100        format('Decoded message: ',a14)
         else
            print*,'Error unpacking message'
         endif
         first=.false.
      endif
   enddo

   open(unit=23,file='nerrhisto.dat',status='unknown')
   do i=1,120
      write(23,'(i4,2x,i10,i10,f10.2)') i,nerrdec(i),nerrtot(i),real(nerrdec(i))/real(nerrtot(i)+1e-10)
   enddo
   close(23)
   open(unit=25,file='nmpcbad.dat',status='unknown')
   do i=1,68
      write(25,'(i4,2x,i10)') i,nmpcbad(i)
   enddo
   close(25)

end program ldpcsim174_74
