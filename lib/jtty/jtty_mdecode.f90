module jtty_mdec

  use jtty_fec, only: PAYLOAD_BITS

  type :: decode
     real :: f1    = 0.0              !Synced audio frequency
     real :: xdt   = 0.0              !Synced DT (0 to 0.5 s)
     real :: tsync = 0.0              !Time of sync from istart=1
     real :: snrdb = 0.0              !SNR of decoded frame
     integer ::  k = 0                !Accumulated length of decoded text
     character(len=80) :: decoded = ''
     logical :: trailing_sep = .false. !decoded ends with an implicit separator column
     logical :: is_last_frame = .false. !this frame had the "last frame of message" bit set
     ! Per-frame merge history (capped at MAX_FRAMES, jtty_mod.f90), so a
     ! rediscovered frame can be matched against the exact frame it repeats.
     integer :: nframes_merged = 0
     real    :: frame_f1(16) = 0.0
     real    :: frame_tsync(16) = 0.0
  end type decode

  integer, parameter        :: MAX_DECODES = 100
  integer, parameter        :: MAX_SLOTS = 30
  integer                   :: ndecodes = 0
  integer                   :: nslots = 0
  type(decode)              :: slot(MAX_SLOTS)   !Accumulating decode messages

! Cross-call "retro re-sweep" plumbing (see jtty_mdecode_step): interferer_*
! requests a pre-search subtraction; nsubtracted/subtracted_* report this
! call's own subtractions back out.
  integer, parameter        :: MAX_SUBTRACTED = 16
  logical                   :: interferer_pending = .false.
  real                      :: interferer_f1 = 0.0
  real                      :: interferer_tsync = 0.0
  integer                   :: interferer_payload(PAYLOAD_BITS) = 0
  integer                   :: nsubtracted = 0
  real                      :: subtracted_f1(MAX_SUBTRACTED) = 0.0
  real                      :: subtracted_tsync(MAX_SUBTRACTED) = 0.0
  integer                   :: subtracted_payload(PAYLOAD_BITS,MAX_SUBTRACTED) = 0

contains

  pure subroutine jtty_search_window(fc,fwid,nfa,nfb,constrain_to_graph,df, &
       first_bin,last_bin,ja,jb,usable)
      implicit none
      real, intent(inout) :: fc
      real, intent(in) :: fwid,df
      integer, intent(in) :: nfa,nfb,first_bin,last_bin
      integer, intent(out) :: ja,jb
      logical, intent(in) :: constrain_to_graph
      logical, intent(out) :: usable

      ja=first_bin
      jb=last_bin
      usable=.false.
      if(df.le.0.0 .or. fwid.lt.0.0 .or. first_bin.gt.last_bin) return

      if(constrain_to_graph) then
         if(nfa.gt.nfb) return
         fc=max(real(nfa),min(fc,real(nfb)))
      endif

      ja=max(first_bin,int((fc-fwid)/df))
      jb=min(last_bin,int((fc+fwid)/df))
      if(constrain_to_graph) then
         ja=max(ja,ceiling(real(nfa)/df))
         jb=min(jb,floor(real(nfb)/df))
      endif
      usable=ja.le.jb
  end subroutine jtty_search_window

  subroutine jtty_mdecode(istart,iwave,nchunk,nsps,ndebug,nfa,nfb,f0,ftol,smin)

!  First try at a multi-decoder for JTTY - replaces the single-decode version in
!  jtty_decode.f90. Does not pass decodes back to rjtty_sub yet - just prints
!  results to the console

