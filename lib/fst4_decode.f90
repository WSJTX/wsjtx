module fst4_decode
   use fst4_osd_workspace, only: fst4_osd_workspace_type
   use fst4_ldpc_74, only: decode240_74_owned
   use fst4_ldpc_101, only: decode240_101_owned
   use fst4_wavegen, only: fst4_wavegen_workspace,gen_fst4wave_owned
   use fst4_baseline_workspace, only: fst4_baseline_owned
   use fst4_bitmetrics, only: fst4_bitmetrics_workspace,get_fst4_bitmetrics_owned
   use decoder_engine_types, only: fst4_result
   use packjt77, only: pack77_state,initialize_pack77_state,reset_pack77_state, &
      pack77_for_state,unpack77_for_state
   use, intrinsic :: iso_c_binding
   use fftw3, only: fftwf_alloc_complex,fftwf_free,fftwf_plan_dft_r2c_1d,fftwf_plan_dft_1d, &
      fftwf_destroy_plan,fftwf_execute_dft_r2c,fftwf_execute_dft,FFTW_ESTIMATE,FFTW_FORWARD,FFTW_BACKWARD
   private
   public :: fst4_decoder,fst4_options,fst4_decode_callback,fst4_diagnostic_callback,fst4_spectrum_callback

   type fst4_options
      integer :: utc=0,period=15,receive_frequency=1500,low_frequency=200,high_frequency=4000
      integer :: tolerance=100,effort=1,qso_progress=0,blanker_percent=0,blanker_step=0
      integer :: fft_flags=FFTW_ESTIMATE
      real :: eme_delay=0
      logical :: wspr=.false.,single_decode=.false.,ap_cq_only=.false.
      logical :: measure_doppler=.false.
      character(len=12) :: mycall='',hiscall=''
   end type


   type :: fst4_decoder
      type(fst4_bitmetrics_workspace) :: metrics
      type(fst4_osd_workspace_type) :: fec
      type(fst4_wavegen_workspace) :: wavegen
      type(pack77_state), pointer :: knowledge=>null()
      complex(c_float_complex), pointer, contiguous :: big(:)=>null(),baseband(:)=>null(),frame(:)=>null()
      integer, allocatable :: blanker_hist(:)
      type(c_ptr) :: big_plan=c_null_ptr,baseband_plan=c_null_ptr,big_storage=c_null_ptr
      integer :: fft_length=0,down_length=0,nwcalls=0
      integer :: fft_flags=FFTW_ESTIMATE
      character(len=20) :: wcalls(100)=''
      character(len=12) :: mycall0='',hiscall0=''
      integer :: apbits(240)=0,nappasses(0:5)=0,naptypes(0:5,4)=0
      logical :: first=.true.,measure_doppler=.false.,owns_knowledge=.false.
      logical :: host_history_loaded=.false.,history_dirty=.false.
      procedure(fst4_diagnostic_callback), pointer, nopass :: diagnostic=>null()
      procedure(fst4_spectrum_callback), pointer, nopass :: spectrum_sink=>null()
      complex, allocatable :: sync1(:),sync2(:),synct1(:),synct2(:),sync_tweak(:)
      integer :: sync_nss=0,sync_period=0
      real :: sync_frequency=-1.e30,sync_dt=0,sync_fac=0
      complex, pointer :: dopwave(:)=>null()
      complex(c_float_complex), pointer, contiguous :: dopgain(:)=>null()
      real, pointer :: dopss(:)=>null()
      real, allocatable :: candidate_s(:),candidate_s2(:),candidate_base(:),baseline_scratch(:)
      type(c_ptr) :: dop_plan=c_null_ptr,dop_storage=c_null_ptr
      integer :: dop_length=0
      real, allocatable :: spectrum(:)
      procedure(fst4_decode_callback), pointer :: callback=>null()
   contains
      procedure :: decode_pcm
      procedure :: initialize
      procedure :: reset
      procedure :: destroy
      procedure :: set_known_calls
      procedure :: get_known_calls
      final :: finalize
   end type fst4_decoder

   abstract interface
      subroutine fst4_spectrum_callback(spectrum,df,first_frequency)
         real, intent(in) :: spectrum(:),df,first_frequency
      end subroutine
      subroutine fst4_diagnostic_callback(line)
         character(len=*), intent(in) :: line
      end subroutine
      subroutine fst4_decode_callback (this,nutc,sync,nsnr,dt,freq,    &
         decoded,nap,qual,ntrperiod,fmid,w50,result)
         import fst4_decoder,fst4_result
         implicit none
         class(fst4_decoder), intent(inout) :: this
         integer, intent(in) :: nutc
         real, intent(in) :: sync
         integer, intent(in) :: nsnr
         real, intent(in) :: dt
         real, intent(in) :: freq
         character(len=37), intent(in) :: decoded
         integer, intent(in) :: nap
         real, intent(in) :: qual
         integer, intent(in) :: ntrperiod
         real, intent(in) :: fmid
         real, intent(in) :: w50
         type(fst4_result), optional, intent(in) :: result
      end subroutine fst4_decode_callback
   end interface

