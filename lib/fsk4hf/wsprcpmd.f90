program wsprcpmd

! Decode WSPRCPM data read from *.c2 or *.wav files.

! WSPRCPM is a WSPR-like mode based on full-response CPM.
!
! Currently configured to use (204,68) r=1/3 LDPC code, regular column weight 3.
!   50 data bits + 14 bit CRC + 4 "0" bits. The 4 "0" bits are unused bits that
!   are not transmitted. At the decoder, these bits are treated as "AP" bits.
!   This shortens the code to (200,64) r=0.32, slightly decreasing the code rate.
!
! Frame format is:
! d100 p32 d100 (232)  channel symbols
!
   use crc
   include 'wsprcpm_params.f90'
   parameter(NMAX=120*12000)
   character arg*8,message*22,cbits*50,infile*80,fname*16,datetime*11
   character ch1*1,ch4*4,cseq*31
   character*22 decodes(100)
   character*120 data_dir
   character*32 uwbits
   character*68 dmsg
   complex c2(0:120*12000/32-1)              !Complex waveform
   complex cframe(0:216*200-1)               !Complex waveform
   complex cd(0:216*10-1)                   !Complex waveform
   complex c1(0:9,0:1),c0(0:9,0:1)
   complex ccor(0:1,216)
   complex cp(0:1,0:1)
   complex csum,cterm
   real*8 fMHz
   real rxdata(ND),llr(204)               !Soft symbols
   real sbits(216),sbits1(216),sbits3(216)
   real ps(0:8191),psbest(0:8191)
   real candidates(100,2)
   integer iuniqueword0
   integer isync(200)                     !Unique word
   integer isync2(216)
!   integer ipreamble(16)                 !Preamble vector
   integer isyncword(16)
   integer ihdr(11)
   integer*2 iwave(NMAX)                 !Generated full-length waveform
   integer*1,target ::  idat(9)
   integer*1 decoded(68),apmask(204),cw(204)
   integer*1 hbits(216),hbits1(216),hbits3(216)
!   data ipreamble/1,1,1,1,0,0,0,0,0,0,0,0,1,1,1,1/
   data isyncword/0,1,3,2,1,0,2,3,2,3,1,0,3,2,0,1/
   data cseq /'9D9F C48B 797A DD60 58CB 2EBC 6'/
   data iuniqueword0/z'30C9E8AD'/

   k=0
   do i=1,31
      ch1=cseq(i:i)
      if(ch1.eq.' ') cycle
      read(ch1,'(z1)') n
      write(ch4,'(b4.4)') n
      do j=1,4
         k=k+1
         isync(k)=0
         if(ch4(j:j).eq.'1') isync(k)=1
      enddo
   enddo
   isync(101:200)=isync(1:100)

   fs=12000.0/NDOWN                       !Sample rate
   dt=1.0/fs                              !Sample interval (s)
   tt=NSPS*dt                             !Duration of "itone" symbols (s)
   baud=1.0/tt                            !Keying rate for "itone" symbols (baud)
   txt=NZ*dt                              !Transmission length (s)
   h=0.50                                !h=0.8 seems to be optimum for AWGN sensitivity (not for fading)
   twopi=8.0*atan(1.0)
   pi=4.0*atan(1.0)

   nargs=iargc()
   if(nargs.lt.1) then
      print*,'Usage:   wsprcpmd [-a <data_dir>] [-f fMHz] [-c ncoh] [-h h] file1 [file2 ...]'
      go to 999
   endif
   iarg=1
   data_dir="."
   call getarg(iarg,arg)
   if(arg(1:2).eq.'-a') then
      call getarg(iarg+1,data_dir)
      iarg=iarg+2
   endif
   call getarg(iarg,arg)
   if(arg(1:2).eq.'-f') then
      call getarg(iarg+1,arg)
      read(arg,*) fMHz
      iarg=iarg+2
   endif
   ncoh=1
   if(arg(1:2).eq.'-h') then
      call getarg(iarg+1,arg)
      read(arg,*) h
      iarg=iarg+2
   endif

   isync2(1:100)=isync(1:100)
   isync2(101:116)=(/0,1,1,0,1,0,0,1,0,1,1,0,1,0,0,1/)
   isync2(117:216)=isync(101:200)