!  Note: nsps is samples per symbol at 12000 s^-1 sample rate.
      use iso_fortran_env, only: int16
      use jtty_mod
      use jtty_fec
      implicit none
      integer, parameter             :: MAXCAND = 100
      integer, parameter             :: NSYNC_SYM  = 13
      integer, parameter             :: NCHAN_SYM  = 46
      integer, parameter             :: NFRAME_SYM = 59
      real, parameter                :: FSAMPLE = 6000.0
      real, parameter                :: TWOPI = 6.283185307179586
      character(len=80)              :: msg
      character(len=34)              :: c32(MAX_FRAMES)
      integer                        :: final_payload(PAYLOAD_BITS), tone_symbols_chk(NCHAN_SYM)
      integer                        :: tone_symbols_full(NFRAME_SYM), ipass
      integer(int16), intent(in)     :: iwave(nchunk)
      integer, intent(in)            :: istart, ndebug
      integer                        :: i,i0,is,j,ja,jb,k,kz,n
      integer, save                  :: ntstep, ntgrid
      integer                        :: istep
      integer                        :: nchan, ichan
      integer, intent(in)            :: nchunk,nsps   !size of chunk, nsps at 12000 Sa/s
      integer, intent(in)            :: nfa,nfb       !Wide Graph freq range
      integer                        :: nchunk6,nana  !size of chunk, nana at 6000 Sa/s
      integer, save                  :: nframe6       !size of frame at 6000 Sa/s
      integer, save                  :: nsps0=-999
      integer, save                  :: nu0=-999
      integer, save                  :: nfft,nh2,nss
      integer                        :: iloc(1)
      integer                        :: irxsync(NSYNC_SYM), irxchan(NCHAN_SYM)
      integer                        :: islot
      integer                        :: nsloc(2),nfz,ntz,ncand,ic,nc,nstep_search
      integer                        :: nc0,n_ch0_ok
      integer                        :: ja_ch0_ok(16),jb_ch0_ok(16)
      real                            :: f1_ch0_ok(16),tsync_ch0_ok(16)
      integer                        :: nharderrors,nsync,nsymerrs
      real                           :: fc,fwid
      real                           :: fpk,pa,pt,pn
      real                           :: fbest,xdtbest
      real, allocatable, save        :: s(:), s0(:,:)
      logical, allocatable, save     :: mask0(:,:)
      real                           :: a(3)
      real                           :: pow(0:3,NCHAN_SYM)
      real, save                     :: baud,dt,df2
      real                           :: phi,dphi
      real                           :: db
      real, intent(in)               :: f0,ftol,smin
      real                           :: snrdb, xdt
      real                           :: xdt1, f11, snr0, df1, dtsync, dxdt
      complex, allocatable,save      :: c(:)
      complex, allocatable,save      :: c0(:)
      complex, allocatable,save      :: c1(:)
      complex, allocatable,save      :: csync(:)    !Waveform for sync at 6000 s^-1 sample rate
      complex, allocatable,save      :: ctones(:,:)
      complex                        :: z
      logical                        :: match
      logical                        :: dupe
      logical                        :: usable
      logical                        :: is_history_dupe
      logical                        :: success_dec
      logical                        :: channel_decoded, decoded_ok
      logical                        :: any_subtracted
      integer                        :: ir
      integer                        :: kf
      type(decode)                   :: cand(MAXCAND)     !Candidates for decoding
      type(decode)                   :: dec               !Current successful decode
      logical                        :: use_interferer
      real                            :: use_interferer_f1, use_interferer_tsync
      integer                         :: use_interferer_payload(PAYLOAD_BITS)

! Capture and clear the retro-resweep interferer request (if any) as the
! very first thing this call does, before any possible early return below
! -- otherwise a stale request could leak into a later, unrelated call.
      use_interferer=interferer_pending
      use_interferer_f1=interferer_f1
      use_interferer_tsync=interferer_tsync
      use_interferer_payload=interferer_payload
      interferer_pending=.false.
      nsubtracted=0

      nharderrors=-1
      nsync=0

      if(nu0.ne.JTTY_WAVA_NU) then
         nu0=JTTY_WAVA_NU
         call tbcc_init(JTTY_WAVA_NU)
      endif

      if(istart.eq.1 .and. .not.use_interferer) then
         ndecodes=0
         nslots=0
      endif
      if(sum(abs(int(iwave))).eq.0) return

      nchunk6=nchunk/2                ! chunk size at 6000 Sa/s
