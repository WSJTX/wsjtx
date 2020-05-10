program ldpcsim174_91
! End to end test of the (174,91)/crc14 encoder and decoder.
   use packjt77

   integer, parameter:: N=174, K=91, M=N-K
   character*37 msg,msgsent,msgreceived
   character*77 c77
   character*8 arg
   character*6 grid
   character*96 tmpchar
   integer*1, allocatable ::  codeword(:), decoded(:), message(:)
   integer*1 msgbits(77)
   integer*1 message77(77),message91(91)
   integer*1 apmask(N), cw(N)
   integer nerrtot(0:N),nerrdec(0:N)
   logical unpk77_success
   real*8, allocatable ::  rxdata(:)
   real, allocatable :: llr(:)

   nerrtot=0
   nerrdec=0

   nargs=iargc()
   if(nargs.ne.6) then
      print*,'Usage: ldpcsim  niter  ndepth  #trials   s     Keff  nbposd'
      print*,'eg:    ldpcsim    10     2      1000    0.84    91     1'
      print*,'niter:  max BP iterations'
      print*,'ndepth: OSD order'
      print*,'s: noise sigma; if negative value is ignored and sigma is calculated from SNR.'
      print*,'nbposd=0, no coupling. nbposd>0, maxsuper=nbposd; nbposd<0, no OSD'
      return
   endif
   call getarg(1,arg)
   read(arg,*) max_iterations
   call getarg(2,arg)
   read(arg,*) ndepth
   call getarg(3,arg)
   read(arg,*) ntrials
   call getarg(4,arg)
   read(arg,*) s
   call getarg(5,arg)
   read(arg,*) Keff
   call getarg(6,arg)
   read(arg,*) nbposd

!  scale Eb/No for a (174,91) code
   rate=real(K)/real(N)

   write(*,*) "rate: ",rate
   write(*,*) "niter= ",max_iterations," s= ",s

   allocate ( codeword(N), decoded(K), message(K) )
   allocate ( rxdata(N), llr(N) )

   msg="K9ABC K1ABC FN20"
   i3=0
   n3=1
   call pack77(msg,i3,n3,c77) !Pack into 12 6-bit bytes
   call unpack77(c77,1,msgsent,unpk77_success) !Unpack to get msgsent
   write(*,*) "message sent ",msgsent

   read(c77,'(77i1)') msgbits(1:77)
   write(*,*) 'message'
   write(*,'(a71,1x,a3,1x,a3)') c77(1:71),c77(72:74),c77(75:77)

   call init_random_seed()

   call encode174_91(msgbits,codeword)
   write(*,*) 'crc14'
   write(*,'(14i1)') codeword(78:91)
   write(*,*) 'codeword'
   write(*,'(22(8i1,1x))') codeword

   write(*,*) "Eb/N0   Es/N0    ngood  nundetected   sigma         psymerr"
   do idb = 10,-4,-1
      db=idb/2.0-1.0
      sigma=1/sqrt( 2*rate*(10**(db/10.0)) )
      ngood=0
      nue=0
      nsumerr=0

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

         rxav=sum(rxdata)/N
         rx2av=sum(rxdata*rxdata)/N
         rxsig=sqrt(rx2av-rxav*rxav)
         rxdata=rxdata/rxsig
         if( s .lt. 0 ) then
            ss=sigma
         else
            ss=s
         endif

         llr=2.0*rxdata/(ss*ss)
         nap=0 ! number of AP bits
         llr(1:nap)=5*(2.0*msgbits(1:nap)-1.0)
         apmask=0
         apmask(1:nap)=1
         call decode174_91(llr,Keff,nbposd,ndepth,apmask,message91,cw,ntype,nharderrors,dmin)
! If the decoder finds a valid codeword, nharderrors will be .ge. 0.
         if( nharderrors.ge.0 ) then
            nhw=count(cw.ne.codeword)
            if(nhw.eq.0) then ! this is a good decode
               ngood=ngood+1
               nerrdec(nerr)=nerrdec(nerr)+1
            else
               nue=nue+1
            endif
         endif
         nsumerr=nsumerr+nerr
      enddo

      esn0=db+10.0*log10(rate)
      pberr=real(nsumerr)/(real(ntrials*N))
      write(*,"(f4.1,4x,f5.1,1x,i8,1x,i8,8x,f5.2,8x,e10.3)") db,esn0,ngood,nue,ss,pberr

   enddo

   open(unit=23,file='nerrhisto.dat',status='unknown')
   do i=1,174
      write(23,'(i4,2x,i10,i10,f10.2)') i,nerrdec(i),nerrtot(i),real(nerrdec(i))/real(nerrtot(i)+1e-10)
   enddo
   close(23)

end program ldpcsim174_91