! data MSB
! data sync tone
!   0    0   0
!   0    1   1
!   1    0   2
!   1    1   3

   dphi=twopi*baud*(h/2.0)*dt*20  ! dt*10 is samp interval after downsample
   do j=0,1
      if(j.eq.0) then
         dphi0=-3*dphi
         dphi1=+1*dphi
      else
         dphi0=-1*dphi
         dphi1=+3*dphi
      endif
      phi0=0.0
      phi1=0.0
      do i=0,9
         c1(i,j)=cmplx(cos(phi1),sin(phi1))
         c0(i,j)=cmplx(cos(phi0),sin(phi0))
         phi1=mod(phi1+dphi1,twopi)
         phi0=mod(phi0+dphi0,twopi)
      enddo
      cp(1,j)=cmplx(cos(phi1),sin(phi1))
      cp(0,j)=cmplx(cos(phi0),sin(phi0))
   enddo

   open(13,file=trim(data_dir)//'/ALL_WSPR.TXT',status='unknown',   &
      position='append')

   xs1=0.0
   xs2=0.0
   fr1=0.0
   fr2=0.0
   nav=0
   ngood=0

   do ifile=iarg,nargs
      call getarg(ifile,infile)
      open(10,file=infile,status='old',access='stream')
      j1=index(infile,'.c2')
      j2=index(infile,'.wav')
      if(j1.gt.0) then
         read(10,end=999) fname,ntrmin,fMHz,c2
         read(fname(8:11),*) nutc
         write(datetime,'(i11)') nutc
      else if(j2.gt.0) then
         read(10,end=999) ihdr,iwave
         read(infile(j2-4:j2-1),*) nutc
         datetime=infile(j2-11:j2-1)
         call wsprcpm_downsample(iwave,c2)
      else
         print*,'Wrong file format?'
         go to 999
      endif
      close(10)

      fa=-100.0
      fb=100.0
      fs=12000.0/32.0
      npts=120*12000.0/32.0
      nsync=16
      call getcandidate2(c2,npts,fs,fa,fb,ncand,candidates)         !First approx for freq
      ndecodes=0
      do icand=1,ncand
         fc0=candidates(icand,1)
         xsnr=candidates(icand,2)
         xmax=-1e32
         do i=-7,7
            ft=fc0+i*0.2
            call noncoherent_frame_sync(c2,h,ft,isync2,is,xf1)
            if(xf1.gt.xmax) then
               xmax=xf1
               fc1=ft
               is0=is
            endif
         enddo
         fcest=fc1
         imode=0 ! refine freq
         call coherent_sync(c2,h,isyncword,nsync,NSPS,is0,fcest,imode,xp0)
         imode=1 ! refine istart
         istart=is0
         call coherent_sync(c2,h,isyncword,nsync,NSPS,istart,fcest,imode,xp1)
         write(*,'(i5,i5,i5,6(f11.5,2x))') ifile-2,is0,istart,fc0,fc1,fcest,xf1,xp0,xp1

