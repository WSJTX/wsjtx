module jtty_mdec
  integer, parameter        :: MAX_DECODES = 100
  integer, parameter        :: MAX_SLOTS = 100
  integer                   :: ndecodes = 0
  integer                   :: nslots = 0
contains

  subroutine jtty_mdecode(istart,iwave,nchunk,nsps,ndebug,f0,ftol,smin,synced, &
       xdt,f1,snrdb,line,success,nharderrors,nsync,dmin)

!  First try at a multi-decoder for JTTY - replaces the single-decode version in
!  jtty_decode.f90. Does not pass decodes back to rjtty_sub yet - just prints
!  results to the console

!  Note: nsps is samples per symbol at 12000 s^-1 sample rate.

   use jtty_mod
   use jtty_fec
   implicit none
   character*80, intent(out) :: line
   character*80              :: msg
   character*32              :: c32(MAX_FRAMES)
   integer*1                 :: message32(32), cw80(80)
   integer*2, intent(in)     :: iwave(nchunk)
   integer, intent(in)       :: istart, ndebug
   integer                   :: i,i0,j,ja,jb,k,kz,n
   integer                   :: ntstep,istep
   integer                   :: nchan, ichan
   integer, intent(in)       :: nchunk,nsps   !size of chunk, nsps at 12000 Sa/s
   integer                   :: nchunk6,nana  !size of chunk, nana at 6000 Sa/s
   integer                   :: nframe6       !size of frame at 6000 Sa/s
   integer, save             :: nsps0=-999
   integer, save             :: nfft,nh2,nss
   integer                   :: iloc(1)
   integer                   :: irxsync(13)
   integer                   :: ndeep, maxiterations, islot
   integer, intent(out)      :: nharderrors,nsync
   real                      :: fsample,fc,fwid
   real                      :: spk,fpk,pa,pt,pn
   real                      :: fbest,xdtbest,sbest
   real, allocatable         :: s(:), sm(:), s0(:,:)
   real                      :: a(3)
   real                      :: bitmetrics(1:80), pow(0:3)
   real                      :: p00, p01, p11, p10
   real, save                :: twopi,baud,dt
   real                      :: phi,dphi,df2
   real                      :: x2,db
   real, intent(in)          :: f0,ftol,smin
   real, intent(out)         :: dmin
   real, intent(inout)       :: xdt,f1,snrdb
   real                      :: xdt1, f11, snr0, df1, dtsync, dxdt
   complex, allocatable      :: c(:)
   complex, allocatable      :: c0(:)
   complex, allocatable      :: c1(:)
   complex, allocatable,save :: csync(:)    !Waveform for sync at 6000 s^-1 sample rate
   complex, allocatable,save :: ctones(:,:)
   complex                   :: z
   logical, intent(out)      :: success
   logical, intent(inout)    :: synced
   logical                   :: match

   type :: decode
      real :: f1    = 0.0              !Synced audio frequency
      real :: xdt   = 0.0              !Synced DT (0 to 0.5 s)
      real :: tsync = 0.0              !Time of sync from istart=1
      real :: snrdb = 0.0              !SNR of decoded frame
      integer ::  k = 0                !Accumulated length of decoded text
      character*80 :: decoded = ''
   end type

   type(decode)              :: cand(0:14)        !Candidates for decoding
   type(decode)              :: dec               !Current successful decode
   type(decode), save        :: slot(MAX_SLOTS)   !Accumulating decode messages 
   
   if(istart.eq.1) then
      ndecodes=0
      nslots=0
   endif
   success=.false.
   if(sum(abs(iwave)).eq.0) return
   if(f0+ftol.eq.-99.0) return               !Silence compiler warning of unused params

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
   ntstep=nframe6/4
   allocate(s0(0:nh2,0:ntstep))

   fsample=6000.0
   dt=1.0/fsample
   df2=fsample/nfft

   istep=0
   do i0=0,ntstep,12                     !Search over quarter-frame segment
      xdt=i0*dt
      c(0:13*nss-1)=conjg(csync(0:13*nss-1))*c0(i0:i0+13*nss-1)
      c(13*nss:)=0.
      call four2a(c,nfft,1,-1,1)            !Compute the sync-shifted spectrum
      do j=0,nh2
         s(j)=real(c(j))**2 + aimag(c(j))**2
      enddo
      sm=0.
      do j=2,nh2-2
         sm(j)=s(j-2)+2*s(j-1)+3*s(j)+2*s(j+1)+s(j+2)
      enddo
      s0(0:nh2,istep)=sm
      istep=istep+1
   enddo

   nchan= 14

   do ichan=0, nchan         ! frequency channels - channel 0 is always centered on f0
      if(ichan.eq.0) then
         fc=1500      ! hardwired for now
         fwid=50
      else            ! for now, hardwired nonoverlapping channels
         fc=ichan*200
         fwid=100
         if(ichan.eq.7) then
            fc=1375.0
            fwid=75
         elseif(ichan.eq.8) then
            fc=1625.0
            fwid=75
         endif
      endif

      sbest=0.
      fbest=0.
      xdtbest=0.
      fpk=0.

      ja=(fc-fwid)/df2
      jb=(fc+fwid)/df2
      if(ja .lt. 3) ja=3

      spk=0.
      do j=ja,jb,2
         do istep=0,ntstep
            if(s0(j,istep).gt.spk) then
               spk=s0(j,istep)
               fpk=j*df2
               xdt=istep*dt*12
            endif
         enddo
         if(spk.gt.sbest) then
            sbest=spk
            fbest=fpk
            xdtbest=xdt
         endif
      enddo

      if(ichan.eq.0) then
         call jtty_peakup(c0,c1,csync,nchunk6, nss, xdtbest, fbest, xdt1, f11, snr0)
         xdtbest=xdt1
         fbest=f11
      endif

      cand(ichan)%xdt=xdtbest
      cand(ichan)%f1=fbest

      a=0.
      a(1)=-cand(ichan)%f1                                !Shift peak to zero frequency
      call twkfreq(c0,c1,nchunk6,6000.0,a)

      pt=0.
      pa=0.
      do j=1,13                                ! find tone powers for sync symbols
         i0=nint(cand(ichan)%xdt/dt) + (j-1)*nss
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
      snrdb=-99.9
      pn=(pa-pt)/3.0
      if(pn.gt.0.) snrdb=db(pt/pn)
      nsync=count(is13.eq.irxsync)             ! nsync is the number of correct hard-decoded sync tones.
      cand(ichan)%snrdb=snrdb
      if( nchan.eq.0 .and. (nsync .le. 6 .or. snrdb .lt. smin)) cycle
      if( nchan.ne.0 .and. (nsync .le. 8 .or. snrdb .lt. 5.0)) cycle