contains

   subroutine initialize(this)
      class(fst4_decoder), intent(inout) :: this
      if(.not.associated(this%knowledge)) then
         allocate(this%knowledge)
         this%owns_knowledge=.true.
         call initialize_pack77_state(this%knowledge)
      endif
   end subroutine

   subroutine reset(this)
      class(fst4_decoder), intent(inout) :: this
      this%wcalls=''
      this%nwcalls=0
      this%mycall0=''
      this%hiscall0=''
      this%first=.true.
      this%host_history_loaded=.false.
      this%history_dirty=.false.
      this%sync_nss=0
      this%sync_period=0
      this%sync_frequency=-1.e30
      if(associated(this%knowledge)) call reset_pack77_state(this%knowledge)
   end subroutine

   subroutine destroy(this)
      class(fst4_decoder), intent(inout) :: this
      call clear_fft_plans(this)
      call release_large_workspace(this)
      if(associated(this%baseband)) deallocate(this%baseband)
      if(associated(this%frame)) deallocate(this%frame)
      nullify(this%baseband,this%frame)
      if(allocated(this%blanker_hist)) deallocate(this%blanker_hist)
      if(allocated(this%metrics%ci)) deallocate(this%metrics%ci)
      if(allocated(this%metrics%one)) deallocate(this%metrics%one,this%metrics%zero)
      if(allocated(this%metrics%s2)) deallocate(this%metrics%s2)
      this%metrics%nss=0
      if(allocated(this%wavegen%ctab)) deallocate(this%wavegen%ctab)
      if(allocated(this%wavegen%pulse)) deallocate(this%wavegen%pulse)
      if(allocated(this%wavegen%dphi)) deallocate(this%wavegen%dphi,this%wavegen%scaled_pulse)
      this%wavegen%first=.true.
      this%wavegen%nsps=0
      if(associated(this%knowledge).and.this%owns_knowledge) deallocate(this%knowledge)
      this%owns_knowledge=.false.
      nullify(this%knowledge,this%callback,this%diagnostic,this%spectrum_sink)
      if(allocated(this%sync1)) deallocate(this%sync1,this%sync2,this%synct1,this%synct2,this%sync_tweak)
      if(allocated(this%spectrum)) deallocate(this%spectrum)
      if(associated(this%dopss)) deallocate(this%dopss)
      nullify(this%dopss)
      call this%fec%destroy()
      call this%reset()
   end subroutine

   subroutine clear_fft_plans(this)
      class(fst4_decoder), intent(inout) :: this
      !$omp critical(fftw)
      if(c_associated(this%big_plan)) call fftwf_destroy_plan(this%big_plan)
      if(c_associated(this%baseband_plan)) call fftwf_destroy_plan(this%baseband_plan)
      if(c_associated(this%dop_plan)) call fftwf_destroy_plan(this%dop_plan)
      !$omp end critical(fftw)
      this%big_plan=c_null_ptr
      this%baseband_plan=c_null_ptr
      this%dop_plan=c_null_ptr
      this%dop_length=0
      this%fft_length=0
      this%down_length=0
   end subroutine

   subroutine ensure_fft_buffer(storage,buffer,length,status)
      type(c_ptr), intent(inout) :: storage
      complex(c_float_complex), pointer, contiguous, intent(inout) :: buffer(:)
      integer, intent(in) :: length
      integer, intent(out) :: status
      complex(c_float_complex), pointer, contiguous :: one_based(:)
      status=0
      if(associated(buffer)) then
         if(size(buffer)>=length) return
         call fftwf_free(storage)
         storage=c_null_ptr
         nullify(buffer)
      endif
      storage=fftwf_alloc_complex(int(length,c_size_t))
      if(.not.c_associated(storage)) then
         status=-1
         return
      endif
      call c_f_pointer(storage,one_based,[length])
      buffer(0:length-1)=>one_based
   end subroutine

   subroutine release_candidate_workspace(this)
      class(fst4_decoder), intent(inout) :: this
      if(allocated(this%candidate_s)) deallocate(this%candidate_s)
      if(allocated(this%candidate_s2)) deallocate(this%candidate_s2)
      if(allocated(this%candidate_base)) deallocate(this%candidate_base)
      if(allocated(this%baseline_scratch)) deallocate(this%baseline_scratch)
   end subroutine

   subroutine release_large_workspace(this)
      class(fst4_decoder), intent(inout) :: this
      ! Aligned replacement buffers can reuse the retained plans through new-array execution.
      if(c_associated(this%big_storage)) call fftwf_free(this%big_storage)
      if(c_associated(this%dop_storage)) call fftwf_free(this%dop_storage)
      this%big_storage=c_null_ptr
      this%dop_storage=c_null_ptr
      nullify(this%big,this%dopgain)
      if(associated(this%dopwave)) deallocate(this%dopwave)
      nullify(this%dopwave)
      call release_candidate_workspace(this)
   end subroutine

   subroutine finalize(this)
      type(fst4_decoder), intent(inout) :: this
      call this%destroy()
   end subroutine

   subroutine set_known_calls(this,calls,status)
      class(fst4_decoder), intent(inout) :: this
      character(len=*), intent(in) :: calls(:)
      integer, intent(out) :: status
      status=-1
      if(size(calls)>100) return
      this%wcalls=''
      this%nwcalls=size(calls)
      this%wcalls(1:size(calls))=calls
      status=0
   end subroutine

   subroutine get_known_calls(this,calls,count)
      class(fst4_decoder), intent(in) :: this
      character(len=*), intent(out) :: calls(:)
      integer, intent(out) :: count
      count=this%nwcalls
      calls=''
      calls(1:min(size(calls),count))=this%wcalls(1:min(size(calls),count))
   end subroutine

   subroutine decode_pcm(this,callback,samples,sample_count,options,status)
      class(fst4_decoder), intent(inout) :: this
      procedure(fst4_decode_callback) :: callback
      integer(c_int16_t), intent(in) :: samples(:)
      integer, intent(in) :: sample_count
      type(fst4_options), intent(in) :: options
      integer, intent(out) :: status
      integer :: nfa,nfb,period,n
      character(len=12) :: mycall,hiscall
      status=-1
      period=options%period
      n=period*12000
      ! The engine validates options and supplies a zero-padded full period.
      if(sample_count<1.or.sample_count/=n.or.sample_count>size(samples)) return
      call this%initialize()
      if(options%fft_flags/=this%fft_flags) then
         call clear_fft_plans(this)
         this%fft_flags=options%fft_flags
      endif
      this%measure_doppler=options%measure_doppler
      nfa=options%low_frequency
      nfb=options%high_frequency
      mycall=options%mycall
      hiscall=options%hiscall
      call decode_kernel(this,callback,samples,options%utc,options%qso_progress,nfa,nfb, &
         options%receive_frequency,options%effort,period,options%blanker_percent,options%blanker_step, &
         options%single_decode,options%tolerance, &
         options%eme_delay,options%ap_cq_only,mycall,hiscall,merge(1,0,options%wspr),status)
      if(period>=300) call release_large_workspace(this)
   end subroutine

   subroutine decode_kernel(this,callback,iwave,nutc,nQSOProgress,nfa,nfb,nfqso, &
      ndepth,ntrperiod,blanker_percent,blanker_step,single_decode,ntol,emedelay,lapcqonly,mycall, &
      hiscall,iwspr,status)

      use timer_module, only: timer
      use packjt77
      use, intrinsic :: iso_c_binding
      include 'fst4/fst4_params.f90'
      parameter (MAXCAND=100,MAXWCALLS=100)
      class(fst4_decoder), intent(inout) :: this
      procedure(fst4_decode_callback) :: callback
      integer, intent(in) :: blanker_percent,blanker_step
      logical, intent(in) :: single_decode
      character*37 decodes(100)
      integer decoded_hashes(100),unresolved_hash
      character*37 msg,msgsent
      character*20 wpart
      character*77 c77
      character*12 mycall,hiscall
      complex(c_float_complex), pointer, contiguous :: c2(:),cframe(:),c_bigfft(:)
      real llr(240),llrs(240,4)
      real candidates0(200,5),candidates(200,5)
      real bitmetrics(320,4)
      real s4(0:3,NN)
      real minsync
      logical lapcqonly
      integer itone(NN)
      integer hmod
      integer*1 apmask(240),cw(240),hdec(240)
      integer*1 message101(101),message74(74),message77(77)

      logical badsync,unpk77_success
      logical nohiscall
      logical new_callsign,plotspec_exists,do_k50_decode
      logical decdata_exists
      character(len=256) :: diagnostic_line

      integer*2 iwave(*)
      integer status
      real(c_float), pointer, contiguous :: fft_input(:)
      type(fst4_result) :: result

      integer, parameter :: rvec(77)=[0,1,0,0,1,0,1,0,0,1,0,1,1,1,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0, &
         1,0,0,1,0,1,1,0,0,0,0,1,0,0,0,1,0,1,0,0,1,1,1,1,0,0,1,0,1, &
         0,1,0,1,0,1,1,0,1,1,1,1,1,0,0,0,1,0,1]
      integer, parameter :: mcq(29)= &
         2*mod([0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0]+rvec(1:29),2)-1
      integer, parameter :: mrrr(19)=2*mod([0,1,1,1,1,1,1,0,1,0,0,1,0,0,1,0,0,0,1]+rvec(59:77),2)-1
      integer, parameter :: m73(19)=2*mod([0,1,1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,0,1]+rvec(59:77),2)-1
      data hmod/1/



      status=0
      this%callback => callback
      this%knowledge%dxcall13=hiscall
      this%knowledge%mycall13=mycall

      if(iwspr.ne.0 .and. iwspr.ne.1) return 

      if(this%first) then
         this%apbits=0
         this%apbits(1)=99
         this%apbits(30)=99

         this%nappasses(0)=2
         this%nappasses(1)=2
         this%nappasses(2)=2
         this%nappasses(3)=2
         this%nappasses(4)=2
         this%nappasses(5)=3