! nana is the size of c0 - next power of 2 larger than nchunk
      nana = 2**nint(log(real(nchunk))/log(2.0)+0.5)

      if(nsps.ne.nsps0) then
         nsps0=nsps
         nss=nsps/2    ! samples per symbol at 6000 sa/s
         nfft=8192     ! FFT size for sync search, gives df2=0.732
         df2=FSAMPLE/nfft 
         nh2=nfft/2    ! spectrum size for sync search
         nframe6=NFRAME_SYM*nss          ! frame size at 6000 Sa/s
         ntstep=nframe6/4
         ntgrid=ntstep/12

! allocate saved arrays once
         if(allocated(csync)) deallocate(csync)
           allocate(csync(0:NSYNC_SYM*nss-1))
         if(allocated(ctones)) deallocate(ctones)
           allocate(ctones(0:nss-1,0:3))
         if(allocated(c0)) deallocate(c0)
           allocate(c0(0:nana-1))
         if(allocated(c)) deallocate(c)
           allocate(c(0:nfft-1))        !
         if(allocated(c1)) deallocate(c1)
           allocate(c1(0:nchunk6-1))
         if(allocated(s)) deallocate(s)
           allocate(s(0:nh2))
         if(allocated(s0)) deallocate(s0)
           allocate(s0(0:nh2,0:ntgrid))
         if(allocated(mask0)) deallocate(mask0)
           allocate(mask0(0:nh2,0:ntgrid))

! Generate complex waveform for sync
         baud=FSAMPLE/real(nss)   !31.25 for nss=192
         dt=1/FSAMPLE
         call gen_syncwave(csync,nss)

         do i=0,3
            phi=0.0
            dphi=i*TWOPI/real(nss)
            do j=0,nss-1
               ctones(j,i)=cmplx(cos(phi),sin(phi))
               phi=phi+dphi
            enddo
         enddo
      endif

!  convert integer samples at 12K Sa/s to complex analytic signal at 6K Sa/s
      call ana64a(iwave,nchunk,c0,nana)
      c0(nchunk6:)=0.

      if(use_interferer) then
         ! Retro re-sweep call: subtract the already-known signal out of
         ! this window's own c0 before searching, on the theory that this
         ! window's own candidates (whose frame spans reach ~ntstep to
         ! nframe6 forward of this window's own [0,ntstep] search range)
         ! may have been corrupted by that signal's energy even though this
         ! window never itself searched for that signal's own sync. See
         ! jtty_mdecode_step.
         tone_symbols_full(1:NSYNC_SYM)=is13
         call tbcc_encode(use_interferer_payload, tone_symbols_chk)
         tone_symbols_full(NSYNC_SYM+1:NFRAME_SYM)=tone_symbols_chk
         call subtract_jtty(c0, nana, nchunk6, tone_symbols_full, NFRAME_SYM, &
              nss, use_interferer_f1, use_interferer_tsync-(istart-1)/12000.0)
      endif

