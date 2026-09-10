module jtty_mdec

  use iso_fortran_env, only: int64
  use jtty_mod, only: MAX_FRAMES
  use jtty_fec, only: PAYLOAD_BITS, TOTAL_K, tbcc_encode
  use jtty_tbcc_code_profiles, only: jtty_tbcc_code_profile
  use jtty_tbcc_decoder, only: jtty_tbcc_decode
  use jtty_payload_correlators, only: jtty_payload_correlator, &
       jtty_payload_correlator_prepare, jtty_correlate_payload_symbols

  type :: decode
     real :: f1    = 0.0              !Synced audio frequency
     real :: xdt   = 0.0              !Synced DT (0 to 0.5 s)
     real :: tsync = 0.0              !Time of sync from istart=1
     real :: snrdb = 0.0              !SNR of decoded frame
     character(len=80) :: decoded = ''
     logical :: trailing_sep = .false. !decoded ends with an implicit separator column
     logical :: is_last_frame = .false. !this frame had the "last frame of message" bit set
  end type decode

  type :: message_assembly
     integer(int64) :: message_id = 0_int64
     real :: f1 = 0.0
     real :: tsync = 0.0
     real :: start_tsync = 0.0
     integer :: k = 0
     character(len=80) :: decoded = ''
     logical :: trailing_sep = .false.
  end type message_assembly

  type :: frame_fingerprint
     real :: f1 = 0.0
     real :: tsync = 0.0
  end type frame_fingerprint

  type :: message_update
     integer(int64) :: message_id = 0_int64
     real :: f1 = 0.0
     real :: start_tsync = 0.0
     character(len=80) :: decoded = ''
     logical :: complete = .false.
  end type message_update

  integer, parameter        :: MAX_DECODES = 100
  integer, parameter        :: MAX_ACTIVE_MESSAGES = 30
  integer, parameter        :: MAX_RECENT_FRAMES = MAX_ACTIVE_MESSAGES*MAX_FRAMES
  integer, parameter        :: MAX_CONTINUATION_GAP = 3
  integer, parameter        :: MAX_RETRO_STEPS = 3
  real, parameter           :: FRAME_HISTORY_TIME_TOLERANCE = 0.05
  real, parameter           :: FRAME_HISTORY_FREQ_TOLERANCE = 3.0
  real, parameter           :: NEAR_SIMULTANEOUS_FREQ_TOLERANCE = 12.0
  real, parameter           :: CONTINUATION_TIME_TOLERANCE = 0.1
  integer                   :: ndecodes = 0
  integer                   :: nactive = 0
  integer                   :: nrecent = 0
  integer                   :: npending = 0
  integer                   :: pending_first = 1
  integer(int64)            :: next_message_id = 1_int64
  type(message_assembly)    :: active_messages(MAX_ACTIVE_MESSAGES)
  type(frame_fingerprint)   :: recent_frames(MAX_RECENT_FRAMES)
  type(message_update), allocatable :: pending_updates(:)

! Cross-call "retro re-sweep" plumbing (see jtty_mdecode_step): interferer_*
! requests a pre-search subtraction; nsubtracted/subtracted_* report this
! call's own subtractions back out.
  integer, parameter        :: MAX_SUBTRACTED = 16
  logical                   :: interferer_pending = .false.
  real                      :: interferer_f1 = 0.0
  real                      :: interferer_tsync = 0.0
  integer                   :: interferer_payload(PAYLOAD_BITS) = 0
  type(jtty_tbcc_code_profile) :: interferer_code_profile
  integer                   :: nsubtracted = 0
  real                      :: subtracted_f1(MAX_SUBTRACTED) = 0.0
  real                      :: subtracted_tsync(MAX_SUBTRACTED) = 0.0
  integer                   :: subtracted_payload(PAYLOAD_BITS,MAX_SUBTRACTED) = 0
  complex, allocatable, private :: sync_chirp_weights(:),sync_chirp_kernel(:)
  integer, private :: sync_chirp_samples=0,sync_chirp_first_bin=-1
  integer, private :: sync_chirp_output_count=0
  type(jtty_tbcc_code_profile) :: subtracted_code_profile(MAX_SUBTRACTED)