! iaptype
!------------------------
!   1        CQ     ???    ???           (29 ap bits)
!   2        MyCall ???    ???           (29 ap bits)
!   3        MyCall DxCall ???           (58 ap bits)
!   4        MyCall DxCall RRR           (77 ap bits)
!   5        MyCall DxCall 73            (77 ap bits)
!   6        MyCall DxCall RR73          (77 ap bits)
!********

         this%naptypes(0,1:4)=(/1,2,0,0/) ! Tx6 selected (CQ)
         this%naptypes(1,1:4)=(/2,3,0,0/) ! Tx1
         this%naptypes(2,1:4)=(/2,3,0,0/) ! Tx2
         this%naptypes(3,1:4)=(/3,6,0,0/) ! Tx3
         this%naptypes(4,1:4)=(/3,6,0,0/) ! Tx4
         this%naptypes(5,1:4)=(/3,1,2,0/) ! Tx5

         this%mycall0=''
         this%hiscall0=''
         this%first=.false.
      endif

      l1=index(mycall,char(0))
      if(l1.ne.0) mycall(l1:)=" "
      l1=index(hiscall,char(0))
      if(l1.ne.0) hiscall(l1:)=" "
      if(mycall.ne.this%mycall0 .or. hiscall.ne.this%hiscall0) then
         this%apbits=0
         this%apbits(1)=99
         this%apbits(30)=99

         if(len(trim(mycall)) .lt. 3) go to 10

         nohiscall=.false.
         this%hiscall0=hiscall
         if(len(trim(this%hiscall0)).lt.3) then
            this%hiscall0=mycall  ! use mycall for dummy hiscall - mycall won't be hashed.
            nohiscall=.true.
         endif
         msg=trim(mycall)//' '//trim(this%hiscall0)//' RR73'
         i3=-1
         n3=-1
         call pack77_for_state(this%knowledge,msg,i3,n3,c77)
         call unpack77_for_state(this%knowledge,c77,1,msgsent,unpk77_success)
         if(i3.ne.1 .or. (msg.ne.msgsent) .or. .not.unpk77_success) go to 10
         read(c77,'(77i1)') message77
         message77=mod(message77+rvec,2)
         this%apbits(1:77)=2*message77-1
         if(nohiscall) this%apbits(30)=99

10       continue
         this%mycall0=mycall
         this%hiscall0=hiscall
      endif
!************************************

      if(nfqso+nqsoprogress.eq.-999) return
      Keff=91
      nmax=15*12000
      if(ntrperiod.eq.15) then
         nsps=720
         nmax=15*12000
         ndown=18                  !nss=40,80,160,400
         nfft1=int(nmax/ndown)*ndown
      else if(ntrperiod.eq.30) then
         nsps=1680
         nmax=30*12000
         ndown=42                  !nss=40,80,168,336
         nfft1=359856  !nfft2=8568=2^3*3^2*7*17
      else if(ntrperiod.eq.60) then
         nsps=3888
         nmax=60*12000
         ndown=108
         nfft1=7500*96    ! nfft2=7500=2^2*3*5^4
      else if(ntrperiod.eq.120) then
         nsps=8200
         nmax=120*12000
         ndown=205                 !nss=40,82,164,328
         nfft1=7200*200   ! nfft2=7200=2^5*3^2*5^2
      else if(ntrperiod.eq.300) then
         nsps=21504
         nmax=300*12000
         ndown=512                 !nss=42,84,168,336
         nfft1=7020*512   ! nfft2=7020=2^2*3^3*5*13
      else if(ntrperiod.eq.900) then
         nsps=66560
         nmax=900*12000
         ndown=1664                !nss=40,80,160,320
         nfft1=6480*1664  ! nfft2=6480=2^4*3^4*5
      else if(ntrperiod.eq.1800) then
         nsps=134400
         nmax=1800*12000
         ndown=3360                !nss=40,80,160,320
         nfft1=6426*3360  ! nfft2=6426=2*3^3*7*17
      end if
      nss=nsps/ndown
      fs=12000.0                       !Sample rate
      fs2=fs/ndown
      nspsec=nint(fs2)
      dt=1.0/fs                        !Sample interval (s)
      dt2=1.0/fs2
      tt=nsps*dt                       !Duration of "itone" symbols (s)
      baud=1.0/tt
      sigbw=4.0*baud
      nfft2=nfft1/ndown                !make sure that nfft1 is exactly nfft2*ndown
      nfft1=nfft2*ndown
      nh1=nfft1/2

      call ensure_fft_buffer(this%big_storage,this%big,nfft1/2+1,status)
      if(status/=0) return
      call c_f_pointer(this%big_storage,fft_input,[nfft1+2])
      if(this%fft_length/=nfft1.or..not.c_associated(this%big_plan)) then
         !$omp critical(fftw)
         if(c_associated(this%big_plan)) call fftwf_destroy_plan(this%big_plan)
         this%big_plan=fftwf_plan_dft_r2c_1d(nfft1,fft_input,this%big,this%fft_flags)
         !$omp end critical(fftw)
         this%fft_length=nfft1
      endif
      if(this%down_length/=nfft2.or..not.c_associated(this%baseband_plan)) then
         !$omp critical(fftw)
         if(c_associated(this%baseband_plan)) call fftwf_destroy_plan(this%baseband_plan)
         !$omp end critical(fftw)
         this%baseband_plan=c_null_ptr
         if(associated(this%baseband)) then
            if(size(this%baseband)<nfft2) then
               deallocate(this%baseband)
               nullify(this%baseband)
            endif
         endif
         if(.not.associated(this%baseband)) allocate(this%baseband(0:nfft2-1))
         !$omp critical(fftw)
         this%baseband_plan=fftwf_plan_dft_1d(nfft2,this%baseband,this%baseband,FFTW_BACKWARD,this%fft_flags)
         !$omp end critical(fftw)
         this%down_length=nfft2
      endif
      if(.not.c_associated(this%big_plan).or..not.c_associated(this%baseband_plan)) then
         status=-1
         return
      endif
      if(associated(this%frame)) then
         if(size(this%frame)<160*nss) then
            deallocate(this%frame)
            nullify(this%frame)
         endif
      endif
      if(.not.associated(this%frame)) allocate(this%frame(0:160*nss-1))
      c_bigfft(0:)=>this%big(0:nfft1/2)
      c2(0:)=>this%baseband(0:nfft2-1)
      cframe(0:)=>this%frame(0:160*nss-1)

      jittermax=2
      do_k50_decode=.false.
      if(ndepth.eq.3) then
         nblock=4
         jittermax=2
         do_k50_decode=.true.
      elseif(ndepth.eq.2) then
         nblock=4
         jittermax=2
         do_k50_decode=.false.
      elseif(ndepth.eq.1) then
         nblock=4
         jittermax=0
         do_k50_decode=.false.
      endif