! looks like a real candidate - try to decode
      do j=1,40                                ! find tone powers for 40 symbols
         i0=nint(cand(ichan)%xdt/dt) + 13*nss + (j-1)*nss
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
         ndeep=1
         if(ichan.eq.0) ndeep=3
         call osd80_32(bitmetrics, ndeep, message32, cw80, nharderrors, dmin)
      endif
      if(nharderrors .ge. 0 .and. sum(message32) .eq. 0) nharderrors=-1  ! reject the all zero message

      cand(ichan)%decoded=' '
      line=' '
      if( nharderrors .ge. 0 ) then
         success=.true.
         ndecodes=ndecodes+1
         write(c32(1),'(32i1)') message32
         call unpack_jtty(c32,1,cand(ichan)%decoded)
         if(cand(ichan)%decoded(1:4).eq.'599 ') then
            cand(ichan)%decoded = '~' // trim(cand(ichan)%decoded)
         endif
         cand(ichan)%tsync=(istart-1)/12000.0 + cand(ichan)%xdt
         dec=cand(ichan)
         if(ichan.eq.0) then
! Make single-channel rjtty_sub happy
            line=cand(ichan)%decoded
            xdt=cand(ichan)%xdt
            f1=cand(ichan)%f1
            snrdb=cand(ichan)%snrdb
         endif
         match=.false.
         islot=1
         if(ndecodes.eq.1) then
            nslots=1
            islot=1
            slot(1)=dec
         else
            do i=1,nslots
               df1=dec%f1 - slot(i)%f1
               dxdt=dec%xdt - slot(i)%xdt
               dtsync=dec%tsync - slot(i)%tsync
               match=abs(df1).lt.5.0 .and. abs(dxdt).lt.0.005
               if(match) then
                  islot=i
                  k=slot(i)%k
                  n=len_trim(dec%decoded)
                  kz=min(k+n,80)
                  slot(i)%decoded=trim(slot(i)%decoded)//dec%decoded(1:kz-k)
                  slot(i)%k=kz
                  exit
               endif
            enddo
            if(.not.match) then
               nslots=nslots+1
               slot(nslots)=dec
               islot=nslots
            endif
         endif
         msg=slot(islot)%decoded
         do i=1,len_trim(msg)
            if(msg(i:i).eq.'~') msg(i:i)=' '       !For display, remove ~ chars
         enddo
         if(msg(1:1).eq.' ') msg=msg(2:)
         if(ndebug.eq.0) then
            write(*,3001) nint(dec%f1),nint(dec%snrdb-20.0),trim(msg)
3001        format(i4,i5,2x,a)
         else
            write(*,3002) ichan,ndecodes,islot,nslots,match,dec%f1, &
                 dec%xdt,dec%tsync,nint(dec%snrdb-20.0),trim(msg)
3002        format(4i4,L3,f7.1,f7.3,f9.3,i5,2x,a)
         endif
      endif
   enddo     ! ichan, frequency channel loop

   synced=.false.
   success=.false.
   flush(6)

   return
end subroutine jtty_mdecode

end module jtty_mdec