! Look for up to 2 sync candidates in each quarter-frame (0.424 second) by 2*FTol rectangle in
! the time/frequency plane. Find the peak in the search rectangle, then zero a small region
! of size nfz by ntz centered on the peak location and find the location of the next peak.

      nfz=nint(10.0/df2)            ! 14
      ntz=nint(0.016*6000.0/12.0)   !  8

      nchan = 2
      nc=2          ! look for 2 candidates in each channel
      ncand=0
      any_subtracted=.false.

      ! Phase A: channel 0 gets first claim on every signal -- runs its own
      ! full up-to-2-pass sweep to completion, accumulating n_ch0_ok across
      ! both passes, before channels 1/2 (Phase B) ever run.
      n_ch0_ok=0
      do ipass=1,2
         if(ipass.eq.2 .and. .not.any_subtracted) exit
         call build_s0()
         ichan=0
         call process_channel()
      enddo

      ! Phase B: channels 1/2 (hardwired bands, unrefined estimates). s0 is
      ! rebuilt fresh, reflecting whatever channel 0 subtracted in Phase A.
      any_subtracted=.false.
      do ipass=1,2
         if(ipass.eq.2 .and. .not.any_subtracted) exit
         call build_s0()
         do ichan=1,nchan
            call process_channel()
         enddo
      enddo

      return

   contains

   subroutine build_s0()
      ! Rebuild s0, the FFT-correlation sync-search surface, from the
      ! current c0 (may already reflect earlier-phase subtractions).
      istep=0
      do i0=0,ntstep,12                     !Search over quarter-frame segment
         xdt=i0*dt
         c(0:NSYNC_SYM*nss-1)=conjg(csync(0:NSYNC_SYM*nss-1))*c0(i0:i0+NSYNC_SYM*nss-1)
         c(NSYNC_SYM*nss:)=0.
         call four2a(c,nfft,1,-1,1)            !Compute the sync-shifted spectrum
         do j=0,nh2
            s(j)=real(c(j))**2 + aimag(c(j))**2
         enddo
         s0(0:nh2,istep)=0.
         do j=2,nh2-2
            s0(j,istep)=s(j-2)+2*s(j-1)+3*s(j)+2*s(j+1)+s(j+2)
         enddo
         istep=istep+1
      enddo
      nstep_search=istep-1
   end subroutine build_s0

   subroutine process_channel()
      ! One channel's candidate search plus sticky-sync retry, for the
      ! host's current ichan/ipass (host-associated with jtty_mdecode).
      if(ichan.eq.0) then
         fc=f0
         fwid=ftol
         ! Scale channel 0's candidate count with FTol, so a wide band
         ! can't let other signals win both fixed nc=2 slots first.
         nc0=max(2, min(8, nint(fwid/(nfz*df2))))
      else            ! for now, hardwired nonoverlapping channels
         fc=1350
         if(ichan.eq.2) fc=1650
         fwid=150
         nc0=nc
      endif

      call jtty_search_window(fc,fwid,nfa,nfb,ichan.ne.0,df2,3, &
           ubound(s0,1)-2,ja,jb,usable)
      if(.not.usable) return
      fbest=0.
      xdtbest=0.
      fpk=0.
      channel_decoded=.false.

      if(ichan.ne.0 .and. n_ch0_ok.gt.0) then
         ! Erase only channel 0's actual successful-decode neighborhoods
         ! (not its whole nominal band) so channels 1/2 can't rediscover
         ! them, while staying free to catch what channel 0 missed.
         do i=1,n_ch0_ok
            if(max(ja,ja_ch0_ok(i)) .le. min(jb,jb_ch0_ok(i))) &
                 s0(max(ja,ja_ch0_ok(i)):min(jb,jb_ch0_ok(i)),0:nstep_search) = 0.0
         enddo
      endif

      if(ichan.eq.0) mask0(ja:jb,0:nstep_search)=.true.

      do ic=1,nc0
         if(ichan.eq.0) then
            ! Channel 0 uses a private mask instead of zeroing s0
            ! directly, so candidates that never pass the decode gate
            ! don't eat into channels 1/2's shared search surface.
            nsloc=maxloc(s0(ja:jb,0:nstep_search), &
                 mask=mask0(ja:jb,0:nstep_search))
            mask0( max( ja, nsloc(1)-nfz+ja ) : min( jb, nsloc(1)+nfz+ja  ),  &
                max(  0, nsloc(2)-ntz )    : min( nstep_search, nsloc(2)+ntz )   ) = .false.
         else
            nsloc=maxloc(s0(ja:jb,0:nstep_search))
            s0( max( ja, nsloc(1)-nfz+ja ) : min( jb, nsloc(1)+nfz+ja  ),        &
                max(  0, nsloc(2)-ntz )    : min( nstep_search, nsloc(2)+ntz )   ) = 0.0
         endif
         fbest   = (nsloc(1)-1+ja)*df2
         xdtbest = (nsloc(2)-1)*dt*12

         if(ichan.eq.0) then
            call jtty_peakup(c0,c1,csync,nchunk6, nss, xdtbest, fbest, xdt1, f11, snr0)
            xdtbest=xdt1
            fbest=f11
         endif

         if(ncand .ge. MAXCAND) exit
         ncand=ncand+1
         cand(ncand)%xdt=xdtbest
         cand(ncand)%f1=fbest

         a=0.
         a(1)=-cand(ncand)%f1                                !Shift peak to zero frequency
         call twkfreq(c0,c1,nchunk6,6000.0,a)

         pt=0.
         pa=0.
         pow=0.0
         do j=1,NSYNC_SYM                                ! find tone powers for sync symbols
            i0=nint(cand(ncand)%xdt/dt) + (j-1)*nss
            if(i0+nss.gt.nchunk6) exit

            do i=0,3
               z = dot_product(ctones(0:nss-1,i), c1(i0:i0+nss-1))
               pow(i,j)=real(z*conjg(z))
            enddo

            iloc=maxloc(pow(:,j))-1
            irxsync(j)=iloc(1)
            pt=pt+pow(is13(j),j)              !signal plus noise
            pa=pa+sum(pow(:,j))               !signal plus 4*noise
         enddo

         snrdb=-99.9
         pn=(pa-pt)/3.0
         if(pn.gt.0.) snrdb=db(pt/pn)
         nsync=count(is13.eq.irxsync)         ! nsync is the number of correct hard-decoded sync tones.
         cand(ncand)%snrdb=snrdb

         if( ichan.eq.0 .and. (nsync .le. 6 .or. snrdb .lt. smin)) cycle
         if( ichan.ne.0 .and. (nsync .le. 8 .or. snrdb .lt. 5.0)) cycle