! Noise blanker setup
      ndropmax=1
      npct=blanker_percent
      inb1=0
      inb2=1
      if(blanker_step>0) then
         inb1=20
         inb2=blanker_step
      endif


! nfa,nfb: define the noise-baseline analysis window
!  fa, fb: define the signal search window
! We usually make nfa<fa and nfb>fb so that noise baseline analysis
! window extends outside of the [fa,fb] window where we think the signals are.
!
      if(iwspr.eq.1) then  !FST4W
         nfa=max(100,nfqso-ntol-100)
         nfb=min(4800,nfqso+ntol+100)
         fa=max(100,nint(nfqso+1.5*baud-ntol))  ! signal search window
         fb=min(4800,nint(nfqso+1.5*baud+ntol))
      else if(iwspr.eq.0) then
         if(single_decode) then
            fa=max(100,nint(nfa+1.5*baud))
            fb=min(4800,nint(nfb+1.5*baud))
            ! extend noise fit 100 Hz outside of search window
            nfa=max(100,nfa-100)
            nfb=min(4800,nfb+100)
         else
            fa=max(100,nint(nfa+1.5*baud))
            fb=min(4800,nint(nfb+1.5*baud))
            ! extend noise fit 100 Hz outside of search window
            nfa=max(100,nfa-100)
            nfb=min(4800,nfb+100)
         endif
      endif

      ndecodes=0
      decodes=' '
      decoded_hashes=-1
      new_callsign=.false.
      if(.not.allocated(this%blanker_hist)) allocate(this%blanker_hist(0:32768))
      do inb=0,inb1,inb2
         if(blanker_step>0) npct=inb
         call blanker(iwave,nfft1,ndropmax,npct,c_bigfft,this%blanker_hist)

! The big fft is done once and is used for calculating the smoothed spectrum
! and also for downconverting/downsampling each candidate.
         call fftwf_execute_dft_r2c(this%big_plan,fft_input,this%big)
         nhicoh=1
         nsyncoh=8
         minsync=1.20
         if(ntrperiod.eq.15) minsync=1.15

         call get_candidates_fst4(this,c_bigfft,nfft1,nsps,hmod,fs,fa,fb,nfa,nfb,  &
            minsync,ncand,candidates0,status)
         if(status/=0) return
         isbest=0
         fc2=0.
         do icand=1,ncand
            fc0=candidates0(icand,1)
            if(iwspr.eq.0 .and. blanker_step>0 .and. npct.ne.0 .and.    &
               abs(fc0-(nfqso+1.5*baud)).gt.ntol) cycle  ! blanker loop only near nfqso
            detmet=candidates0(icand,2)

! Downconvert and downsample a slice of the spectrum centered on the
! rough estimate of the candidates frequency.
! Output array c2 is complex baseband sampled at 12000/ndown Sa/sec.
! The size of the downsampled c2 array is nfft2=nfft1/ndown
            call timer('dwnsmpl ',0)
            call fst4_downsample(this,c_bigfft,nfft1,ndown,fc0,sigbw,c2)
            call timer('dwnsmpl ',1)

            call timer('sync240 ',0)
            call fst4_sync_search(this,c2,nfft2,hmod,fs2,nss,ntrperiod,nsyncoh, &
                 emedelay,sbest,fcbest,isbest)
            call timer('sync240 ',1)
            if(isbest<0) then
               candidates0(icand,3)=-1
               cycle
            endif

            fc_synced = fc0 + fcbest
            dt_synced = (isbest-fs2)*dt2  !nominal dt is 1 second so frame starts at sample fs2
            candidates0(icand,3)=fc_synced
            candidates0(icand,4)=isbest
         enddo

! remove duplicate candidates
         do icand=1,ncand
            fc=candidates0(icand,3)
            isbest=nint(candidates0(icand,4))
            do ic2=icand+1,ncand
               fc2=candidates0(ic2,3)
               isbest2=nint(candidates0(ic2,4))
               if(fc2.gt.0.0) then
                  if(abs(fc2-fc).lt.0.10*baud) then ! same frequency
                     if(abs(isbest2-isbest).le.2) then
                        candidates0(ic2,3)=-1
                     endif
                  endif
               endif
            enddo
         enddo
         ic=0
         do icand=1,ncand
            if(candidates0(icand,3).gt.0) then
               ic=ic+1
               candidates0(ic,:)=candidates0(icand,:)
            endif
         enddo
         ncand=ic

