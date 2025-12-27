subroutine jtty_decode(iwave,nwave,f0,ftol,smin,synced,xdt,f1,snr,decoded)
   use jtty_mod
   use jtty_fec
   parameter (NMAX=20*12000)                 !Max length of data @12000 Hz
   parameter (NZ=20*6000)                    !Max length of data @6000 Hz
!  parameter (NSPS=384)                      !Samples per symbol @12000 Hz
   parameter (NSS=NSPS/2)                    !Samples per symbol @6000 Hz
   parameter (NSC=7*NSPS/2)                  !Samples per char @6000 Hz (1680)
   parameter (NFFT2=4*NSC,NH2=NFFT2/2)
   parameter (NFFT=NSC,NH=NFFT/2)
   character*80 decoded
   character*42 c42(MAX_FRAMES)
   integer*2 iwave(nwave)
   real s(0:NH2)
   real s0(0:NH2)
   real a(3)
   real bitmetrics(1:80)
   real pow(0:3)
   complex c(0:NFFT2-1)
   complex c0(0:262143)
   complex c1(0:NZ-1)
   complex csync(0:2*NSC-1)           !Waveform for sync
   complex cwave(NSC,0:63)            !Waveforms and for the 64 JTTY characters
   complex ctones(0:191,0:3)
   integer*1 message42(42)
   integer*1 cw80(80)

   complex z
   logical first,synced
   data first/.true./,snrbest/-9999.0/
!  save first,snrbest,xdt_3,f1_3
   save

   if(first) then
      ! Generate complex waveforms for sync and for the 64 JTTY characters.
      call cw_cwave(NSC,csync,cwave)

      twopi=8.0*atan(1.0)
      baud=6000/192.0   !31.25
      dt=1/6000.0

      do i=0,3
         phi=0.0
         dphi=twopi*i*baud*dt
         do j=0,191
            ctones(j,i)=cmplx(cos(phi),sin(phi))
            phi=phi+dphi
         enddo
      enddo

      first=.false.
   endif

   call ana64a(iwave,nwave,c0)

   npts=nwave/2
   c0(npts:)=0.

   fsample=6000.0
   dt=1.0/fsample
   df=fsample/NFFT
   df2=fsample/NFFT2

   if(.not.synced) then
      sbest=0.
      fpk=0.
      xdtbest=0.
      fbest=0.
      ja=(f0-ftol)/df2
      jb=(f0+ftol)/df2
      do i0=0,2400,20                           !Search over xdt for sync pattern
         xdt=i0*dt
         c(0:13*NSS-1)=conjg(csync(0:13*NSS-1))*c0(i0:i0+13*NSS-1)
         c(13*NSS:)=0.
         call four2a(c,NFFT2,1,-1,1)            !Compute the sync-shifted spectrum
         spk=0.
         do j=ja,jb
            s(j)=real(c(j))**2 + aimag(c(j))**2
!            write(13,3013) j*df2,s(j)
!3013        format(2f12.3)
            if(s(j).gt.spk) then
               spk=s(j)
               fpk=j*df2
               xdt=i0*dt
            endif
         enddo
!         write(14,3014) i0,xdt,fpk,spk
!3014     format(i6,f10.6,f10.3,f12.3)
         if(spk.gt.sbest) then
            s0=s
            sbest=spk
            fbest=fpk
            xdtbest=xdt
         endif
      enddo

      xdt=xdtbest
      snr=db(sbest)
      f1=fbest

! Should do a peakup here, to get accurate values for f1 and DT
! ... And make sure that 'blue' and 'red' curves have good peaks!
! Also, we need a better SNR measurment.

      call jtty_peakup(c0,c1,csync,xdtbest,fbest,xdt,f1,snr)
!     write(*,3081) 'aa',nwave,xdt,f1,snr

      if(snr.gt.snrbest) then
         xdt_3=xdt
         f1_3=f1
         snrbest=snr
      endif
!     write(*,3081) 'bb',nwave,xdt,f1,snr,snrbest,synced
!3081  format(a2,i8,4f10.2,L3)
!     if(snrbest.gt.smin .and. snr.lt.snrbest) go to 10
      if(snrbest.gt.smin .and. snr.le.snrbest) go to 10
      return
   endif

10 if(.not.synced) then
      xdtbest=xdt_3
      fbest=f1_3
!     write(*,3091) nwave,nwave/12000.0,xdtbest,fbest,snrbest
!3091  format('Synced:',i8,4f10.2)
      synced=.true.
   endif

! At this point we are 'synced" and have determined xdt and f1.

   xdt=xdtbest
   f1=fbest
   snr=snrbest
!  print*,'aa',npts,count(abs(c0).gt.0.0)

   a=0.
   a(1)=-f1                                !Shift peak to zero frequency
   call twkfreq(c0,c1,npts,6000.0,a)

   do j=1,40                                ! find tone powers for 40 symbols
      i0=nint(xdt/dt) + 13*NSS + (j-1)*192
      if(i0.gt.npts) exit

      do i=0,3
         c(0:NSS-1)=conjg(ctones(0:NSS-1,i))*c1(i0:i0+NSS-1)
         z=sum(c(0:NSS-1))
         pow(i)=abs(z)**2
      enddo

! tones 0:3 represent bit sequences 00, 01, 11, 10, respectively
      p00=pow(0); p01=pow(1); p11=pow(2); p10=pow(3)

      bitmetrics(2*j-1) = (p11 + p10) - (p00 + p01)
      bitmetrics(2*j  ) = (p11 + p01) - (p00 + p10)
   enddo

   x2=sum(bitmetrics**2)/80.0
   bitmetrics=2.75*bitmetrics/sqrt(x2)

   maxiterations=20
   nharderror=0
   call bpdecode_80_42(bitmetrics,maxiterations,message42,cw80,nharderror)
   write(c42,'(42i1)') message42
   if(sum(message42) .eq. 0) then ! reject the all zero message
      nharderror=-1
      decoded=''
      return
   endif
   if( nharderror.ge.0 ) then
      call unpack_jtty(c42,1,decoded)
   else
      decoded="*****"
   endif
   return
end subroutine jtty_decode