contains

  subroutine reset_decode_search_state()
      nactive=0
      nrecent=0
  end subroutine reset_decode_search_state

  subroutine discard_pending_updates()
      npending=0
      pending_first=1
  end subroutine discard_pending_updates

  pure logical function same_frame(f1_a,tsync_a,f1_b,tsync_b)
      real, intent(in) :: f1_a,tsync_a,f1_b,tsync_b

      same_frame=abs(f1_a-f1_b).lt.FRAME_HISTORY_FREQ_TOLERANCE .and. &
           abs(tsync_a-tsync_b).lt.FRAME_HISTORY_TIME_TOLERANCE
  end function same_frame

  pure logical function same_recent_frame(f1_a,tsync_a,f1_b,tsync_b)
      real, intent(in) :: f1_a,tsync_a,f1_b,tsync_b

      same_recent_frame=abs(f1_a-f1_b).lt.NEAR_SIMULTANEOUS_FREQ_TOLERANCE .and. &
           abs(tsync_a-tsync_b).lt.FRAME_HISTORY_TIME_TOLERANCE
  end function same_recent_frame

  pure function display_message_text(decoded) result(msg)
      character(len=*), intent(in) :: decoded
      character(len=80) :: msg
      integer :: i

      msg=decoded
      do i=1,len_trim(msg)-4
         if(msg(i:i+4).eq.'~~~~~') msg(i:i+4)=' ... '
      enddo
      do i=1,len_trim(msg)
         if(msg(i:i).eq.'~') msg(i:i)=' '
      enddo
      if(msg(1:1).eq.' ') msg=trim(msg(2:))
  end function display_message_text

  logical function is_recent_frame(candidate)
      type(decode), intent(in) :: candidate
      integer :: i

      is_recent_frame=.false.
      do i=1,nrecent
         if(same_recent_frame(candidate%f1,candidate%tsync,recent_frames(i)%f1, &
              recent_frames(i)%tsync)) then
            is_recent_frame=.true.
            return
         endif
      enddo
  end function is_recent_frame

  subroutine remember_recent_frame(candidate)
      type(decode), intent(in) :: candidate

      if(nrecent.ge.MAX_RECENT_FRAMES) then
         ! Duplicate history must never prevent an otherwise valid decode.
         recent_frames(1:MAX_RECENT_FRAMES-1)=recent_frames(2:MAX_RECENT_FRAMES)
      else
         nrecent=nrecent+1
      endif
      recent_frames(nrecent)%f1=candidate%f1
      recent_frames(nrecent)%tsync=candidate%tsync
  end subroutine remember_recent_frame

  subroutine queue_message_update(message,complete)
      type(message_assembly), intent(in) :: message
      logical, intent(in) :: complete
      type(message_update), allocatable :: grown(:)
      integer :: i,index,new_capacity

      do i=0,npending-1
         index=pending_first+i
         if(pending_updates(index)%message_id.eq.message%message_id) then
            pending_updates(index)%f1=message%f1
            pending_updates(index)%decoded=message%decoded
            pending_updates(index)%complete=complete
            return
         endif
      enddo

      if(.not.allocated(pending_updates)) then
         allocate(pending_updates(MAX_ACTIVE_MESSAGES))
      else if(pending_first+npending.gt.size(pending_updates)) then
         if(npending.lt.size(pending_updates)) then
            pending_updates(1:npending)= &
                 pending_updates(pending_first:pending_first+npending-1)
         else
            new_capacity=2*npending
            allocate(grown(new_capacity))
            grown(1:npending)=pending_updates(pending_first:pending_first+npending-1)
            call move_alloc(grown,pending_updates)
         endif
         pending_first=1
      endif
      npending=npending+1
      index=pending_first+npending-1
      pending_updates(index)%message_id=message%message_id
      pending_updates(index)%f1=message%f1
      pending_updates(index)%start_tsync=message%start_tsync
      pending_updates(index)%decoded=message%decoded
      pending_updates(index)%complete=complete
  end subroutine queue_message_update

  subroutine remove_active_message(index)
      integer, intent(in) :: index

      if(index.lt.1 .or. index.gt.nactive) return
      if(index.lt.nactive) active_messages(index)=active_messages(nactive)
      nactive=nactive-1
  end subroutine remove_active_message

  subroutine start_message(candidate,message,accepted)
      type(decode), intent(in) :: candidate
      type(message_assembly), intent(out) :: message
      logical, intent(out) :: accepted

      message=message_assembly()
      accepted=.false.
      if(.not.candidate%is_last_frame .and. nactive.ge.MAX_ACTIVE_MESSAGES) return
      call remember_recent_frame(candidate)

      message%message_id=next_message_id
      message%f1=candidate%f1
      message%tsync=candidate%tsync
      message%start_tsync=candidate%tsync
      message%decoded=candidate%decoded
      if(message%decoded(1:4).eq.'599 ') &
           message%decoded='~'//trim(message%decoded)
      message%k=len_trim(message%decoded)
      message%trailing_sep=candidate%trailing_sep

      next_message_id=next_message_id+1_int64
      call queue_message_update(message,candidate%is_last_frame)
      if(.not.candidate%is_last_frame) then
         nactive=nactive+1
         active_messages(nactive)=message
      endif
      accepted=.true.
  end subroutine start_message

  subroutine append_active_message(index,candidate,nframes_gap,message,accepted)
      integer, intent(in) :: index,nframes_gap
      type(decode), intent(in) :: candidate
      type(message_assembly), intent(out) :: message
      logical, intent(out) :: accepted
      integer :: k,kz,n,nchar,nstart

      message=message_assembly()
      accepted=.false.
      if(index.lt.1 .or. index.gt.nactive) return
      call remember_recent_frame(candidate)

      k=active_messages(index)%k
      n=len_trim(candidate%decoded)
      if(nframes_gap.gt.1) then
         nstart=1
         if(n.ge.1) then
            if(candidate%decoded(1:1).eq.'~') nstart=2
         endif
         kz=min(k+5+(n-nstart+1),80)
         nchar=max(kz-k-5,0)
         active_messages(index)%decoded=trim(active_messages(index)%decoded)// &
              '~~~~~'//candidate%decoded(nstart:nstart+nchar-1)
         active_messages(index)%k=k+5+nchar
      else
         kz=min(k+n,80)
         if(active_messages(index)%trailing_sep) then
            active_messages(index)%decoded=trim(active_messages(index)%decoded)// &
                 ' '//candidate%decoded(1:kz-k)
         else
            active_messages(index)%decoded=trim(active_messages(index)%decoded)// &
                 candidate%decoded(1:kz-k)
         endif
         active_messages(index)%k=kz
      endif
      active_messages(index)%trailing_sep=candidate%trailing_sep
      active_messages(index)%f1=candidate%f1
      active_messages(index)%tsync=candidate%tsync
      call queue_message_update(active_messages(index),candidate%is_last_frame)
      message=active_messages(index)
      if(candidate%is_last_frame) call remove_active_message(index)
      accepted=.true.
  end subroutine append_active_message

  subroutine prune_receive_state(forward_tsync,frame_period)
      real, intent(in) :: forward_tsync,frame_period
      real :: oldest_revisit
      integer :: i,keep

      oldest_revisit=forward_tsync-real(MAX_RETRO_STEPS)*frame_period/4.0
      ! Retro candidates move backward in time, so only the forward watermark may expire history.
      keep=0
      do i=1,nrecent
         if(recent_frames(i)%tsync.lt. &
              oldest_revisit-FRAME_HISTORY_TIME_TOLERANCE) cycle
         keep=keep+1
         if(keep.ne.i) recent_frames(keep)=recent_frames(i)
      enddo
      nrecent=keep

      i=1
      do while(i.le.nactive)
         if(oldest_revisit-active_messages(i)%tsync.gt. &
              real(MAX_CONTINUATION_GAP)*frame_period+ &
              CONTINUATION_TIME_TOLERANCE) then
            call queue_message_update(active_messages(i),.false.)
            call remove_active_message(i)
         else
            i=i+1
         endif
      enddo
  end subroutine prune_receive_state
  subroutine jtty_tbcc_reencode_for_subtraction(payload, code_profile, tones)
      integer, intent(in) :: payload(PAYLOAD_BITS)
      type(jtty_tbcc_code_profile), intent(in) :: code_profile
      integer, intent(out) :: tones(TOTAL_K)

      ! Queued subtraction uses the profile that admitted the payload.
      call tbcc_encode(payload, tones, code_profile)
  end subroutine jtty_tbcc_reencode_for_subtraction

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

   pure subroutine classify_active_candidate(existing,candidate,frame_period, &
       match,is_window_dupe,nframes_gap)
      type(message_assembly), intent(in) :: existing
      type(decode), intent(in) :: candidate
      real, intent(in) :: frame_period
      logical, intent(out) :: match,is_window_dupe
      integer, intent(out) :: nframes_gap
      real :: df1,dtsync,qstep,resid,fp_resid,df_tol
      integer :: nstep,nfp

      df1=candidate%f1-existing%f1
      dtsync=candidate%tsync-existing%tsync
      match=.false.
      nframes_gap=1
      nfp=nint(dtsync/frame_period)
      fp_resid=abs(dtsync-frame_period*nfp)
      if(nfp.ge.1 .and. nfp.le.MAX_CONTINUATION_GAP .and. &
           fp_resid.lt.CONTINUATION_TIME_TOLERANCE) then
         df_tol=10.0+3.0*real(nfp-1)
         match=abs(df1).lt.df_tol
         if(match) nframes_gap=nfp
      endif

      ! Retro sweeps revisit only the preceding three quarter-frame windows.
      is_window_dupe=.false.
      if(.not.match) then
         qstep=frame_period/4.0
         nstep=nint(dtsync/qstep)
         resid=abs(dtsync-qstep*nstep)
         if(abs(nstep).le.MAX_RETRO_STEPS .and. abs(df1).lt.10.0 .and. &
              resid.lt.0.003 .and. &
              .not.(mod(abs(nstep),4).eq.0 .and. nstep.ne.0)) then
            match=.true.
            is_window_dupe=.true.
         endif
      endif

   end subroutine classify_active_candidate

  subroutine jtty_mdecode(istart,istart0,iwave,nchunk,nsps,ndebug,nfa,nfb,f0,ftol,smin)

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
      integer, intent(in)            :: istart, istart0, ndebug
      integer                        :: i,i0,is,j,ja,jb
      integer, save                  :: ntstep, ntgrid
      integer                        :: istep,first_sync_bin,last_sync_bin
      integer                        :: nchan, ichan
      integer, intent(in)            :: nchunk,nsps   !size of chunk, nsps at 12000 Sa/s
      integer, intent(in)            :: nfa,nfb       !Wide Graph freq range
      integer                        :: nchunk6,nana  !size of chunk, nana at 6000 Sa/s
      integer, save                  :: nframe6       !size of frame at 6000 Sa/s
      integer, save                  :: nsps0=-999
      type(jtty_tbcc_code_profile)   :: code_profile
      type(jtty_payload_correlator), save :: payload_correlator
      integer, save                  :: nfft,nh2,nss
      integer                        :: iloc(1)
      integer                        :: irxsync(NSYNC_SYM), irxchan(NCHAN_SYM)
      integer                        :: iactive
      integer                        :: nsloc(2),nfz,ntz,ncand,ic,nc,nstep_search
      integer                        :: nc0,n_ch0_ok
      integer                        :: ja_ch0_ok(16),jb_ch0_ok(16)
      real                            :: f1_ch0_ok(16),tsync_ch0_ok(16)
      integer                        :: nsync,nsymerrs
      real                           :: fc,fwid
      real                           :: fpk,pa,pt,pn
      real                           :: fbest,xdtbest
      real                           :: xdt_retry
      real, allocatable, save        :: s0(:,:)
      logical, allocatable, save     :: mask0(:,:)
      real                           :: a(3)
      real                           :: pow(0:3,NCHAN_SYM)
      real, save                     :: baud,dt,df2
      real                           :: phi,dphi
      real, external                 :: db
      real, intent(in)               :: f0,ftol,smin
      real                           :: snrdb, xdt
      real                           :: xdt1, f11, snr0
      complex, allocatable,save      :: c(:)
      complex, allocatable,save      :: c0(:)
      complex, allocatable,save      :: c1(:)
      complex, allocatable,save      :: csync(:)    !Waveform for sync at 6000 s^-1 sample rate
      complex, allocatable,save      :: ctones(:,:)
      complex                        :: z
      logical                        :: match
      logical                        :: dupe
      logical                        :: usable
      logical                        :: is_window_dupe
      logical                        :: is_pure_dupe
      logical                        :: success_dec
      logical                        :: channel_decoded, decoded_ok
      logical                        :: any_subtracted
      logical                        :: s0_valid
      integer                        :: ir
      type(decode)                   :: cand(MAXCAND)     !Candidates for decoding
      type(decode)                   :: dec               !Current successful decode
      logical                        :: use_interferer
      real                            :: use_interferer_f1, use_interferer_tsync
      integer                         :: use_interferer_payload(PAYLOAD_BITS)
      type(jtty_tbcc_code_profile) :: use_interferer_code_profile