!genie sync
!istart=375
!fcest=0.0
         do ijitter=0,4
            io=-10*(ijitter/2+1)
            if(mod(ijitter,2).eq.0) io=10*(ijitter/2)
            ib=max(0,istart+io)
            cframe=c2(ib:ib+216*200-1)
            call downsample2(cframe,fcest,h,cd)

            s2=sum(cd*conjg(cd))/(10*216)
            cd=cd/sqrt(s2)

            do nseq=1,5
               if( nseq.eq.1 ) then  ! noncoherent single-symbol detection
                  sbits1=0.0
                  do ibit=1,216
                     j=isync2(ibit)
                     ib=(ibit-1)*10
                     ccor(1,ibit)=sum(cd(ib:ib+9)*conjg(c1(0:9,j)))
                     ccor(0,ibit)=sum(cd(ib:ib+9)*conjg(c0(0:9,j)))
                     sbits1(ibit)=abs(ccor(1,ibit))-abs(ccor(0,ibit))
                     hbits1(ibit)=0
                     if(sbits1(ibit).gt.0) hbits1(ibit)=1
                  enddo
                  sbits=sbits1
                  hbits=hbits1
                  sbits3=sbits1
                  hbits3=hbits1
               elseif( nseq.ge.2 ) then
                  ps=0
                  if( nseq.eq. 2 ) nbit=3
                  if( nseq.eq. 3 ) nbit=5
                  if( nseq.eq. 4 ) nbit=7
                  if( nseq.eq. 5 ) nbit=9
                  if( nseq.eq. 6 ) nbit=11
                  if( nseq.eq. 7 ) nbit=13
                  numseq=2**(nbit)
                  do ibit=nbit/2+1,216-nbit/2
                     ps=0.0
                     pmax=0.0
                     do iseq=0,numseq-1
                        csum=0.0
                        cterm=1.0
                        k=1
                        do i=nbit-1,0,-1
                           j=isync2(ibit-(nbit/2+1)+k)
                           ibb=iand(iseq/(2**i),1)
                           csum=csum+ccor(ibb,ibit-(nbit/2+1)+k)*cterm
                           cterm=cterm*conjg(cp(ibb,j))
                           k=k+1
                        enddo
                        ps(iseq)=abs(csum)
                        if( ps(iseq) .gt. pmax ) then
                           pmax=ps(iseq)
                           ibflag=1
                        endif
                     enddo
                     if( ibflag .eq. 1 ) then
                        psbest=ps
                        ibflag=0
                     endif
                     call getmetric2(2**(nbit/2),psbest,numseq,sbits3(ibit))
                     hbits3(ibit)=0
                     if(sbits3(ibit).gt.0) hbits3(ibit)=1
                  enddo
                  sbits=sbits3
                  hbits=hbits3
               endif

               rxdata(1:100)=sbits(1:100)
               rxdata(101:200)=sbits(117:216);
               rxav=sum(rxdata(1:200))/200.0
               rx2av=sum(rxdata(1:200)*rxdata(1:200))/200.0
               rxsig=sqrt(rx2av-rxav*rxav)
               rxdata=rxdata/rxsig
               sigma=0.90
               llr(201:204)=-5.0
               llr(1:200)=2*rxdata/(sigma*sigma)
               apmask=0
               apmask(201:204)=1
               max_iterations=40
               ifer=0
               call bpdecode204(llr,apmask,max_iterations,decoded,cw,nharderror,niterations)
               nhardmin=-1
               if(nharderror.lt.0) call osd204(llr,apmask,4,decoded,cw,nhardmin,dmin)
               if(sum(decoded).eq.0) cycle
               if(nhardmin.ge.0 .or. nharderror.ge.0) then
                  idat=0
                  write(dmsg,'(68i1)') decoded
                  read(dmsg(1:50),'(6b8.8,b2.2)') idat(1:7)
                  idat(7)=idat(7)*64
                  read(dmsg(51:64),'(b14.14)') ndec_crc
                  ncalc_crc=iand(crc14(c_loc(idat),9),z'FFFF')
                  nbadcrc=1
                  if(ncalc_crc .eq. ndec_crc) nbadcrc=0
               else
                  cycle
               endif
               if( nbadcrc.eq.0 ) then
                  write(cbits,1200) decoded(1:50)
1200              format(50i1)
                  read(cbits,1202) idat
