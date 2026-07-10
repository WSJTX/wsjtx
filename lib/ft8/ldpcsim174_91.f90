program ldpcsim174_91
! End to end test of the (174,91)/crc14 encoder and decoder.
   use packjt77

   integer, parameter:: N=174, K=91, M=N-K, NN=58
   character*37 msg,msgsent
   character*77 c77
   character*8 arg
   integer*1, allocatable ::  codeword(:), decoded(:), message(:)
   integer*1 msgbits(77)
   integer*1 message91(91)
   integer*1 apmask(N), cw(N)
   integer lmax(1)
   integer modtype, itone(79), itonecw(58), graymap(0:7)
   integer nerrtot(0:N),nerrdec(0:N)
   integer channeltype
   logical unpk77_success
   logical one(0:511,0:8)
   real*8, allocatable ::  rxdata(:)
   real     llr(174),llra(174),llrb(174),llrc(174),llrd(174)
   real     bmeta(174),bmetb(174),bmetc(174),bmetd(174)
   complex cs(0:7,NN)
   real s2(0:511)
   data graymap/0,1,3,2,5,6,4,7/

   nerrtot=0
   nerrdec=0

!

   nargs=iargc()
   if(nargs.ne.7) then
      print*,'Usage: ldpcsim  maxosd  norder  #trials   s     Keff  modtype  channel'
      print*,'eg:    ldpcsim    10      2      1000    0.84    91     1         0'
      print*,'  maxosd<0: do bp only'
      print*,'  maxosd=0: do bp and then call osd once with channel llrs'
      print*,'  maxosd>1: do bp and then call osd maxosd times with saved bp outputs'
      print*,'  norder  : osd decoding depth'
      print*,'  s       : BPSK only, noise sigma, if s<0 value is ignored and sigma is calculated from SNR.'
      print*,'  Keff    : Keff must be in the range [77,91]; Keff-77 is the # of bits to use as CRC.'
      print*,'  modtype : 0, coherent BPSK; 1 noncoherent 8FSK'
      print*,'  channel : 0, AWGN; 1, block Rayleigh (Rayleigh only works with 8FSK!)'
      return
   endif
   call getarg(1,arg)
   read(arg,*) maxosd
   call getarg(2,arg)
   read(arg,*) norder 
   call getarg(3,arg)
   read(arg,*) ntrials
   call getarg(4,arg)
   read(arg,*) s
   call getarg(5,arg)
   read(arg,*) Keff
   call getarg(6,arg)
   read(arg,*) modtype
   call getarg(7,arg)
   read(arg,*) channeltype

!  scale Eb/No for a (174,91) code
   rate=real(K)/real(N)

   write(*,*) "rate: ",rate

   allocate ( codeword(N), decoded(K), message(K) )
   allocate ( rxdata(N) )

   one=.false.
   do i=0,511
      do j=0,8
         if(iand(i,2**j).ne.0) one(i,j)=.true.
      enddo
   enddo

   msg="K9ABC K1ABC FN20"
   i3=0
   n3=1
   call pack77(msg,i3,n3,c77)
   call unpack77(c77,1,msgsent,unpk77_success) !Unpack to get msgsent
   write(*,*) "message sent ",msgsent

   read(c77,'(77i1)') msgbits(1:77)
   write(*,*) 'message'
   write(*,'(a71,1x,a3,1x,a3)') c77(1:71),c77(72:74),c77(75:77)

   call init_random_seed()

   iq=1
   if(modtype.gt.0) then ! MFSK - get tones
      i3=-1
      n3=-1
      call genft8(msg,i3,n3,msgsent,msgbits,itone)
      write(*,*) 'message tones'
      write(*,'(79(i1,1x))') itone
      itonecw(1:29)=itone(8:36)
      itonecw(30:58)=itone(44:72)
      iq=3 ! bits per symbol    
   endif
   call encode174_91(msgbits,codeword)
   write(*,*) 'crc14'
   write(*,'(14i1)') codeword(78:91)
   write(*,*) 'codeword'
   write(*,'(22(8i1,1x))') codeword
   write(*,*) 'Eb/N0   Es/N0   SNR2500   ngood  nundetected   sigma         psymerr            pbiterr'
   do idb = 20,-4,-1
      nsymerr=0
      nbiterr=0
      db=idb/2.0-1.0
      sigma=1/sqrt( 2*rate*iq*(10**(db/10.0)) )
      ngood=0
      nue=0
      nsumerr=0
      do itrial=1, ntrials
! Create a realization of a noisy received word
         if(modtype.eq.0) then
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
         else