! If FST4 mode and Single Decode is not checked, then find candidates
! within 20 Hz of nfqso and put them at the top of the list
         if(iwspr.eq.0 .and. .not.single_decode) then
            nclose=count(abs(candidates0(:,3)-(nfqso+1.5*baud)).le.20)
            k=0
            do i=1,ncand
               if(abs(candidates0(i,3)-(nfqso+1.5*baud)).le.20) then
                  k=k+1
                  candidates(k,:)=candidates0(i,:)
               endif
            enddo
            do i=1,ncand
               if(abs(candidates0(i,3)-(nfqso+1.5*baud)).gt.20) then
                  k=k+1
                  candidates(k,:)=candidates0(i,:)
               endif
            enddo
         else
            candidates=candidates0
         endif

         xsnr=0.
         do icand=1,ncand
            sync=candidates(icand,2)
            fc_synced=candidates(icand,3)
            isbest=nint(candidates(icand,4))
            xdt=(isbest-nspsec)/fs2
            if(ntrperiod.eq.15) xdt=(isbest-real(nspsec)/2.0)/fs2
            call timer('dwnsmpl ',0)
            call fst4_downsample(this,c_bigfft,nfft1,ndown,fc_synced,sigbw,c2)
            call timer('dwnsmpl ',1)

            do ijitter=0,jittermax
               if(ijitter.eq.0) ioffset=0
               if(ijitter.eq.1) ioffset=1
               if(ijitter.eq.2) ioffset=-1
               is0=isbest+ioffset
               iend=is0+160*nss-1
               if( is0.lt.0 .or. iend.gt.(nfft2-1) ) cycle
               cframe=c2(is0:iend)
               bitmetrics=0
               call timer('bitmetrc',0)
               call get_fst4_bitmetrics_owned(this%metrics,cframe,nss,bitmetrics, &
                  s4,nsync_qual,badsync)
               call timer('bitmetrc',1)
               if(badsync) cycle

               do il=1,4
                  llrs(  1: 60,il)=bitmetrics( 17: 76, il)
                  llrs( 61:120,il)=bitmetrics( 93:152, il)
                  llrs(121:180,il)=bitmetrics(169:228, il)
                  llrs(181:240,il)=bitmetrics(245:304, il)
               enddo

               apmag=maxval(abs(llrs(:,4)))*1.1
               ntmax=nblock+this%nappasses(nQSOProgress)
               if(lapcqonly) ntmax=nblock+1
               if(ndepth.eq.1) ntmax=nblock ! no ap for ndepth=1
               apmask=0

               if(iwspr.eq.1) then ! 50-bit msgs, no ap decoding
                  nblock=4
                  ntmax=nblock
               endif

               do itry=1,ntmax
                  if(itry.eq.1) llr=llrs(:,1)
                  if(itry.eq.2.and.itry.le.nblock) llr=llrs(:,2)
                  if(itry.eq.3.and.itry.le.nblock) llr=llrs(:,3)
                  if(itry.eq.4.and.itry.le.nblock) llr=llrs(:,4)
                  if(itry.le.nblock) then
                     apmask=0
                     iaptype=0
                  endif

                  if(itry.gt.nblock .and. iwspr.eq.0) then ! do ap passes
                     llr=llrs(:,nblock)  ! Use largest blocksize as the basis for AP passes
                     iaptype=this%naptypes(nQSOProgress,itry-nblock)
                     if(lapcqonly) iaptype=1
                     if(iaptype.ge.2 .and. this%apbits(1).gt.1) cycle  ! No, or nonstandard, mycall
                     if(iaptype.ge.3 .and. this%apbits(30).gt.1) cycle ! No, or nonstandard, dxcall
                     if(iaptype.eq.1) then   ! CQ
                        apmask=0
                        apmask(1:29)=1
                        llr(1:29)=apmag*mcq(1:29)
                     endif

                     if(iaptype.eq.2) then  ! MyCall ??? ???
                        apmask=0
                        apmask(1:29)=1
                        llr(1:29)=apmag*this%apbits(1:29)
                     endif

                     if(iaptype.eq.3) then  ! MyCall DxCall ???
                        apmask=0
                        apmask(1:58)=1
                        llr(1:58)=apmag*this%apbits(1:58)
                     endif

                     if(iaptype.eq.4 .or. iaptype.eq.5 .or. iaptype .eq.6) then
                        apmask=0
                        apmask(1:77)=1
                        llr(1:58)=apmag*this%apbits(1:58)
                        if(iaptype.eq.4) llr(59:77)=apmag*mrrr(1:19)
                        if(iaptype.eq.5) llr(59:77)=apmag*m73(1:19)
                        if(iaptype.eq.6) llr(59:77)=apmag*this%apbits(59:77)
                     endif
                  endif

                  dmin=0.0
                  nharderrors=-1
                  unpk77_success=.false.
                  if(iwspr.eq.0) then
                     maxosd=2
                     Keff=91
                     norder=3
                     call timer('d240_101',0)
                     call decode240_101_owned(this%fec,llr,Keff,maxosd,norder,apmask,message101, &
                        cw,ntype,nharderrors,dmin)
                     call timer('d240_101',1)
                     if(count(cw.eq.1).eq.0) then
                        nharderrors=-nharderrors
                        cycle
                     endif
                     write(c77,'(77i1)') mod(message101(1:77)+rvec,2)
                     call unpack77_for_state(this%knowledge,c77,1,msg,unpk77_success)
                  elseif(iwspr.eq.1) then
! Try decoding with Keff=66
                     maxosd=2
                     call timer('d240_74 ',0)
                     Keff=66
                     norder=3
                     call decode240_74_owned(this%fec,llr,Keff,maxosd,norder,apmask,message74,cw, &
                        ntype,nharderrors,dmin)
                     call timer('d240_74 ',1)
                     if(nharderrors.lt.0) goto 3465
                     if(count(cw.eq.1).eq.0) then
                        nharderrors=-nharderrors
                        cycle
                     endif
                     write(c77,'(50i1)') message74(1:50)
                     c77(51:77)='000000000000000000000110000'
                     call unpack77_for_state(this%knowledge,c77,1,msg,unpk77_success)
                     if(unpk77_success .and. do_k50_decode) then
! If decode was obtained with Keff=66, save call/grid in fst4w_calls.txt if not there already.
                        i1=index(msg,' ')
                        i2=i1+index(msg(i1+1:),' ')
                        wpart=trim(msg(1:i2))
! Only save callsigns/grids from type 1 messages
                        if(index(wpart,'/').eq.0 .and. index(wpart,'<').eq.0) then
                           ifound=0
                           do i=1,this%nwcalls
                              if(index(this%wcalls(i),wpart).ne.0) ifound=1
                           enddo

                           if(ifound.eq.0) then ! This is a new callsign
                              new_callsign=.true.
                              if(this%nwcalls.lt.MAXWCALLS) then
                                 this%nwcalls=this%nwcalls+1
                                 this%wcalls(this%nwcalls)=wpart
                              else
                                 this%wcalls(1:this%nwcalls-1)=this%wcalls(2:this%nwcalls)
                                 this%wcalls(this%nwcalls)=wpart
                              endif
                           endif
                        endif
                     endif
3465                 continue

! If no decode then try Keff=50
                     iaptype=0
                     if( .not. unpk77_success .and. do_k50_decode ) then
                        maxosd=1
                        call timer('d240_74 ',0)
                        Keff=50
                        norder=4
                        call decode240_74_owned(this%fec,llr,Keff,maxosd,norder,apmask,message74,cw, &
                           ntype,nharderrors,dmin)
                        call timer('d240_74 ',1)
                        if(count(cw.eq.1).eq.0) then
                           nharderrors=-nharderrors
                           cycle
                        endif
                        write(c77,'(50i1)') message74(1:50)
                        c77(51:77)='000000000000000000000110000'
                        call unpack77_for_state(this%knowledge,c77,1,msg,unpk77_success)