1202              format(8b8,b4)
                  idat(7)=ishft(idat(7),6)
                  call wqdecode(idat,message,itype)
                  idupe=0
                  do i=1,ndecodes
                     if(decodes(i).eq.message) idupe=1
                  enddo
                  if(idupe.eq.1) goto 888
                  ndecodes=ndecodes+1
                  decodes(ndecodes)=message
                  nsnr=nint(xsnr)
                  freq=fMHz + 1.d-6*(fc1+fbest)
                  nfdot=0
                  write(13,1210) datetime,0,nsnr,xdt,freq,message,nfdot
1210              format(a11,2i4,f6.2,f12.7,2x,a22,i3)
                  write(*,1212) datetime(8:11),nsnr,xdt,freq,nfdot,message,'*',nseq,ijitter,nharderror,nhardmin
1212              format(a4,i4,f5.1,f11.6,i3,2x,a22,a1,i5,i5,i5,i5)
                  goto 888
               endif
            enddo ! nseq
         enddo !jitter
888      continue
      enddo !candidate list
   enddo !files

   write(*,1120)
1120 format("<DecodeFinished>")

999 end program wsprcpmd


subroutine coherent_sync(c2,h,isyncword,nsync,nsps,istart,fc,imode,xmax)
! imode=0: refine fc using given istart
! imode=1: refine istart using given fc
   complex c2(0:120*12000/32-1)
   complex csync(0:16*200-1)
   complex ctmp1(0:4*16*200-1)
   complex ctwkp(0:16*200-1)
   complex ccohp(0:15)
   integer isyncword(nsync)
   logical first/.true./
   save dt,first,twopi,csync

   if(first) then
      baud=12000.0/6400.0
      dt=32.0/12000.0
      twopi=8.0*atan(1.0)
      k=0
      phi=0.0
      dphi=twopi*baud*0.5*h*dt
      do i=1,16
         dp=dphi*2*(isyncword(i)-1.5)
         do j=1,200
            csync(k)=cmplx(cos(phi),sin(phi))
            phi=mod(phi+dp,twopi)
            k=k+1
         enddo
      enddo
      first=.false.
   endif
   dphi=twopi*fc*dt
   ctwkp=cmplx(0.0,0.0)
   phi=0
   do i=0,nsync*nsps-1
      ctwkp(i)=csync(i)*cmplx(cos(phi),sin(phi))
      phi=mod(phi+dphi,twopi)
   enddo
   ipstart=istart+100*200
   ctmp1=0.0
   xmax=0.0
   if(imode.eq.1) then !refine DT with given fc
      do iii=-50,50,5
         ctmp1(0:16*200-1)=c2(ipstart+iii:ipstart+iii+16*200-1)*conjg(ctwkp)
         xnorm=sqrt(sum(abs(ctmp1(0:16*200-1))**2))*sqrt(16.0*200.0)
         xc=abs(sum(ctmp1))/xnorm
         if(xc.gt.xmax) then
            iiibest=iii
            xmax=xc
         endif
      enddo
      istart=istart+iiibest
      return
   endif
! else refine fc with given DT
   ctmp1(0:16*200-1)=c2(ipstart:ipstart+16*200-1)*conjg(ctwkp)
   xnorm=sqrt(sum(abs(ctmp1(0:16*200-1))**2))*sqrt(16.0*200.0)
   ctmp1=ctmp1/xnorm
   call four2a(ctmp1,4*16*200,1,-1,1)             !c2c FFT to freq domain
   xmax=0.0
   ctmp1=cshift(ctmp1,-200)
   dfp=1/(4*6400.0/12000.0*16)
!   do i=150,250
   do i=190,210
      xa=abs(ctmp1(i))
!write(51,*) (i-200)*dfp,xa
      if(xa.gt.xmax) then
         ishift=i
         xmax=xa
      endif
   enddo
   delta=(ishift-200)*dfp
   xm1=abs(ctmp1(ishift-1))
   x0=abs(ctmp1(ishift))
   xp1=abs(ctmp1(ishift+1))
   xint=(log(xm1)-log(xp1))/(log(xm1)+log(xp1)-2*log(x0))
   delta2=delta+xint*dfp/2.0
   fc=fc+delta2
   return
