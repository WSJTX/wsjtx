subroutine jtty_decode(iwave,nwave,f0,ftol,smin,synced,xdt,f1,snr,decoded,success,nharderrors,nsync,dmin)
   use jtty_mod
   use jtty_fec
   parameter (NMAX=30*12000)                 !Max length of data @12000 Hz
   parameter (NZ=30*6000)                    !Max length of data @6000 Hz
   parameter (NSS=NSPS/2)                    !Samples per symbol @6000 Hz
   parameter (NFFT=13*NSPS,NH2=NFFT/2)
   character*80 decoded
   character*32 c32(MAX_FRAMES)
   integer*2 iwave(nwave)
   real s(0:NH2)
   real sm(0:NH2)
   real s0(0:NH2)
   real a(3)
   real bitmetrics(1:80)
   real pow(0:3)
   complex c(0:NFFT-1)
   complex c0(0:262143)
   complex c1(0:NZ-1)
   complex csync(0:13*192-1)           !Waveform for sync
   complex ctones(0:191,0:3)
   integer*1 message32(32)
   integer*1 cw80(80)
   integer   iloc(1)
   integer isyncvec(13),irxsync(13)
   logical success
   data isyncvec/0,0,0,0,0,3,3,0,0,3,0,3,0/

   complex z
   logical first,synced
   data first/.true./,snrbest/-9999.0/
   save csync, baud, dt, twopi, ctones, first

   success=.false.
   if(sum(abs(iwave)).eq.0) return

   if(first) then
! Generate complex waveform for sync
      call gen_syncwave(csync)
      twopi=8.0*atan(1.0)
      baud=6000.0/192.0   !31.25
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
   df2=fsample/NFFT
   decoded=' '

   if(.not.synced) then
      sbest=0.
      fbest=0.
      xdtbest=0.
      fpk=0.
      ja=(f0-ftol)/df2
      jb=(f0+ftol)/df2
      do i0=0,2544,10                           !Search over xdt for sync pattern
         xdt=i0*dt
         c(0:13*NSS-1)=conjg(csync(0:13*NSS-1))*c0(i0:i0+13*NSS-1)
         c(13*NSS:)=0.
         call four2a(c,NFFT,1,-1,1)            !Compute the sync-shifted spectrum
         spk=0.
         do j=ja-2,jb+2
            s(j)=real(c(j))**2 + aimag(c(j))**2
         enddo
         do j=ja,jb
            sm(j)=s(j-2)+2*s(j-1)+3*s(j)+2*s(j+1)+s(j+2)
            if(sm(j).gt.spk) then
               spk=sm(j)
               fpk=j*df2
               xdt=i0*dt
            endif
         enddo
         if(spk.gt.sbest) then
            s0=sm
            sbest=spk
            fbest=fpk
            xdtbest=xdt
         endif
      enddo

      xdt=xdtbest
      snr=db(sbest)
      f1=fbest

!      call jtty_peakup(c0,c1,csync,xdtbest,fbest,xdt,f1,snr)
   endif

   a=0.
   a(1)=-f1                                !Shift peak to zero frequency
   call twkfreq(c0,c1,npts,6000.0,a)

   pt=0.
   pa=0.
   do j=1,13                                ! find tone powers for sync symbols
      i0=nint(xdt/dt) + (j-1)*192
      if(i0.gt.npts) exit

      do i=0,3
         c(0:NSS-1)=conjg(ctones(0:NSS-1,i))*c1(i0:i0+NSS-1)
         z=sum(c(0:NSS-1))
         pow(i)=abs(z)**2
      enddo
      iloc=maxloc(pow)-1
      irxsync(j)=iloc(1)
      pt=pt+pow(isyncvec(j))
      pa=pa+sum(pow)
   enddo
   ssnr=-99.0
   pn=(pa-pt)/3.0
   if(pn.gt.0.) ssnr=db(pt/pn)
   snr=ssnr                                 ! replace the snr derived from sync-shifted spectrum
   nsync=count(isyncvec.eq.irxsync)         ! nsync is the number of correct hard-decoded sync tones.

   if(nsync .ge. 5 .and. snr .gt. smin) go to 10
   return

10 synced=.true.


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

   maxiterations=25
   nharderrors=-1
   dmin=0.0
   call bpdecode_80_32(bitmetrics,maxiterations,message32,cw80,nharderrors)
   if(nharderrors .lt. 0) then
      ndeep=3
      call osd80_32(bitmetrics, ndeep, message32, cw80, nharderrors, dmin)
   endif
   if(nharderrors .ge. 0 .and. sum(message32) .eq. 0) nharderrors=-nharderrors  ! reject the all zero message

   decoded=' '
   if( nharderrors .ge. 0 ) then
      success=.true.
      write(c32,'(32i1)') message32
      call unpack_jtty(c32,1,decoded)
   endif
   return
end subroutine jtty_decode