! noncoherent MFSK
            do i=1,58
               do j=0,7
                  if(j.eq.itonecw(i)) then
                     if(channeltype.eq.0) then
                        A=1.0
                     elseif(channeltype.eq.1) then
                        xI=gran()**2 + gran()**2
                        A=sqrt(xI/2)
                     endif
                     cs(j,i)= A + sigma*gran() + cmplx(0,1)*sigma*gran()
                  elseif(j.ne.itonecw(i)) then
                     cs(j,i)=      sigma*gran() + cmplx(0,1)*sigma*gran()
                  endif
               enddo
               lmax=maxloc(abs(cs(:,i)))
               if(lmax(1)-1.ne.itonecw(i) ) nsymerr=nsymerr+1
            enddo
            do nsym=1,3
               nt=2**(3*nsym)
                  do ks=1,58,nsym
                     amax=-1.0
                     do i=0,nt-1
                        i1=i/64
                        i2=iand(i,63)/8
                        i3=iand(i,7)
                        if(nsym.eq.1) then
                           s2(i)=abs(cs(graymap(i3),ks))
                        elseif(nsym.eq.2) then
                           s2(i)=abs(cs(graymap(i2),ks)+cs(graymap(i3),ks+1))
                        elseif(nsym.eq.3) then
                           if(ks.ne.58) then
                              s2(i)=abs(cs(graymap(i1),ks)+cs(graymap(i2),ks+1)+cs(graymap(i3),ks+2))
                           else
                              s2(i)=abs(cs(graymap(i1),ks))
                           endif
                        else
                           print*,"Error - nsym must be 1, 2, or 3."
                        endif
                     enddo

                     i32=1+(ks-1)*3
                     if(nsym.eq.1) ibmax=2
                     if(nsym.eq.2) ibmax=5
                     if(nsym.eq.3) ibmax=8
                     do ib=0,ibmax
                        bm=maxval(s2(0:nt-1),one(0:nt-1,ibmax-ib)) - &
                           maxval(s2(0:nt-1),.not.one(0:nt-1,ibmax-ib))
                        if(i32+ib .gt.174) cycle
                        if(nsym.eq.1) then
                           bmeta(i32+ib)=bm
                           den=max(maxval(s2(0:nt-1),one(0:nt-1,ibmax-ib)), &
                              maxval(s2(0:nt-1),.not.one(0:nt-1,ibmax-ib)))
                           if(den.gt.0.0) then
                              cm=bm/den
                           else ! erase it
                              cm=0.0
                           endif
                           bmetd(i32+ib)=cm
                        elseif(nsym.eq.2) then
                           bmetb(i32+ib)=bm
                        elseif(nsym.eq.3) then
                           bmetc(i32+ib)=bm
                        endif
                     enddo
                  enddo
            enddo
            call normalizebmet(bmeta,174)
            call normalizebmet(bmetb,174)
            call normalizebmet(bmetc,174)
            call normalizebmet(bmetd,174)

            scalefac=2.83  
            llra=scalefac*bmeta
            llrb=scalefac*bmetb
            llrc=scalefac*bmetc
            llrd=scalefac*bmetd

            llr=llrc 
         endif

         do i=1, 174 
           if(llr(i)*(codeword(i)-0.5).lt.0) nbiterr=nbiterr+1
         enddo

         nap=0 ! number of AP bits
         llr(1:nap)=5*(2.0*msgbits(1:nap)-1.0)
         apmask=0
         apmask(1:nap)=1
         call decode174_91(llr,Keff,maxosd,norder,apmask,message91,cw,ntype,nharderrors,dmin)
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

      esn0=db+10.0*log10(iq*rate) ! iq=3 bits per symbol for 8FSK
      snr2500=esn0-10.0*log10(2500/6.25)
!      pberr=real(nsumerr)/(real(ntrials*N))
      psymerr=real(nsymerr)/(ntrials*174.0/iq)
      pbiterr=real(nbiterr)/(ntrials*174.0)
      write(*,"(f4.1,4x,f5.1,4x,f5.1,1x,i8,1x,i8,8x,f5.2,8x,e10.3,8x,e10.3)") db,esn0,snr2500,ngood,nue,ss,psymerr,pbiterr

   enddo

   open(unit=23,file='nerrhisto.dat',status='unknown')
   do i=1,174
      write(23,'(i4,2x,i10,i10,f10.2)') i,nerrdec(i),nerrtot(i),real(nerrdec(i))/real(nerrtot(i)+1e-10)
   enddo
   close(23)

end program ldpcsim174_91

subroutine normalizebmet(bmet,n)
   real bmet(n)

   bmetav=sum(bmet)/real(n)
   bmet2av=sum(bmet*bmet)/real(n)
   var=bmet2av-bmetav*bmetav
   if( var .gt. 0.0 ) then
      bmetsig=sqrt(var)
   else
      bmetsig=sqrt(bmet2av)
   endif
   bmet=bmet/bmetsig
   return
end subroutine normalizebmet