! No CRC in this mode, so only accept the decode if call/grid have been seen before
                        if(unpk77_success) then
                           unpk77_success=.false.
                           do i=1,this%nwcalls
                              if(len_trim(this%wcalls(i))>0.and.index(msg,trim(this%wcalls(i))).gt.0) then
                                 unpk77_success=.true.
                              endif
                           enddo
                        endif
                     endif

                  endif

                  if(nharderrors .ge.0 .and. unpk77_success) then
                     unresolved_hash=-1
                     if(iwspr==1.and.index(msg,'<...>')>0) read(c77,'(b22.22)') unresolved_hash
                     idupe=0
                     do i=1,ndecodes
                        if(decodes(i)==msg.and.decoded_hashes(i)==unresolved_hash) idupe=1
                     enddo
                     if(idupe.eq.1) goto 800
                     ndecodes=ndecodes+1
                     decodes(ndecodes)=msg
                     decoded_hashes(ndecodes)=unresolved_hash

                     if(iwspr.eq.0) then
                        call get_fst4_tones_from_bits(message101,itone,0)
                     else
                        call get_fst4_tones_from_bits(message74,itone,1)
                     endif
                     plotspec_exists=this%measure_doppler

                     fmid=-999.0
                     w50=0.0
                     call timer('dopsprd ',0)
                     if(plotspec_exists) then
                        call dopspread(this,itone,iwave,nsps,nmax,ndown,hmod,  &
                           isbest,fc_synced,fmid,w50,status)
                        if(status/=0) return
                     endif
                     call timer('dopsprd ',1)
                     xsig=0
                     do i=1,NN
                        xsig=xsig+s4(itone(i),i)
                     enddo
                     base=candidates(icand,5)
                     select case(ntrperiod)
                        case(15) 
                           snr_calfac=800.0
                        case(30) 
                           snr_calfac=600.0
                        case(60) 
                           snr_calfac=430.0
                        case(120) 
                           snr_calfac=390.0
                        case(300) 
                           snr_calfac=340.0
                        case(900) 
                           snr_calfac=320.0
                        case(1800) 
                           snr_calfac=320.0
                        case default
                           snr_calfac=430.0
                     end select
                     arg=snr_calfac*xsig/base - 1.0
                     if(arg.gt.0.0) then
                        xsnr=10*log10(arg)+10*log10(1.46/2500)+10*log10(8200.0/nsps)
                     else
                        xsnr=-99.9
                     endif
                     nsnr=nint(xsnr)
                     qual=0.0
                     fsig=fc_synced - 1.5*baud
                     decdata_exists=associated(this%diagnostic)
                     if(decdata_exists) then
                        hdec=0
                        where(llrs(:,1).ge.0.0) hdec=1
                        nhp=count(hdec.ne.cw) ! # hard errors wrt N=1 soft symbols
                        hd=sum(ieor(hdec,cw)*abs(llrs(:,1))) ! weighted distance wrt N=1 symbols
                        write(diagnostic_line,3021) nutc,icand,itry,nsyncoh,iaptype,  &
                           ijitter,npct,ntype,Keff,nsync_qual,nharderrors,dmin,nhp,hd,  &
                           sync,xsnr,xdt,fsig,w50,trim(msg)
3021                    format(i6.6,i4,6i3,3i4,f6.1,i4,f6.1,f9.2,f6.1,f6.2,f7.1,f7.3,1x,a)
                        call this%diagnostic(trim(diagnostic_line))
                     endif
                     result=fst4_result()
                     result%period_seconds=ntrperiod
                     result%effective_bits=Keff
                     result%blanker_percent=npct
                     if(unresolved_hash>=0) then
                        result%has_hash22=1
                        result%hash22=unresolved_hash
                     endif
                     result%has_doppler=merge(1,0,plotspec_exists)
                     if(plotspec_exists) then
                        result%fmid_hz=fmid
                        result%width_hz=w50
                     endif
                     call this%callback(nutc,sync,nsnr,xdt,fsig,msg, &
                        iaptype,qual,ntrperiod,fmid,w50,result)
                     goto 800
                  endif
               enddo  ! metrics
            enddo  ! istart jitter
800      enddo !candidate list
      enddo ! noise blanker loop

      if(new_callsign) this%history_dirty=.true.

      return
   end subroutine decode_kernel

   subroutine sync_fst4(this,cd0,i0,f0,hmod,ncoh,np,nss,ntr,fs,sync)

! Compute sync power for a complex, downsampled FST4 signal.

      use timer_module, only: timer
      include 'fst4/fst4_params.f90'
      class(fst4_decoder), intent(inout) :: this
      complex cd0(0:np-1)

      complex z1,z2,z3,z4,z5
      integer hmod,isyncword1(0:7),isyncword2(0:7)


      data isyncword1/0,1,3,2,1,0,2,3/
      data isyncword2/2,3,1,0,3,2,0,1/



      p(z1)=(real(z1*this%sync_fac)**2 + aimag(z1*this%sync_fac)**2)**0.5     !Compute power

      if(.not.allocated(this%sync1)) allocate(this%sync1(3200),this%sync2(3200), &
         this%synct1(3200),this%synct2(3200),this%sync_tweak(3200))
      twopi=8.0*atan(1.0)
      nz=8*nss
      call timer('sync240a',0)
      if(nss.ne.this%sync_nss .or. ntr.ne.this%sync_period) then
         twopi=8.0*atan(1.0)
         this%sync_dt=1/fs
         k=1
         phi1=0.0
         phi2=0.0
         do i=0,7
            dphi1=twopi*hmod*(isyncword1(i)-1.5)/real(nss)
            dphi2=twopi*hmod*(isyncword2(i)-1.5)/real(nss)
            do j=1,nss
               this%sync1(k)=cmplx(cos(phi1),sin(phi1))
               this%sync2(k)=cmplx(cos(phi2),sin(phi2))
               phi1=mod(phi1+dphi1,twopi)
               phi2=mod(phi2+dphi2,twopi)
               k=k+1
            enddo
         enddo
         this%sync_fac=1.0/(8.0*nss)
         this%sync_nss=nss
         this%sync_period=ntr
         this%sync_frequency=-1.e30
      endif

      if(f0.ne.this%sync_frequency) then
         dphi=twopi*f0*this%sync_dt
         phi=0.0
         do i=1,nz
            this%sync_tweak(i)=cmplx(cos(phi),sin(phi))
            phi=mod(phi+dphi,twopi)
         enddo
         this%synct1(1:nz)=this%sync_tweak(1:nz)*this%sync1(1:nz)
         this%synct2(1:nz)=this%sync_tweak(1:nz)*this%sync2(1:nz)
         this%sync_frequency=f0
         this%sync_nss=nss
      endif
      call timer('sync240a',1)

      i1=i0                            !Costas arrays
      i2=i0+38*nss
      i3=i0+76*nss
      i4=i0+114*nss
      i5=i0+152*nss

      s1=0.0
      s2=0.0
      s3=0.0
      s4=0.0
      s5=0.0

      if(ncoh.gt.0) then
         nsec=8/ncoh
         do i=1,nsec
            is=(i-1)*ncoh*nss
            z1=0
            if(i1+is.ge.1) then
               z1=sum(cd0(i1+is:i1+is+ncoh*nss-1)*conjg(this%synct1(is+1:is+ncoh*nss)))
            endif
            z2=sum(cd0(i2+is:i2+is+ncoh*nss-1)*conjg(this%synct2(is+1:is+ncoh*nss)))
            z3=sum(cd0(i3+is:i3+is+ncoh*nss-1)*conjg(this%synct1(is+1:is+ncoh*nss)))
            z4=sum(cd0(i4+is:i4+is+ncoh*nss-1)*conjg(this%synct2(is+1:is+ncoh*nss)))
            z5=0
            if(i5+is+ncoh*nss-1.lt.np) then
               z5=sum(cd0(i5+is:i5+is+ncoh*nss-1)*conjg(this%synct1(is+1:is+ncoh*nss)))
            endif
            s1=s1+abs(z1)/nz
            s2=s2+abs(z2)/nz
            s3=s3+abs(z3)/nz
            s4=s4+abs(z4)/nz
            s5=s5+abs(z5)/nz
         enddo
      else
         nsub=-ncoh
         nps=nss/nsub
         do i=1,8
            do isub=1,nsub
               is=(i-1)*nss+(isub-1)*nps
               z1=0.0
               if(i1+is.ge.1) then
                  z1=sum(cd0(i1+is:i1+is+nps-1)*conjg(this%synct1(is+1:is+nps)))
               endif
               z2=sum(cd0(i2+is:i2+is+nps-1)*conjg(this%synct2(is+1:is+nps)))
               z3=sum(cd0(i3+is:i3+is+nps-1)*conjg(this%synct1(is+1:is+nps)))
               z4=sum(cd0(i4+is:i4+is+nps-1)*conjg(this%synct2(is+1:is+nps)))
               z5=0.0
               if(i5+is+ncoh*nss-1.lt.np) then
                  z5=sum(cd0(i5+is:i5+is+nps-1)*conjg(this%synct1(is+1:is+nps)))
               endif
               s1=s1+abs(z1)/(8*nss)
               s2=s2+abs(z2)/(8*nss)
               s3=s3+abs(z3)/(8*nss)
               s4=s4+abs(z4)/(8*nss)
               s5=s5+abs(z5)/(8*nss)
            enddo
         enddo
      endif
      sync = s1+s2+s3+s4+s5
      return
   end subroutine sync_fst4

   subroutine fst4_downsample(this,c_bigfft,nfft1,ndown,f0,sigbw,c1)

