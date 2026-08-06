module jtty_mdec

  type :: decode
     real :: f1    = 0.0              !Synced audio frequency
     real :: xdt   = 0.0              !Synced DT (0 to 0.5 s)
     real :: tsync = 0.0              !Time of sync from istart=1
     real :: snrdb = 0.0              !SNR of decoded frame
     integer ::  k = 0                !Accumulated length of decoded text
     character(len=80) :: decoded = ''
     logical :: trailing_sep = .false. !decoded ends with an implicit separator column
     logical :: is_last_frame = .false. !this frame had the "last frame of message" bit set
  end type decode

  integer, parameter        :: MAX_DECODES = 100
  integer, parameter        :: MAX_SLOTS = 30
  integer                   :: ndecodes = 0
  integer                   :: nslots = 0
  type(decode)              :: slot(MAX_SLOTS)   !Accumulating decode messages

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
      integer                        :: nharderrors,nsync,nsymerrs
      real                           :: fc,fwid
      real                           :: fpk,pa,pt,pn
      real                           :: fbest,xdtbest
      real, allocatable, save        :: s(:), s0(:,:)
      real                           :: a(3)
      real                           :: pow(0:3,NCHAN_SYM)
      real, save                     :: baud,dt,df2
      real                           :: phi,dphi
      real                           :: db
      real, intent(in)               :: f0,ftol,smin
      real                           :: dmin
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
      logical                        :: success_dec
      logical                        :: channel_decoded, decoded_ok
      logical                        :: any_subtracted
      integer                        :: ir
      type(decode)                   :: cand(MAXCAND)     !Candidates for decoding
      type(decode)                   :: dec               !Current successful decode

      nharderrors=-1
      nsync=0
      dmin=0.0

      if(nu0.ne.JTTY_WAVA_NU) then
         nu0=JTTY_WAVA_NU
         call tbcc_init(JTTY_WAVA_NU)
      endif

      if(istart.eq.1) then
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

! Look for up to 2 sync candidates in each quarter-frame (0.424 second) by 2*FTol rectangle in
! the time/frequency plane. Find the peak in the search rectangle, then zero a small region
! of size nfz by ntz centered on the peak location and find the location of the next peak.

      nfz=nint(10.0/df2)            ! 14
      ntz=nint(0.016*6000.0/12.0)   !  8

      nchan = 2
      nc=2          ! look for 2 candidates in each channel
      ncand=0
      any_subtracted=.false.

      ! Two-pass sweep: pass 1 is the normal blind search/decode. Every
      ! successful decode is subtracted from c0 (see decode_and_merge) so a
      ! second, weaker signal masked by it can be found underneath. Pass 2
      ! repeats the identical search on the now-subtracted c0, once, and
      ! only runs at all if pass 1 actually subtracted something.
      do ipass=1,2
      if(ipass.eq.2 .and. .not.any_subtracted) exit

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

      do ichan=0, nchan         ! frequency channels - channel 0 is always centered on f0
         if(ichan.eq.0) then
            fc=f0
            fwid=ftol
         else            ! for now, hardwired nonoverlapping channels
            fc=1350
            if(ichan.eq.2) fc=1650
            fwid=150
         endif

         call jtty_search_window(fc,fwid,nfa,nfb,ichan.ne.0,df2,3, &
              ubound(s0,1)-2,ja,jb,usable)
         if(.not.usable) cycle
         fbest=0.
         xdtbest=0.
         fpk=0.
         channel_decoded=.false.

         do ic=1,nc
            nsloc=maxloc(s0(ja:jb,0:nstep_search))
            fbest   = (nsloc(1)-1+ja)*df2
            xdtbest = (nsloc(2)-1)*dt*12
            s0( max( ja, nsloc(1)-nfz+ja ) : min( jb, nsloc(1)+nfz+ja  ),        &
                max(  0, nsloc(2)-ntz )    : min( nstep_search, nsloc(2)+ntz )   ) = 0.0

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
            if(decoded_ok) channel_decoded=.true.
         enddo     ! candidate loop

         if(.not.channel_decoded) then
            ! Sticky-sync retry: nothing decoded in this channel's blind
            ! search this call. If a still-open (EOM not yet seen) decode
            ! from this same channel's frequency window landed almost
            ! exactly one frame duration ago, its continuation frame's own
            ! sync may be too weak to pass the blind search above -- retry
            ! the FEC decode directly at that remembered sync point (no
            ! fresh sync-symbol search or nsync/snrdb gate) instead of
            ! giving up on it. slot(:) is the "memory of prior decodes".
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
               if(decoded_ok) channel_decoded=.true.
               exit   ! at most one retry attempt per channel per call
            enddo
         endif
      enddo     ! ichan, frequency channel loop

      enddo     ! ipass -- normal sweep, then one post-subtraction re-sweep

      return

   contains

   subroutine decode_and_merge(ic_label, decoded_ok)
      ! Shared by the normal per-candidate path and the sticky-sync retry:
      ! given cand(ncand)%xdt/%f1 already set by the caller, compute tone
      ! powers for the 46 info symbols, WAVA-decode, and (on success) merge
      ! the result into slot(:) exactly like any other successful decode.
      ! ic_label is only for the ndebug>0 print -- pass -1 for a retry,
      ! since it wasn't drawn from this call's ic candidate loop.
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

      dmin=0.0
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

      dec=cand(ncand)
      match=.false.
      islot=1
      if(ndecodes.eq.1) then
         nslots=1
         islot=1
         slot(1)=dec
         ! A "599 ..." frame opening a slot has no preceding structured
         ! frame to supply a separator, so mark one explicitly here.
         if(slot(1)%decoded(1:4).eq.'599 ') &
              slot(1)%decoded='~'//trim(slot(1)%decoded)
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
            match=abs(df1).lt.8.0 .and. abs(dxdt).lt.0.008
            if(match) then
               islot=i
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
               exit
            endif
         enddo
         if(.not.match) then
            if(nslots .ge. MAX_SLOTS) return
            nslots=nslots+1
            slot(nslots)=dec
            islot=nslots
            ! Same as above: a fresh slot starting with "599" has no
            ! preceding separator, so mark one explicitly.
            if(slot(nslots)%decoded(1:4).eq.'599 ') &
                 slot(nslots)%decoded='~'//trim(slot(nslots)%decoded)
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
         write(*,3002) ichan,ic_label,ndecodes,islot,nslots,match,dec%f1, &
            dec%xdt,dec%tsync,nint(dec%snrdb-20.0),nsync,nsymerrs,nharderrors,dmin,trim(msg)
3002     format(5i4,L3,f7.1,f7.3,f9.3,i5,i4,i4,i4,f6.1,2x,a)
      endif
   end subroutine decode_and_merge

   end subroutine jtty_mdecode

end module jtty_mdec