! Capture and clear the retro-resweep interferer request (if any) as the
! very first thing this call does, before any possible early return below
! -- otherwise a stale request could leak into a later, unrelated call.
      use_interferer=interferer_pending
      use_interferer_f1=interferer_f1
      use_interferer_tsync=interferer_tsync
      use_interferer_payload=interferer_payload
      use_interferer_code_profile=interferer_code_profile
      interferer_pending=.false.
      nsubtracted=0

      nsync=0

      call jtty_tbcc_get_code_profile(code_profile)

      if(istart.eq.istart0 .and. .not.use_interferer) then
         ndecodes=0
         call reset_decode_search_state()
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
         if(allocated(s0)) deallocate(s0)
           allocate(s0(0:nh2,0:ntgrid))
         s0=0.
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

      call jtty_payload_correlator_prepare(payload_correlator,nss)

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
         call jtty_tbcc_reencode_for_subtraction(use_interferer_payload, &
              use_interferer_code_profile, tone_symbols_chk)
         tone_symbols_full(NSYNC_SYM+1:NFRAME_SYM)=tone_symbols_chk
         call subtract_jtty(c0, nana, nchunk6, tone_symbols_full, NFRAME_SYM, &
              nss, use_interferer_f1, use_interferer_tsync-(istart-1)/12000.0)
      endif