end subroutine coherent_sync

subroutine noncoherent_frame_sync(c2,h,fc,isync2,istart,ssmax)
   complex c2(0:120*12000/32-1)
   complex ct0(0:199),ct1(0:199),ct2(0:199),ct3(0:199)
   integer isync2(216)

   twopi=8.0*atan(1.0)
   dt=32.0/12000.0
   baud=12000.0/6400.0
   imax=370  ! defines dt search range (375 samples/s)
   ssmax=-1e32
   izero=375
   do it = -imax,imax,5
! noncoherent wspr-type dt estimation
      dp0=twopi*(fc-1.5*h*baud)*dt
      dp1=twopi*(fc-0.5*h*baud)*dt
      dp2=twopi*(fc+0.5*h*baud)*dt
      dp3=twopi*(fc+1.5*h*baud)*dt
      th0=0.0
      th1=0.0
      th2=0.0
      th3=0.0
      do i=0,199
         ct0(i)=cmplx(cos(th0),sin(th0))
         ct1(i)=cmplx(cos(th1),sin(th1))
         ct2(i)=cmplx(cos(th2),sin(th2))
         ct3(i)=cmplx(cos(th3),sin(th3))
         th0=mod(th0+dp0,twopi)
         th1=mod(th1+dp1,twopi)
         th2=mod(th2+dp2,twopi)
         th3=mod(th3+dp3,twopi)
      enddo
      xs=0.0
      xn=0.0
      do is=1,216
         i0=izero+it+(is-1)*200
         p0=abs(sum(c2(i0:i0+199)*conjg(ct0)))
         p1=abs(sum(c2(i0:i0+199)*conjg(ct1)))
         p2=abs(sum(c2(i0:i0+199)*conjg(ct2)))
         p3=abs(sum(c2(i0:i0+199)*conjg(ct3)))
         p0=p0**2
         p1=p1**2
         p2=p2**2
         p3=p3**2
         if(isync2(is).eq.0) then
!            xs=xs+(p0+p2)/2.0
            xs=xs+max(p0,p2)
            xn=xn+(p1+p3)/2.0
         elseif(isync2(is).eq.1) then
!            xs=xs+(p1+p3)/2.0
            xs=xs+max(p1,p3)
            xn=xn+(p0+p2)/2.0
         endif
      enddo
      sy=xs/xn
!write(41,*) it,sy
      if(sy.gt.ssmax) then
         ioffset=it
         ssmax=sy
      endif
   enddo
   istart=izero+ioffset
   return
end subroutine noncoherent_frame_sync

subroutine getmetric2(ib,ps,ns,xmet)
   real ps(0:ns-1)
   xm1=0
   xm0=0
   do i=0,ns-1
      if( iand(i/ib,1) .eq. 1 .and. ps(i) .gt. xm1 ) xm1=ps(i)
      if( iand(i/ib,1) .eq. 0 .and. ps(i) .gt. xm0 ) xm0=ps(i)
   enddo
   xmet=xm1-xm0
   return
end subroutine getmetric2

subroutine downsample2(ci,f0,h,co)
   parameter(NI=216*200,NH=NI/2,NO=NI/20)  ! downsample from 200 samples per symbol to 10
   complex ci(0:NI-1),ct(0:NI-1)
   complex co(0:NO-1)
   fs=12000.0/32.0
   df=fs/NI
   ct=ci
   call four2a(ct,NI,1,-1,1)             !c2c FFT to freq domain
   i0=nint(f0/df)
   ct=cshift(ct,i0)
   co=0.0
   co(0)=ct(0)
   b=max(1.0,h)*8.0
   do i=1,NO/2
      arg=(i*df/b)**2
      filt=exp(-arg)
      co(i)=ct(i)*filt
      co(NO-i)=ct(NI-i)*filt
   enddo
   co=co/NO
   call four2a(co,NO,1,1,1)            !c2c FFT back to time domain
   return