! looks like a real candidate - try to decode
         call decode_and_merge(ic, decoded_ok)
         if(decoded_ok) call record_ch0_success()
         if(decoded_ok) channel_decoded=.true.
      enddo     ! candidate loop

      if(.not.channel_decoded) then
         ! Sticky-sync retry: nothing decoded this call. If a still-open
         ! slot's continuation frame is due almost exactly one frame
         ! period ago, retry the FEC decode directly at that remembered
         ! sync point instead of giving up on it.
         do ir=1,nslots
            if(slot(ir)%is_last_frame) cycle
            if(slot(ir)%f1.lt.fc-fwid .or. slot(ir)%f1.gt.fc+fwid) cycle
            if(abs(((istart-1)/12000.0 - slot(ir)%tsync) - nframe6/6000.0) &
                 .gt. 0.1) cycle
            if(ncand .ge. MAXCAND) exit
            ncand=ncand+1
            cand(ncand)%xdt=slot(ir)%xdt
            cand(ncand)%f1=slot(ir)%f1
            nsync=-1   ! not meaningful for a sticky-sync retry; flags it in ndebug output
            call decode_and_merge(-1, decoded_ok)
            if(decoded_ok) call record_ch0_success()
            if(decoded_ok) channel_decoded=.true.
            exit   ! at most one retry attempt per channel per call
         enddo
      endif
   end subroutine process_channel

   subroutine record_ch0_success()
      ! Record channel 0's successful-decode neighborhood so Phase B can
      ! avoid it, and decode_and_merge's dedup check can catch it too.
      if(ichan.ne.0) return
      if(n_ch0_ok.ge.16) return
      n_ch0_ok=n_ch0_ok+1
      ja_ch0_ok(n_ch0_ok)=nint(cand(ncand)%f1/df2)-nfz
      jb_ch0_ok(n_ch0_ok)=nint(cand(ncand)%f1/df2)+nfz
      f1_ch0_ok(n_ch0_ok)=cand(ncand)%f1
      tsync_ch0_ok(n_ch0_ok)=cand(ncand)%tsync
   end subroutine record_ch0_success

   subroutine decode_and_merge(ic_label, decoded_ok)
      ! Shared by the candidate loop and the sticky-sync retry: given
      ! cand(ncand)%xdt/%f1, decode the 46 info symbols and merge into
      ! slot(:). ic_label is only for the ndebug print (-1 for a retry).
      integer, intent(in)  :: ic_label
      logical, intent(out) :: decoded_ok

      decoded_ok=.false.
      pow(:,:)=0.0
      do j=1,NCHAN_SYM                  ! find tone powers for 46 symbols
         i0=nint(cand(ncand)%xdt/dt) + NSYNC_SYM*nss + (j-1)*nss
         if(i0+nss .gt. nchunk6) exit

         do i=0,3
            z = dot_product(ctones(0:nss-1,i), c1(i0:i0+nss-1))
            pow(i,j)=real(z*conjg(z))
         enddo

         iloc=maxloc(pow(:,j))-1
         irxchan(j)=iloc(1)   ! hard decision received channel symbols
      enddo

      call tbcc_wava_fsk_decode(pow, JTTY_WAVA_L, JTTY_WAVA_ITERS,             &
           final_payload, success_dec, reserved_zero_bit=JTTY_RESERVED_BIT)
      if(success_dec .and. sum(final_payload).eq.0) success_dec=.false. ! reject all-zero
      nharderrors=-1
      if(success_dec) nharderrors=0
      cand(ncand)%decoded=' '
      if( .not. success_dec ) return

      ndecodes=ndecodes+1
      ! Re-encode the decoded payload to recover the expected tone
      ! per symbol, for the symbol-error-count/SNR diagnostic below
      ! (mirrors what the old LDPC path got for free from its own
      ! codeword bits).
      call tbcc_encode(final_payload, tone_symbols_chk)
      nsymerrs=13-nsync
      do j = 1, NCHAN_SYM
         is=tone_symbols_chk(j)
         if(is.ne.irxchan(j)) nsymerrs=nsymerrs+1
         pt=pt+pow(is,j)
         pa=pa+sum(pow(:,j))
      enddo
      pn=(pa-pt)/3.0
      if(pn.gt.0.) then
         snrdb=db(pt/pn)
         cand(ncand)%snrdb=snrdb
      endif
      write(c32(1),'(34i1)') final_payload
      call unpack_jtty(c32,1,cand(ncand)%decoded,cand(ncand)%trailing_sep,   &
           cand(ncand)%is_last_frame)
      cand(ncand)%tsync=(istart-1)/12000.0 + cand(ncand)%xdt
      decoded_ok=.true.