! Output: Complex data in c(), sampled at 12000/ndown Hz

      class(fst4_decoder), intent(inout) :: this
      complex c_bigfft(0:nfft1/2)
      complex c1(0:nfft1/ndown-1)

      df=12000.0/nfft1
      i0=nint(f0/df)
      ih=nint( ( f0 + 1.3*sigbw/2.0 )/df)
      nbw=ih-i0+1
      c1=0.
      c1(0)=c_bigfft(i0)
      nfft2=nfft1/ndown
      do i=1,nbw
         if(i0+i.le.nfft1/2) c1(i)=c_bigfft(i0+i)
         if(i0-i.ge.0) c1(nfft2-i)=c_bigfft(i0-i)
      enddo
      c1=c1/nfft2
      call fftwf_execute_dft(this%baseband_plan,c1,c1)
      return

   end subroutine fst4_downsample

   subroutine get_candidates_fst4(this,c_bigfft,nfft1,nsps,hmod,fs,fa,fb,nfa,nfb,   &
      minsync,ncand,candidates,status)

      class(fst4_decoder), intent(inout) :: this
      complex c_bigfft(0:nfft1/2)              !Full length FFT of raw data
      integer hmod                             !Modulation index (submode)
      integer im(1)                            !For maxloc
      real candidates(200,5)                   !Candidate list
      real xdb(-3:3)                           !Model 4-tone CCF peaks
      real minsync
      integer, intent(out) :: status
      integer allocation_status
      data xdb/0.25,0.50,0.75,1.0,0.75,0.50,0.25/

      status=0
      ncand=0
      candidates=0
      nh1=nfft1/2
      df1=fs/nfft1
      baud=fs/nsps                             !Keying rate
      df2=baud/2.0
      nd=df2/df1                               !s() sums this many bins of big FFT
      ndh=nd/2
      ia=nint(max(100.0,fa)/df2)               !Low frequency search limit
      ib=nint(min(4800.0,fb)/df2)              !High frequency limit
      ina=nint(max(100.0,real(nfa))/df2)       !Low freq limit for noise baseline fit
      inb=nint(min(4800.0,real(nfb))/df2)      !High freq limit for noise fit
      if(ia.lt.ina) ia=ina
      if(ib.gt.inb) ib=inb
      if(ib<ia.or.inb-ina<8) return

      nnw=nint(48000.*nsps*2./fs)
      if(allocated(this%candidate_s)) then
         if(size(this%candidate_s)<nnw) call release_candidate_workspace(this)
      endif
      if(.not.allocated(this%candidate_s)) then
         allocate(this%candidate_s(nnw),this%candidate_s2(nnw),this%candidate_base(nnw), &
            this%baseline_scratch(nnw),stat=allocation_status)
         if(allocation_status/=0) then
            call release_candidate_workspace(this)
            status=-1
            return
         endif
      endif
      associate(s=>this%candidate_s(1:nnw),s2=>this%candidate_s2(1:nnw),sbase=>this%candidate_base(1:nnw))
      s=0.                                  !Compute low-resolution power spectrum
      do i=ina,inb   ! noise analysis window includes signal analysis window
         j0=nint(i*df2/df1)
         do j=j0-ndh,j0+ndh
            s(i)=s(i) + real(c_bigfft(j))**2 + aimag(c_bigfft(j))**2
         enddo
      enddo

      ina=max(ina,1+3*hmod)                       !Don't run off the ends
      inb=min(inb,nnw-3*hmod)
      s2=0.
      do i=ina,inb                                !Compute CCF of s() and 4 tones
         s2(i)=s(i-hmod*3) + s(i-hmod) +s(i+hmod) +s(i+hmod*3)
      enddo
      npctile=30
      call fst4_baseline_owned(s2,nnw,ina+hmod*3,inb-hmod*3,npctile,sbase,this%baseline_scratch(1:nnw))
      if(any(sbase(ina:inb).le.0.0)) return
      s2(ina:inb)=s2(ina:inb)/sbase(ina:inb)             !Normalize wrt noise level

      ncand=0
      candidates=0
      if(ia.lt.3) ia=3
      if(ib.gt.nnw-2) ib=nnw-2

! Find candidates, using the CLEAN algorithm to remove a model of each one
! from s2() after it has been found.
      pval=99.99
      do while(ncand.lt.200)
         im=maxloc(s2(ia:ib))
         iploc=ia+im(1)-1                         !Index of CCF peak
         pval=s2(iploc)                           !Peak value
         if(pval.lt.minsync) exit
         do i=-3,+3                            !Remove 0.9 of a model CCF at
            k=iploc+2*hmod*i                   !this frequency from s2()
            if(k.ge.ia .and. k.le.ib) then
               s2(k)=max(0.,s2(k)-0.9*pval*xdb(i))
            endif
         enddo
         ncand=ncand+1
         candidates(ncand,1)=df2*iploc         !Candidate frequency
         candidates(ncand,2)=pval              !Rough estimate of SNR
         candidates(ncand,5)=sbase(iploc)
      enddo
      end associate
      return
   end subroutine get_candidates_fst4

   subroutine fst4_sync_search(this,c2,nfft2,hmod,fs2,nss,ntrperiod,nsyncoh,   &
        emedelay,sbest,fcbest,isbest)
      class(fst4_decoder), intent(inout) :: this
      complex c2(0:nfft2-1)
      integer hmod

      isbest=-1
      fcbest=0.
      sbest=0.
      last_start=nfft2-160*nss
      nspsec=int(fs2)
      baud=fs2/real(nss)
      fc1=0.0
      if(emedelay.lt.0.1) then  ! search offsets from 0 s to 2 s
         is0=1.5*nspsec
         ishw=1.5*nspsec
      else      ! search plus or minus 1.5 s centered on emedelay
         is0=nint((emedelay+1.0)*nspsec)
         ishw=1.5*nspsec
      endif

      if(min(is0+ishw,last_start)<max(1,is0-ishw)) return
      sbest=-1.e30
      do if=-12,12
         fc=fc1 + 0.1*baud*if
         do istart=max(1,is0-ishw),min(is0+ishw,last_start),4*hmod
            call sync_fst4(this,c2,istart,fc,hmod,nsyncoh,nfft2,nss,   &
               ntrperiod,fs2,sync)
            if(sync.gt.sbest) then
               fcbest=fc
               isbest=istart
               sbest=sync
            endif
         enddo
      enddo

      fc1=fcbest
      is0=isbest
      ishw=4*hmod
      isst=1*hmod

      sbest=0.0
      do if=-7,7
         fc=fc1 + 0.02*baud*if
         do istart=max(1,is0-ishw),min(is0+ishw,last_start),isst
            call sync_fst4(this,c2,istart,fc,hmod,nsyncoh,nfft2,nss,   &
               ntrperiod,fs2,sync)
            if(sync.gt.sbest) then
               fcbest=fc
               isbest=istart
               sbest=sync
            endif
         enddo
      enddo
   end subroutine fst4_sync_search

   subroutine dopspread(this,itone,iwave,nsps,nmax,ndown,hmod,i0,fc,fmid,w50,status)

