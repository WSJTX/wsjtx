subroutine jtty_decode(iwave,nchunk,nsps,f0,ftol,smin,synced,xdt,f1,snr,decoded,success,nharderrors,nsync,dmin)
!
!  note nsps is samples per symbol at 12000 s^-1 sample rate.
!
   use jtty_mod
   use jtty_fec
   implicit none
   character*80, intent(out) :: decoded
   character*32              :: c32(MAX_FRAMES)
   integer*1                 :: message32(32), cw80(80)
   integer*2, intent(in)     :: iwave(nchunk)
   integer                   :: i,j,i0,ja,jb
   integer, intent(in)       :: nchunk,nsps   !size of chunk, nsps at 12000 Sa/s
   integer                   :: nchunk6,nana  !size of chunk, nana at 6000 Sa/s
   integer                   :: nframe6       !size of frame at 6000 Sa/s
   integer, save             :: nsps0=-999
   integer, save             :: nfft,nh2,nss
   integer                   :: iloc(1)
   integer                   :: irxsync(13)
   integer                   :: ndeep, maxiterations
   integer, intent(out)      :: nharderrors,nsync
   real                      :: fsample
   real                      :: spk,fpk,pa,pt,pn
   real                      :: fbest,xdtbest,sbest
   real, allocatable         :: s(:), sm(:), s0(:)
   real                      :: a(3)
   real                      :: bitmetrics(1:80), pow(0:3)
   real                      :: p00, p01, p11, p10
   real, save                :: twopi,baud,dt
   real                      :: phi,dphi,df2
   real                      :: x2,ssnr,db
   real, intent(in)          :: f0,ftol,smin
   real, intent(out)         :: dmin
   real, intent(inout)       :: xdt,f1,snr
   complex, allocatable      :: c(:)
   complex, allocatable      :: c0(:)
   complex, allocatable      :: c1(:)
   complex, allocatable,save :: csync(:)    !Waveform for sync at 6000 s^-1 sample rate
   complex, allocatable,save :: ctones(:,:)
   complex                   :: z
   logical, intent(out)      :: success
   logical, intent(inout)    :: synced

   success=.false.
   if(sum(abs(iwave)).eq.0) return

   if(nsps.ne.nsps0) then
      nsps0=nsps
      nss=nsps/2    ! samples per symbol at 6000 sa/s
      nfft=8192     ! FFT size for sync search, gives df2=0.732
      nh2=nfft/2    ! spectrum size for sync search

! allocate saved arrays
      if(allocated(csync)) deallocate(csync)
      allocate(csync(0:13*nss-1))
      if(allocated(ctones)) deallocate(ctones)
      allocate(ctones(0:nss-1,0:3)) 

! Generate complex waveform for sync
      twopi=8.0*atan(1.0)
      baud=6000.0/real(nss)   !31.25 for nss=192
      dt=1/6000.0
      call gen_syncwave(csync,nss)

      do i=0,3
         phi=0.0
         dphi=twopi*i*baud*dt
         do j=0,nss-1
            ctones(j,i)=cmplx(cos(phi),sin(phi))
            phi=phi+dphi
         enddo
      enddo
   endif

   nchunk6=nchunk/2                ! chunk size at 6000 Sa/s 
   nframe6=53*nss                  ! frame size at 6000 Sa/s

! make size of c0 next power of 2 larger than nchunk
   nana = 2**nint(log(real(nchunk))/log(2.0)+0.5)
   allocate(c0(0:nana-1))

!  convert integer samples at 12K Sa/s to complex analytic signal at 6K Sa/s
   call ana64a(iwave,nchunk,c0,nana) 
   c0(nchunk6:)=0.

   allocate(c(0:nfft-1))        ! 
   allocate(c1(0:nchunk6-1))
   allocate(s(0:nh2))
   allocate(sm(0:nh2))
   allocate(s0(0:nh2))

   fsample=6000.0
   dt=1.0/fsample
   df2=fsample/nfft
   decoded=' '

   if(.not.synced) then
      sbest=0.
      fbest=0.
      xdtbest=0.
      fpk=0.
      ja=(f0-ftol)/df2
      jb=(f0+ftol)/df2
      if(ja .lt. 3) ja=3
      do i0=0,nframe6/4,12                     !Search over quarter-frame segments
         xdt=i0*dt
         c(0:13*nss-1)=conjg(csync(0:13*nss-1))*c0(i0:i0+13*nss-1)
         c(13*nss:)=0.
         call four2a(c,nfft,1,-1,1)            !Compute the sync-shifted spectrum
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
   call twkfreq(c0,c1,nchunk6,6000.0,a)

   pt=0.
   pa=0.
   do j=1,13                                ! find tone powers for sync symbols
      i0=nint(xdt/dt) + (j-1)*nss
      if(i0+nss.gt.nchunk6) exit

      do i=0,3
         c(0:nss-1)=conjg(ctones(0:nss-1,i))*c1(i0:i0+nss-1)
         z=sum(c(0:nss-1))
         pow(i)=abs(z)**2
      enddo
      iloc=maxloc(pow)-1
      irxsync(j)=iloc(1)
      pt=pt+pow(is13(j))                !signal plus noise
      pa=pa+sum(pow)                        !signal plus 4*noise
   enddo
   ssnr=-99.0
   pn=(pa-pt)/3.0
   if(pn.gt.0.) ssnr=db(pt/pn)              ! pt/pn instead of pt/pn-1 to avoid negative snr estimates
   snr=ssnr                                 ! replace the snr derived from sync-shifted spectrum
   nsync=count(is13.eq.irxsync)         ! nsync is the number of correct hard-decoded sync tones.

   if(nsync .gt. 6 .and. snr .gt. smin) go to 10
   return

10 synced=.true.


   do j=1,40                                ! find tone powers for 40 symbols
      i0=nint(xdt/dt) + 13*nss + (j-1)*nss
      if(i0+nss .gt. nchunk6) exit

      do i=0,3
         c(0:nss-1)=conjg(ctones(0:nss-1,i))*c1(i0:i0+nss-1)
         z=sum(c(0:nss-1))
         pow(i)=abs(z)**2
      enddo

! tones 0:3 represent bit sequences 00, 01, 11, 10, respectively
      p00=pow(0); p01=pow(1); p11=pow(2); p10=pow(3)

      bitmetrics(2*j-1) = max(p11,p10) - max(p00,p01)
      bitmetrics(2*j  ) = max(p11,p01) - max(p00,p10)
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
   if(nharderrors .ge. 0 .and. sum(message32) .eq. 0) nharderrors=-1  ! reject the all zero message

   decoded=' '
   if( nharderrors .ge. 0 ) then
      success=.true.
      write(c32(1),'(32i1)') message32
      call unpack_jtty(c32,1,decoded)
   endif
   return
end subroutine jtty_decode