end subroutine downsample2

subroutine getcandidate2(c,npts,fs,fa,fb,ncand,candidates)
   parameter(NDAT=200,NFFT1=120*12000/32,NH1=NFFT1/2,NFFT2=120*12000/320,NH2=NFFT2/2)
   complex c(0:npts-1)                   !Complex waveform
   complex cc(0:NFFT1-1)
   complex csfil(0:NFFT2-1)
   complex cwork(0:NFFT2-1)
   real bigspec(0:NFFT2-1)
   complex c2(0:NFFT1-1)                 !Short spectra
   real s(-NH1+1:NH1)                    !Coarse spectrum
   real ss(-NH1+1:NH1)                   !Smoothed coarse spectrum
   real candidates(100,2)
   integer indx(NFFT2-1)
   logical first
   data first/.true./
   save first,w,df,csfil

   if(first) then
      df=10*fs/NFFT1
      csfil=cmplx(0.0,0.0)
      do i=0,NFFT2-1
         csfil(i)=exp(-((i-NH2)/32.0)**2)  ! revisit this
      enddo
      csfil=cshift(csfil,NH2)
      call four2a(csfil,NFFT2,1,-1,1)
      first=.false.
   endif

   cc=cmplx(0.0,0.0)
   cc(0:npts-1)=c;
   call four2a(cc,NFFT1,1,-1,1)
   cc=abs(cc)**2
   call four2a(cc,NFFT1,1,-1,1)
   cwork(0:NH2)=cc(0:NH2)*conjg(csfil(0:NH2))
   cwork(NH2+1:NFFT2-1)=cc(NFFT1-NH2+1:NFFT1-1)*conjg(csfil(NH2+1:NFFT2-1))

   call four2a(cwork,NFFT2,1,+1,1)
   bigspec=cshift(real(cwork),-NH2)
   il=NH2+fa/df
   ih=NH2+fb/df
   nnl=ih-il+1
   call indexx(bigspec(il:il+nnl-1),nnl,indx)
   xn=bigspec(il-1+indx(nint(0.3*nnl)))
   bigspec=bigspec/xn
   ncand=0
   do i=il,ih
      if((bigspec(i).gt.bigspec(i-1)).and. &
         (bigspec(i).gt.bigspec(i+1)).and. &
         (bigspec(i).gt.1.12).and.ncand.lt.100) then
         ncand=ncand+1
         candidates(ncand,1)=df*(i-NH2)
         candidates(ncand,2)=10*log10(bigspec(i)-1)-26.0
      endif
   enddo
   return
end subroutine getcandidate2

subroutine wsprcpm_downsample(iwave,c)

! Input: i*2 data in iwave() at sample rate 12000 Hz
! Output: Complex data in c(), sampled at 400 Hz

   include 'wsprcpm_params.f90'
   parameter (NMAX=120*12000,NFFT2=NMAX/32)
   integer*2 iwave(NMAX)
   complex c(0:NMAX/32-1)
   complex c1(0:NFFT2-1)
   complex cx(0:NMAX/2)
   real x(NMAX)
   equivalence (x,cx)

   df=12000.0/NMAX
   x=iwave
   call four2a(x,NMAX,1,-1,0)             !r2c FFT to freq domain
   i0=nint(1500.0/df)
   c1(0)=cx(i0)
   do i=1,NFFT2/2
      c1(i)=cx(i0+i)
      c1(NFFT2-i)=cx(i0-i)
   enddo
   c1=c1/NFFT2
   call four2a(c1,NFFT2,1,1,1)            !c2c FFT back to time domain
   c=c1(0:NMAX/32-1)
   return
end subroutine wsprcpm_downsample