! On "plotspec" special request, compute Doppler spread for a decoded signal

      include 'fst4/fst4_params.f90'
      class(fst4_decoder), intent(inout) :: this
      complex, pointer :: cwave(:)       !Reconstructed complex signal
      complex(c_float_complex), pointer, contiguous :: g(:)
      real, pointer :: ss(:)              !Computed power spectrum of g(t)
      real unused_wave(1)
      integer itone(160)                     !Tones for this message
      integer*2 iwave(nmax)                  !Raw Rx data
      integer hmod                           !Modulation index
      integer, intent(out) :: status
      integer allocation_status
      fmid=-999.
      w50=0.
      nfft=2*nmax
      nwave=max(nmax,(NN+2)*nsps)
      call ensure_fft_buffer(this%dop_storage,this%dopgain,nfft,status)
      if(status/=0) return
      if(this%dop_length/=nfft.or..not.c_associated(this%dop_plan)) then
         !$omp critical(fftw)
         if(c_associated(this%dop_plan)) call fftwf_destroy_plan(this%dop_plan)
         this%dop_plan=fftwf_plan_dft_1d(nfft,this%dopgain,this%dopgain,FFTW_FORWARD,this%fft_flags)
         !$omp end critical(fftw)
         this%dop_length=nfft
      endif
      if(.not.c_associated(this%dop_plan)) then
         status=-1
         return
      endif
      if(associated(this%dopwave)) then
         if(size(this%dopwave)<nwave) then
            deallocate(this%dopwave)
            nullify(this%dopwave)
         endif
      endif
      if(.not.associated(this%dopwave)) then
         allocate(this%dopwave(0:nwave-1),stat=allocation_status)
         if(allocation_status/=0) then
            status=-1
            return
         endif
      endif
      cwave(0:)=>this%dopwave(0:nwave-1)
      g(0:)=>this%dopgain(0:nfft-1)
      unused_wave=0
      fsample=12000.0
      call gen_fst4wave_owned(this%wavegen,itone,NN,nsps,nwave,fsample,hmod,fc,1,cwave,unused_wave)
      fac=1.0/32768
      do i=0,nmax-1
         j=modulo(i-i0*ndown,nwave)
         g(i)=fac*real(iwave(i+1))*conjg(cwave(j))
      enddo
      g(nmax:)=0.
      call fftwf_execute_dft(this%dop_plan,g,g)

      df=12000.0/nfft
      ia=1.0/df
      smax=0.
      do i=-ia,ia                        !Find smax in +/- 1 Hz around 0.
         j=i
         if(j.lt.0) j=i+nfft
         s=real(g(j))**2 + aimag(g(j))**2
         smax=max(s,smax)
      enddo

      ia=10.1/df
      if(associated(this%dopss)) then
         if(size(this%dopss)<2*ia+1) then
            deallocate(this%dopss)
            nullify(this%dopss)
         endif
      endif
      if(.not.associated(this%dopss)) allocate(this%dopss(2*ia+1))
      ss(-ia:)=>this%dopss(1:2*ia+1)
      sum1=0.
      sum2=0.
      nns=0
      do i=-ia,ia
         j=i
         if(j.lt.0) j=i+nfft
         ss(i)=(real(g(j))**2 + aimag(g(j))**2)/smax
         f=i*df
         if(f.ge.-4.0 .and. f.le.-2.0) then
            sum1=sum1 + ss(i)                  !Power between -2 and -4 Hz
            nns=nns+1
         else if(f.ge.2.0 .and. f.le.4.0) then
            sum2=sum2 + ss(i)                  !Power between +2 and +4 Hz
         endif
      enddo
      avg=min(sum1/nns,sum2/nns)               !Compute avg from smaller sum

      sum1=0.
      do i=-ia,ia
         f=i*df
         if(abs(f).le.1.0) sum1=sum1 + ss(i)-avg !Power in abs(f) < 1 Hz
      enddo

      ia=nint(1.0/df) + 1
      sum2=0.0
      xi1=-999
      xi2=-999
      xi3=-999
      sum2z=0.
      do i=-ia,ia                !Find freq range that has 50% of signal power
         sum2=sum2 + ss(i)-avg
         if(sum2.ge.0.25*sum1 .and. xi1.eq.-999.0) then
            xi1=i - 1 + (0.25*sum1-sum2)/(sum2-sum2z)
         endif
         if(sum2.ge.0.50*sum1 .and. xi2.eq.-999.0) then
            xi2=i - 1 + (sum2-0.50*sum1)/(sum2-sum2z)
         endif
         if(sum2.ge.0.75*sum1) then
            xi3=i - 1 + (sum2-0.75*sum1)/(sum2-sum2z)
            exit
         endif
         sum2z=sum2
      enddo
      xdiff=sqrt(1.0+(xi3-xi1)**2) !Keep small values from fluctuating too widely
      w50=xdiff*df                 !Compute Doppler spread
      fmid=xi2*df                  !Frequency midpoint of signal powere

      if(associated(this%spectrum_sink)) then
         nspectrum=2*ia+1
         if(allocated(this%spectrum)) then
            if(size(this%spectrum)<nspectrum) deallocate(this%spectrum)
         endif
         if(.not.allocated(this%spectrum)) allocate(this%spectrum(nspectrum))
         do i=-ia,ia
            y=0
            j=i+nint(xi2)
            if(abs(j*df).lt.10.0) y=0.99*ss(j)
            this%spectrum(i+ia+1)=y
         enddo
         call this%spectrum_sink(this%spectrum(1:nspectrum),df,-ia*df)
      endif

      return
   end subroutine dopspread

end module fst4_decode