! dupe detection
      dupe=.false.
      do i=1,ncand-1
         if( cand(i)%decoded .eq. cand(ncand)%decoded .and. &
         abs(cand(i)%tsync - cand(ncand)%tsync).lt. 0.032 ) dupe=.true.
      enddo
      ! A channel-1/2 candidate matching a frame channel 0 already decoded
      ! this call is sync-estimation noise, not a distinct signal. 3.0 Hz
      ! tracks jtty_peakup's coherent-combining frequency precision.
      if(ichan.ne.0) then
         do i=1,n_ch0_ok
            if( abs(cand(ncand)%f1-f1_ch0_ok(i)).lt.3.0 .and. &
                abs(cand(ncand)%tsync-tsync_ch0_ok(i)).lt.0.05 ) dupe=.true.
         enddo
      endif
      if(dupe) return

      ! Subtract this signal from c0 so a second, weaker one underneath can
      ! be found by a follow-up sweep over the residual (see the ipass loop
      ! in jtty_mdecode). tone_symbols_full is the re-encoded, error-
      ! corrected full frame (sync + info), matching what genjtty.f90
      ! assembles for TX.
      tone_symbols_full(1:NSYNC_SYM)=is13
      tone_symbols_full(NSYNC_SYM+1:NFRAME_SYM)=tone_symbols_chk
      call subtract_jtty(c0, nana, nchunk6, tone_symbols_full, NFRAME_SYM,   &
           nss, cand(ncand)%f1, cand(ncand)%xdt)
      any_subtracted=.true.

      if(nsubtracted.lt.MAX_SUBTRACTED) then
         nsubtracted=nsubtracted+1
         subtracted_f1(nsubtracted)=cand(ncand)%f1
         subtracted_tsync(nsubtracted)=cand(ncand)%tsync
         subtracted_payload(:,nsubtracted)=final_payload
      endif

      dec=cand(ncand)
      match=.false.
      islot=1
      if(ndecodes.eq.1) then
         nslots=1
         islot=1
         slot(1)=dec
         ! A "599 ..." frame has no preceding separator; mark one explicitly.
         if(slot(1)%decoded(1:4).eq.'599 ') &
              slot(1)%decoded='~'//trim(slot(1)%decoded)
         slot(1)%nframes_merged=1
         slot(1)%frame_f1(1)=dec%f1
         slot(1)%frame_tsync(1)=dec%tsync
      else
         do i=1,nslots
            ! A slot whose last frame has already been merged in is
            ! closed -- a further decode near its time/frequency is
            ! either a stray/spurious match or the start of a new
            ! message, never a continuation of this one.
            if(slot(i)%is_last_frame) cycle
            df1=dec%f1 - slot(i)%f1
            dxdt=dec%xdt - slot(i)%xdt
            dtsync=dec%tsync - slot(i)%tsync
            ! A continuation frame decoded via an unrefined channel (no
            ! jtty_peakup) can have enough sync-timing noise to miss the
            ! tight local dxdt match even though it's genuinely the next
            ! frame, so also match when the absolute time gap is close to
            ! exactly one frame period (nframe6/6000.0).
            match=abs(df1).lt.3.0 .and.                                       &
                 (abs(dxdt).lt.0.008 .or. abs(dtsync-nframe6/6000.0).lt.0.1)

            ! Neither condition above catches a rediscovery of a frame
            ! merged into this slot several frames ago (e.g. a retro
            ! re-sweep revisiting an earlier window) -- check the slot's
            ! full per-frame history too, same thresholds as above.
            is_history_dupe=.false.
            if(.not.match) then
               do kf=1,slot(i)%nframes_merged
                  if(abs(dec%f1-slot(i)%frame_f1(kf)).lt.3.0 .and. &
                       abs(dec%tsync-slot(i)%frame_tsync(kf)).lt.0.05) then
                     match=.true.
                     is_history_dupe=.true.
                     exit
                  endif
               enddo
            endif

            if(match) then
               islot=i
               if(is_history_dupe .or. abs(dtsync).lt.0.9) then
                  ! Already merged into this slot -- a retro re-sweep can
                  ! rediscover it; don't re-append.
                  exit
               endif
               k=slot(i)%k
               n=len_trim(dec%decoded)
               kz=min(k+n,80)
               ! The prior frame's implicit separator column is just an
               ! untouched blank in slot(i)%decoded, so trim() above
               ! would silently drop it; put it back explicitly.
               if(slot(i)%trailing_sep) then
                  slot(i)%decoded=trim(slot(i)%decoded)//' '//dec%decoded(1:kz-k)
               else
                  slot(i)%decoded=trim(slot(i)%decoded)//dec%decoded(1:kz-k)
               endif
               slot(i)%k=kz
               slot(i)%trailing_sep=dec%trailing_sep
               slot(i)%is_last_frame=dec%is_last_frame
               ! Track the most recently merged frame, not the frame that
               ! opened this slot -- a long message's gradual drift would
               ! otherwise eventually read as "too far from frame 1".
               slot(i)%f1=dec%f1
               slot(i)%xdt=dec%xdt
               slot(i)%tsync=dec%tsync
               if(slot(i)%nframes_merged.lt.16) then
                  slot(i)%nframes_merged=slot(i)%nframes_merged+1
                  slot(i)%frame_f1(slot(i)%nframes_merged)=dec%f1
                  slot(i)%frame_tsync(slot(i)%nframes_merged)=dec%tsync
               endif
               exit
            endif
         enddo
         if(.not.match) then
            if(nslots .ge. MAX_SLOTS) return
            nslots=nslots+1
            slot(nslots)=dec
            islot=nslots
            if(slot(nslots)%decoded(1:4).eq.'599 ') &
                 slot(nslots)%decoded='~'//trim(slot(nslots)%decoded)
            slot(nslots)%nframes_merged=1
            slot(nslots)%frame_f1(1)=dec%f1
            slot(nslots)%frame_tsync(1)=dec%tsync
         endif
      endif
      msg=slot(islot)%decoded
      do i=1,len_trim(msg)
         if(msg(i:i).eq.'~') msg(i:i)=' ' !For display, remove ~ chars
      enddo
      if(msg(1:1).eq.' ') msg=msg(2:)
      if(ndebug.eq.0) then
         write(*,3001) nint(dec%f1),trim(msg)