! Look for up to 2 sync candidates in each quarter-frame (0.424 second) by 2*FTol rectangle in
! the time/frequency plane. Find the peak in the search rectangle, then zero a small region
! of size nfz by ntz centered on the peak location and find the location of the next peak.

      nfz=nint(10.0/df2)            ! 14
      ntz=nint(0.016*6000.0/12.0)   !  8

      ! Every channel reads inside this span; unsearched bins may remain stale.
      nchan=2
      first_sync_bin=nh2-2
      last_sync_bin=3
      do ichan=0,nchan
         call channel_window()
         if(.not.usable) cycle
         first_sync_bin=min(first_sync_bin,ja)
         last_sync_bin=max(last_sync_bin,jb)
      enddo

      nc=2          ! look for 2 candidates in each channel
      ncand=0
      any_subtracted=.false.
      s0_valid=.false.

      ! Phase A: channel 0 gets first claim on every signal -- runs its own
      ! full up-to-2-pass sweep to completion, accumulating n_ch0_ok across
      ! both passes, before channels 1/2 (Phase B) ever run.
      n_ch0_ok=0
      do ipass=1,2
         if(ipass.eq.2 .and. .not.any_subtracted) exit
         if(.not.s0_valid) call build_s0()
         ichan=0
         call process_channel()
      enddo

      ! Channel 0 masks candidates privately, so an unchanged surface can
      ! also serve channels 1/2. Their suppression modifies s0 directly.
      any_subtracted=.false.
      do ipass=1,2
         if(ipass.eq.2 .and. .not.any_subtracted) exit
         if(.not.s0_valid) call build_s0()
         do ichan=1,nchan
            call process_channel()
         enddo
         s0_valid=.false.
      enddo

      return

   contains

   subroutine build_s0()
      use fftw3, only: c_ptr,c_null_ptr,c_associated,fftwf_plan_dft_1d, &
           fftwf_execute_dft,fftwf_destroy_plan,FFTW_FORWARD,FFTW_BACKWARD, &
           FFTW_ESTIMATE,FFTW_MEASURE,FFTW_PRESERVE_INPUT
      real(8), parameter :: CHIRP_TWOPI=6.2831853071795864769d0
      real(8) :: phase
      real :: p0,p1,p2,p3,p4,chirp_scale
      complex :: fft_input(0:nfft-1)
      complex :: chirp_output(0:nfft/2-1),cz
      type(c_ptr) :: fft_plan,chirp_forward,chirp_backward,kernel_plan
      integer :: npatience,nthreads,chirp_nfft,output_count,convolution_length
      integer :: index,distance,first_output_bin
      logical :: use_chirp
      common/patience/npatience,nthreads
      ! Rebuild s0, the FFT-correlation sync-search surface, from the
      ! current c0 (may already reflect earlier-phase subtractions).
      fft_plan=c_null_ptr
      chirp_forward=c_null_ptr
      chirp_backward=c_null_ptr
      kernel_plan=c_null_ptr
      first_output_bin=first_sync_bin-2
      output_count=last_sync_bin-first_sync_bin+5
      convolution_length=NSYNC_SYM*nss+output_count-1
      chirp_nfft=1
      do while(chirp_nfft.lt.convolution_length)
         chirp_nfft=2*chirp_nfft
      enddo
      use_chirp=output_count.gt.0 .and. npatience.eq.0 .and. &
           chirp_nfft.lt.nfft .and. chirp_nfft.le.nfft/2

      if(use_chirp .and. (sync_chirp_samples.ne.NSYNC_SYM*nss .or. &
           sync_chirp_first_bin.ne.first_output_bin .or. &
           sync_chirp_output_count.ne.output_count)) then
         if(allocated(sync_chirp_weights)) deallocate(sync_chirp_weights)
         if(allocated(sync_chirp_kernel)) deallocate(sync_chirp_kernel)
         allocate(sync_chirp_weights(0:NSYNC_SYM*nss-1))
         allocate(sync_chirp_kernel(0:chirp_nfft-1))
         do index=0,NSYNC_SYM*nss-1
            phase=-CHIRP_TWOPI*(dble(first_output_bin)*dble(index)+ &
                 0.5d0*dble(index)*dble(index))/dble(nfft)
            sync_chirp_weights(index)=conjg(csync(index))* &
                 cmplx(cos(phase),sin(phase))
         enddo
         fft_input(0:chirp_nfft-1)=0.
         do distance=0,max(NSYNC_SYM*nss,output_count)-1
            phase=0.5d0*CHIRP_TWOPI*dble(distance)*dble(distance)/dble(nfft)
            if(distance.lt.output_count) &
                 fft_input(distance)=cmplx(cos(phase),sin(phase))
            if(distance.gt.0 .and. distance.lt.NSYNC_SYM*nss) &
                 fft_input(chirp_nfft-distance)=cmplx(cos(phase),sin(phase))
         enddo
         !$omp critical(fftw)
         kernel_plan=fftwf_plan_dft_1d(chirp_nfft,fft_input,c, &
              FFTW_FORWARD,FFTW_ESTIMATE)
         !$omp end critical(fftw)
         if(c_associated(kernel_plan)) then
            call fftwf_execute_dft(kernel_plan,fft_input,c)
            sync_chirp_kernel=c(0:chirp_nfft-1)
            !$omp critical(fftw)
            call fftwf_destroy_plan(kernel_plan)
            !$omp end critical(fftw)
            sync_chirp_samples=NSYNC_SYM*nss
            sync_chirp_first_bin=first_output_bin
            sync_chirp_output_count=output_count
         else
            use_chirp=.false.
         endif
      endif

      if(use_chirp) then
         !$omp critical(fftw)
         chirp_forward=fftwf_plan_dft_1d(chirp_nfft,fft_input,c, &
              FFTW_FORWARD,ior(FFTW_MEASURE,FFTW_PRESERVE_INPUT))
         chirp_backward=fftwf_plan_dft_1d(chirp_nfft,c,chirp_output, &
              FFTW_BACKWARD,FFTW_MEASURE)
         !$omp end critical(fftw)
         use_chirp=c_associated(chirp_forward) .and. &
              c_associated(chirp_backward)
      endif

      ! Explicit planner settings and wider searches retain the full FFT path.
      if(npatience.eq.0 .and. .not.use_chirp) then
         !$omp critical(fftw)
         fft_plan=fftwf_plan_dft_1d(nfft,fft_input,c,FFTW_FORWARD, &
              ior(FFTW_MEASURE,FFTW_PRESERVE_INPUT))
         !$omp end critical(fftw)
      endif
      if(use_chirp) then
         fft_input(NSYNC_SYM*nss:chirp_nfft-1)=0.
         chirp_scale=1.0/real(chirp_nfft)
      else
         ! Preserving the input keeps this padding intact for every time column.
         fft_input(NSYNC_SYM*nss:)=0.
      endif
      istep=0
      do i0=0,ntstep,12                     !Search over quarter-frame segment
         xdt=i0*dt
         if(use_chirp) then
            fft_input(0:NSYNC_SYM*nss-1)=sync_chirp_weights* &
                 c0(i0:i0+NSYNC_SYM*nss-1)
            call fftwf_execute_dft(chirp_forward,fft_input,c)
            c(0:chirp_nfft-1)=c(0:chirp_nfft-1)*sync_chirp_kernel
            call fftwf_execute_dft(chirp_backward,c,chirp_output)
            ! Bluestein's omitted output chirp has unit magnitude.
            cz=chirp_output(0)*chirp_scale
            p0=real(cz)**2+aimag(cz)**2
            cz=chirp_output(1)*chirp_scale
            p1=real(cz)**2+aimag(cz)**2
            cz=chirp_output(2)*chirp_scale
            p2=real(cz)**2+aimag(cz)**2
            cz=chirp_output(3)*chirp_scale
            p3=real(cz)**2+aimag(cz)**2
            do j=first_sync_bin,last_sync_bin
               cz=chirp_output(j-first_sync_bin+4)*chirp_scale
               p4=real(cz)**2+aimag(cz)**2
               s0(j,istep)=p0+2*p1+3*p2+2*p3+p4
               p0=p1
               p1=p2
               p2=p3
               p3=p4
            enddo
         else
            fft_input(0:NSYNC_SYM*nss-1)=conjg(csync(0:NSYNC_SYM*nss-1))* &
                 c0(i0:i0+NSYNC_SYM*nss-1)
            if(c_associated(fft_plan)) then
               call fftwf_execute_dft(fft_plan,fft_input,c)
            else
               c=fft_input
               call four2a(c,nfft,1,-1,1)
            endif
            ! Keep the two-bin halo while advancing the five-bin smoothing kernel.
            p0=real(c(first_sync_bin-2))**2 + aimag(c(first_sync_bin-2))**2
            p1=real(c(first_sync_bin-1))**2 + aimag(c(first_sync_bin-1))**2
            p2=real(c(first_sync_bin))**2 + aimag(c(first_sync_bin))**2
            p3=real(c(first_sync_bin+1))**2 + aimag(c(first_sync_bin+1))**2
            do j=first_sync_bin,last_sync_bin
               p4=real(c(j+2))**2 + aimag(c(j+2))**2
               s0(j,istep)=p0+2*p1+3*p2+2*p3+p4
               p0=p1
               p1=p2
               p2=p3
               p3=p4
            enddo
         endif
         istep=istep+1
      enddo
      if(c_associated(chirp_forward)) then
         !$omp critical(fftw)
         call fftwf_destroy_plan(chirp_forward)
         !$omp end critical(fftw)
      endif
      if(c_associated(chirp_backward)) then
         !$omp critical(fftw)
         call fftwf_destroy_plan(chirp_backward)
         !$omp end critical(fftw)
      endif
      if(c_associated(fft_plan)) then
         !$omp critical(fftw)
         call fftwf_destroy_plan(fft_plan)
         !$omp end critical(fftw)
      endif
      nstep_search=istep-1
      s0_valid=.true.
   end subroutine build_s0

   subroutine channel_window()
      if(ichan.eq.0) then
         fc=f0
         fwid=ftol
      else
         fc=1350
         if(ichan.eq.2) fc=1650
         fwid=150
      endif
      call jtty_search_window(fc,fwid,nfa,nfb,ichan.ne.0,df2,3, &
           ubound(s0,1)-2,ja,jb,usable)
   end subroutine channel_window

   subroutine process_channel()
      ! One channel's candidate search plus sticky-sync retry, for the
      ! host's current ichan/ipass (host-associated with jtty_mdecode).
      call channel_window()
      if(.not.usable) return
      nc0=nc
      ! A wide QSO band needs more candidates to avoid crowding out its signal.
      if(ichan.eq.0) nc0=max(2, min(8, nint(fwid/(nfz*df2))))
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
         ! Sticky-sync retry: nothing decoded this call. If an active
         ! message's continuation frame is due almost exactly one frame
         ! period ago, retry the FEC decode directly at that remembered
         ! sync point instead of giving up on it.
         do ir=1,nactive
            if(active_messages(ir)%f1.lt.fc-fwid .or. &
                 active_messages(ir)%f1.gt.fc+fwid) cycle
            if(abs(((istart-1)/12000.0 - active_messages(ir)%tsync) - &
                 nframe6/6000.0) &
                 .gt. 0.1) cycle
            ! Derive the local offset from absolute sync time because the
            ! assembly may have been updated from a different decode window.
            xdt_retry=active_messages(ir)%tsync + nframe6/6000.0 - &
                 (istart-1)/12000.0
            if(xdt_retry.lt.0.0) cycle
            if(ncand .ge. MAXCAND) exit
            ncand=ncand+1
            cand(ncand)%xdt=xdt_retry
            cand(ncand)%f1=active_messages(ir)%f1
            ! decode_and_merge reads tone powers from c1, which is only
            ! valid for whichever frequency the blind-candidate loop
            ! above last shifted it to -- re-shift it for this retry's
            ! own frequency, or it silently correlates against the wrong
            ! signal (dchristle, PR #337).
            a=0.
            a(1)=-cand(ncand)%f1
            call twkfreq(c0,c1,nchunk6,6000.0,a)
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
      ! active_messages(:). ic_label is only for the ndebug print (-1 for a retry).
      integer, intent(in)  :: ic_label
      logical, intent(out) :: decoded_ok
      complex               :: zsym(0:3,NCHAN_SYM)
      complex               :: zhalf(0:3,NCHAN_SYM)
      integer               :: payload_start
      integer               :: best_cont
      real                  :: best_df,dfabs
      logical               :: have_win,accepted,source_valid
      integer               :: gap,best_gap
      type(message_assembly) :: accepted_message

      decoded_ok=.false.
      payload_start=nint(cand(ncand)%xdt/dt) + NSYNC_SYM*nss
      call jtty_correlate_payload_symbols(payload_correlator,c1,payload_start,zsym,zhalf)
      call jtty_tbcc_decode(zsym,zhalf,final_payload,success_dec,code_profile=code_profile)
      ! Half-symbol off-tone leakage is not a noise estimate; diagnostics use M1.
      pow=abs(zsym)**2
      do j=1,NCHAN_SYM
         iloc=maxloc(pow(:,j))-1
         irxchan(j)=iloc(1)
      enddo
      cand(ncand)%decoded=' '
      if( .not. success_dec ) return

      write(c32(1),'(34i1)') final_payload
      call unpack_jtty(c32,1,cand(ncand)%decoded,cand(ncand)%trailing_sep,   &
           cand(ncand)%is_last_frame,source_valid)
      if(.not.source_valid) return

      ndecodes=ndecodes+1
      ! Re-encode the decoded payload to recover the expected tone
      ! per symbol, for the symbol-error-count/SNR diagnostic below
      ! (mirrors what the old LDPC path got for free from its own
      ! codeword bits).
      call jtty_tbcc_reencode_for_subtraction(final_payload, code_profile, tone_symbols_chk)
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
            if(same_frame(cand(ncand)%f1,cand(ncand)%tsync, &
                 f1_ch0_ok(i),tsync_ch0_ok(i))) dupe=.true.
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
      s0_valid=.false.

      if(nsubtracted.lt.MAX_SUBTRACTED) then
         nsubtracted=nsubtracted+1
         subtracted_f1(nsubtracted)=cand(ncand)%f1
         subtracted_tsync(nsubtracted)=cand(ncand)%tsync
         subtracted_payload(:,nsubtracted)=final_payload
         subtracted_code_profile(nsubtracted)=code_profile
      endif

      dec=cand(ncand)
      match=.false.
      is_pure_dupe=is_recent_frame(dec)
      iactive=0
      if(.not.is_pure_dupe .and. nactive.gt.0) then
         have_win=.false.
         best_cont=0
         best_df=1.0e30
         best_gap=1
         do i=1,nactive
            call classify_active_candidate(active_messages(i),dec, &
                 nframe6/6000.0,match,is_window_dupe,gap)
            if(.not.match) cycle
            if(is_window_dupe) then
               if(.not.have_win) iactive=i
               have_win=.true.
               cycle
            endif
            dfabs=abs(dec%f1-active_messages(i)%f1)
            if(dfabs.lt.best_df) then
               best_df=dfabs
               best_cont=i
               best_gap=gap
            endif
         enddo
         if(have_win) then
            match=.true.
            is_pure_dupe=.true.
         else if(best_cont.gt.0) then
            match=.true.
            iactive=best_cont
         else
            match=.false.
         endif
      endif

      if(.not.is_pure_dupe .and. match) then
         call append_active_message(iactive,dec,best_gap,accepted_message,accepted)
         if(.not.accepted) return
         msg=accepted_message%decoded
         if(dec%is_last_frame) iactive=0
      else if(.not.is_pure_dupe) then
         call start_message(dec,accepted_message,accepted)
         if(.not.accepted) return
         msg=accepted_message%decoded
         if(dec%is_last_frame) then
            iactive=0
         else
            iactive=nactive
         endif
      else
         msg=dec%decoded
      endif
      msg=display_message_text(msg)
      if(ndebug.eq.0) then
         if(.not.is_pure_dupe) write(*,3001) nint(dec%f1),trim(msg)
3001     format(i4,2x,a)
      else if(ndebug.gt.0) then
         write(*,3002) ichan,ipass,ic_label,ndecodes,iactive,nactive,match, &
            use_interferer,dec%f1,dec%xdt,dec%tsync,nint(dec%snrdb-20.0), &
            nsync,nsymerrs,trim(msg)
3002     format(6i4,2L3,f7.1,f7.3,f9.3,i5,i4,i4,2x,a)
      endif
   end subroutine decode_and_merge

   end subroutine jtty_mdecode

   subroutine jtty_mdecode_step(iwave,nwave,istart,istart0,nchunk,nsps,ndebug,nfa,nfb,f0,ftol,smin)

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
      integer, intent(in)        :: istart, istart0, nchunk, nsps, ndebug, nfa, nfb
      real, intent(in)           :: f0, ftol, smin
      integer                    :: n_local, nframe, step, istart_prev, k, i
      real                       :: f1_local(MAX_SUBTRACTED)
      real                       :: tsync_local(MAX_SUBTRACTED)
      integer                    :: payload_local(PAYLOAD_BITS,MAX_SUBTRACTED)
      type(jtty_tbcc_code_profile) :: code_profile_local(MAX_SUBTRACTED)

      nframe=59*nsps
      step=nframe/4
      call prune_receive_state((istart-1)/12000.0,nframe/12000.0)

      interferer_pending=.false.   ! defensive: no stale interferer input
      call jtty_mdecode(istart,istart0,iwave(istart),nchunk,nsps,ndebug,nfa,nfb, &
           f0,ftol,smin)

! Copy this call's subtraction events out before any retro call below
! overwrites the same module-level output arrays with its own results.
      n_local=nsubtracted
      if(n_local.gt.0) then
         f1_local(1:n_local)=subtracted_f1(1:n_local)
         tsync_local(1:n_local)=subtracted_tsync(1:n_local)
         payload_local(:,1:n_local)=subtracted_payload(:,1:n_local)
         code_profile_local(1:n_local)=subtracted_code_profile(1:n_local)
      endif

      do i=1,n_local
         do k=1,MAX_RETRO_STEPS
            istart_prev=istart-k*step
            if(istart_prev.lt.1) cycle
            interferer_pending=.true.
            interferer_f1=f1_local(i)
            interferer_tsync=tsync_local(i)
            interferer_payload=payload_local(:,i)
            interferer_code_profile=code_profile_local(i)
            call jtty_mdecode(istart_prev,istart0,iwave(istart_prev),nchunk,nsps, &
                 ndebug,nfa,nfb,f0,ftol,smin)
         enddo
      enddo

      return
   end subroutine jtty_mdecode_step

end module jtty_mdec