3001     format(i4,2x,a)
      else if(ndebug.gt.0) then
         write(*,3002) ichan,ipass,ic_label,ndecodes,islot,nslots,match, &
            use_interferer,dec%f1,dec%xdt,dec%tsync,nint(dec%snrdb-20.0), &
            nsync,nsymerrs,nharderrors,trim(msg)
3002     format(6i4,2L3,f7.1,f7.3,f9.3,i5,i4,i4,i4,2x,a)
      endif
   end subroutine decode_and_merge

   end subroutine jtty_mdecode

   subroutine jtty_mdecode_step(iwave,nwave,istart,nchunk,nsps,ndebug,nfa,nfb,f0,ftol,smin)

! Wraps jtty_mdecode with "retro" re-sweeps: after the normal forward call,
! re-run the candidate sweep for up to 3 prior quarter-frame windows for
! every signal this call newly subtracted, since their candidates' frame
! spans could overlap that signal's energy. Not cascaded to further retro
! passes. Both rjtty_sub and rjtty call this instead of jtty_mdecode
! directly, passing the FULL buffer so retro calls can reach backward into it.

      use iso_fortran_env, only: int16
      implicit none
      integer, intent(in)        :: nwave
      integer(int16), intent(in) :: iwave(nwave)
      integer, intent(in)        :: istart, nchunk, nsps, ndebug, nfa, nfb
      real, intent(in)           :: f0, ftol, smin
      integer                    :: n_local, nframe, step, istart_prev, k, i
      real                       :: f1_local(MAX_SUBTRACTED)
      real                       :: tsync_local(MAX_SUBTRACTED)
      integer                    :: payload_local(PAYLOAD_BITS,MAX_SUBTRACTED)

      interferer_pending=.false.   ! defensive: no stale interferer input
      call jtty_mdecode(istart,iwave(istart),nchunk,nsps,ndebug,nfa,nfb, &
           f0,ftol,smin)

! Copy this call's subtraction events out before any retro call below
! overwrites the same module-level output arrays with its own results.
      n_local=nsubtracted
      if(n_local.gt.0) then
         f1_local(1:n_local)=subtracted_f1(1:n_local)
         tsync_local(1:n_local)=subtracted_tsync(1:n_local)
         payload_local(:,1:n_local)=subtracted_payload(:,1:n_local)
      endif

      nframe=59*nsps
      step=nframe/4

      do i=1,n_local
         do k=1,3
            istart_prev=istart-k*step
            if(istart_prev.lt.1) cycle
            interferer_pending=.true.
            interferer_f1=f1_local(i)
            interferer_tsync=tsync_local(i)
            interferer_payload=payload_local(:,i)
            call jtty_mdecode(istart_prev,iwave(istart_prev),nchunk,nsps, &
                 ndebug,nfa,nfb,f0,ftol,smin)
         enddo
      enddo

      return
   end subroutine jtty_mdecode_step

end module jtty_mdec
