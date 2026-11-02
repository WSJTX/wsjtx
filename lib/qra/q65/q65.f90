module q65

  use q65_callers, only: Q65_MAX_CODEWORDS

  use, intrinsic :: iso_c_binding
  use fftw3
  use packjt77, only: pack77_state,unpack77_for_state
  use q65_codec, only: q65_enc,q65_intrinsics_ff,q65_dec,q65_dec_fullaplist

  integer, parameter :: NSTEP=8
  real, parameter :: PLOG_MIN=-242.0
  integer, parameter :: isync(22) = [1,9,12,13,15,22,23,26,27,33,35, &
       38,46,50,55,60,62,66,69,74,76,85]
  type :: q65_state
    integer :: iz0=0
    integer :: jz0=0
    integer :: ibwa=0
    integer :: ibwb=0
    integer :: ncw=0
    integer :: nsps=0
    integer :: mode_q65=0
    integer :: nfa=0
    integer :: nfb=0
    integer :: nqd=0
    integer :: idfbest=0
    integer :: idtbest=0
    integer :: ibw=0
    integer :: ndistbest=0
    integer :: maxiters=0
    integer :: max_drift=0
    integer :: istep=0
    integer :: nsmo=0
    integer :: lag1=0
    integer :: lag2=0
    integer :: npasses=0
    integer :: iseq=0
    integer :: ncand=0
    integer :: nrc=0
    integer :: i0=0
    integer :: j0=0
    integer :: LL0=0
    integer :: nhist=0,curve_average_count=0
    real :: df=0.
    real :: dtstep=0.
    real :: dtdec=0.
    real :: f0dec=0.
    real :: ftol=0.
    real :: plog=0.
    real :: drift=0.
    real :: curve_dt=0.
    integer :: apsym0(58)
    integer :: aph10(10)
    integer :: apmask1(78)
    integer :: apsymbols1(78)
    integer :: apmask(13)
    integer :: apsymbols(13)
    integer :: codewords(63,Q65_MAX_CODEWORDS)
    integer :: navg(0:1)
    integer :: nf0(100)
    real :: candidates(20,3)
    real :: sync(85)
    real, pointer :: s1(:,:)=>null()
    real, pointer :: s1w(:,:)=>null()
    real, pointer :: s1a(:,:,:)=>null()
    real, pointer :: ccf2(:)=>null()
    real, pointer :: ccf2_avg(:)=>null()
    logical :: lnewdat=.false.,spectra_valid=.false.,analytic_valid=.false.,legacy_output=.true.
    character(len=37) :: history(100)
    complex, allocatable :: symbol_fft(:),analytic(:)
    real, allocatable :: prepared(:,:),symbol_energies(:,:),correlation(:),list_correlation(:,:),best_list(:)
    real, allocatable :: timing(:),spectrum_average(:),snr_spectrum(:)
    integer, allocatable :: ordering(:),birdie_histogram(:)
    type(pack77_state), pointer :: knowledge=>null()
    integer(c_int64_t) :: symbol_plan=0,analytic_forward=0,analytic_inverse=0
    integer :: symbol_length=0,analytic_length=0,planning=-1
  end type
  type(q65_state), pointer :: q65_work=>null()
  type(q65_state), target, save :: legacy_work
  integer, pointer :: iz0=>null()
  integer, pointer :: jz0=>null()
  integer, pointer :: ibwa=>null()
  integer, pointer :: ibwb=>null()
  integer, pointer :: ncw=>null()
  integer, pointer :: nsps=>null()
  integer, pointer :: mode_q65=>null()
  integer, pointer :: nfa=>null()
  integer, pointer :: nfb=>null()
  integer, pointer :: nqd=>null()
  integer, pointer :: idfbest=>null()
  integer, pointer :: idtbest=>null()
  integer, pointer :: ibw=>null()
  integer, pointer :: ndistbest=>null()
  integer, pointer :: maxiters=>null()
  integer, pointer :: max_drift=>null()
  integer, pointer :: istep=>null()
  integer, pointer :: nsmo=>null()
  integer, pointer :: lag1=>null()
  integer, pointer :: lag2=>null()
  integer, pointer :: npasses=>null()
  integer, pointer :: iseq=>null()
  integer, pointer :: ncand=>null()
  integer, pointer :: nrc=>null()
  integer, pointer :: i0=>null()
  integer, pointer :: j0=>null()
  integer, pointer :: LL0=>null()
  integer, pointer :: nhist=>null()
  real, pointer :: df=>null()
  real, pointer :: dtstep=>null()
  real, pointer :: dtdec=>null()
  real, pointer :: f0dec=>null()
  real, pointer :: ftol=>null()
  real, pointer :: plog=>null()
  real, pointer :: drift=>null()
  real, pointer :: curve_dt=>null()
  integer, pointer :: apsym0(:)=>null()
  integer, pointer :: aph10(:)=>null()
  integer, pointer :: apmask1(:)=>null()
  integer, pointer :: apsymbols1(:)=>null()
  integer, pointer :: apmask(:)=>null()
  integer, pointer :: apsymbols(:)=>null()
  integer, pointer :: codewords(:,:)=>null()
  integer, pointer :: navg(:)=>null()
  integer, pointer :: nf0(:)=>null()
  real, pointer :: candidates(:,:)=>null()
  real, pointer :: sync(:)=>null()
  real, pointer :: s1(:,:)=>null()
  real, pointer :: s1w(:,:)=>null()
  real, pointer :: s1a(:,:,:)=>null()
  real, pointer :: ccf2(:)=>null()
  real, pointer :: ccf2_avg(:)=>null()
  logical, pointer :: lnewdat=>null()

contains


integer function q65_fft_flags() result(flags)
  integer npatience,nthreads
  common/patience/npatience,nthreads
  flags=FFTW_ESTIMATE
  if(npatience==1) flags=FFTW_ESTIMATE_PATIENT
  if(npatience==2) flags=FFTW_MEASURE
  if(npatience==3) flags=FFTW_PATIENT
  if(npatience==4) flags=FFTW_EXHAUSTIVE
end function

subroutine q65_check_planning()
  use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
  integer npatience,nthreads
  common/patience/npatience,nthreads
  if(q65_work%planning==npatience) return
  !$omp critical(fftw)
  call wsjt_tsan_acquire_fftw()
  if(q65_work%symbol_plan/=0) call sfftw_destroy_plan(q65_work%symbol_plan)
  if(q65_work%analytic_forward/=0) call sfftw_destroy_plan(q65_work%analytic_forward)
  if(q65_work%analytic_inverse/=0) call sfftw_destroy_plan(q65_work%analytic_inverse)
  call wsjt_tsan_release_fftw()
  !$omp end critical(fftw)
  q65_work%symbol_plan=0
  q65_work%analytic_forward=0
  q65_work%analytic_inverse=0
  q65_work%planning=npatience
  q65_work%spectra_valid=.false.
  q65_work%analytic_valid=.false.
end subroutine

subroutine q65_analytic(iwave,npts)
  use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
  integer, intent(in) :: npts
  integer(c_int16_t), intent(in) :: iwave(npts)
  integer nfft2,flags
  real fac
  call q65_check_planning()
  if(q65_work%analytic_length/=npts) then
     !$omp critical(fftw)
     call wsjt_tsan_acquire_fftw()
     if(q65_work%analytic_forward/=0) call sfftw_destroy_plan(q65_work%analytic_forward)
     if(q65_work%analytic_inverse/=0) call sfftw_destroy_plan(q65_work%analytic_inverse)
     call wsjt_tsan_release_fftw()
     !$omp end critical(fftw)
     q65_work%analytic_forward=0
     q65_work%analytic_inverse=0
     if(allocated(q65_work%analytic)) deallocate(q65_work%analytic)
     allocate(q65_work%analytic(0:npts-1))
     q65_work%analytic_length=npts
     q65_work%analytic_valid=.false.
  endif
  if(q65_work%analytic_valid) return
  nfft2=npts/2
  flags=q65_fft_flags()
  if(q65_work%analytic_forward==0) then
     !$omp critical(fftw)
     call wsjt_tsan_acquire_fftw()
     call sfftw_plan_dft_1d(q65_work%analytic_forward,npts,q65_work%analytic, &
          q65_work%analytic,FFTW_FORWARD,flags)
     call sfftw_plan_dft_1d(q65_work%analytic_inverse,nfft2,q65_work%analytic, &
          q65_work%analytic,FFTW_BACKWARD,flags)
     call wsjt_tsan_release_fftw()
     !$omp end critical(fftw)
  endif
  fac=2.0/(32767.0*npts)
  q65_work%analytic=fac*iwave
  call sfftw_execute(q65_work%analytic_forward)
  q65_work%analytic(nfft2/2+1:nfft2-1)=0.
  q65_work%analytic(0)=0.5*q65_work%analytic(0)
  call sfftw_execute(q65_work%analytic_inverse)
  q65_work%analytic_valid=.true.
end subroutine

subroutine q65_unpack(c77,nrx,decoded,success)
  use packjt77, only: unpack77
  character(len=77), intent(in) :: c77
  integer, intent(in) :: nrx
  character(len=37), intent(out) :: decoded
  logical, intent(out) :: success
  if(associated(q65_work)) then
     if(associated(q65_work%knowledge)) then
        call unpack77_for_state(q65_work%knowledge,c77,nrx,decoded,success)
        return
     endif
  endif
  call unpack77(c77,nrx,decoded,success)
end subroutine

subroutine q65_select(state)
  type(q65_state), target, intent(inout) :: state
  q65_work=>state
  iz0=>state%iz0
  jz0=>state%jz0
  ibwa=>state%ibwa
  ibwb=>state%ibwb
  ncw=>state%ncw
  nsps=>state%nsps
  mode_q65=>state%mode_q65
  nfa=>state%nfa
  nfb=>state%nfb
  nqd=>state%nqd
  idfbest=>state%idfbest
  idtbest=>state%idtbest
  ibw=>state%ibw
  ndistbest=>state%ndistbest
  maxiters=>state%maxiters
  max_drift=>state%max_drift
  istep=>state%istep
  nsmo=>state%nsmo
  lag1=>state%lag1
  lag2=>state%lag2
  npasses=>state%npasses
  iseq=>state%iseq
  ncand=>state%ncand
  nrc=>state%nrc
  i0=>state%i0
  j0=>state%j0
  LL0=>state%LL0
  nhist=>state%nhist
  df=>state%df
  dtstep=>state%dtstep
  dtdec=>state%dtdec
  f0dec=>state%f0dec
  ftol=>state%ftol
  plog=>state%plog
  drift=>state%drift
  curve_dt=>state%curve_dt
  apsym0=>state%apsym0
  aph10=>state%aph10
  apmask1=>state%apmask1
  apsymbols1=>state%apsymbols1
  apmask=>state%apmask
  apsymbols=>state%apsymbols
  codewords=>state%codewords
  navg=>state%navg
  nf0=>state%nf0
  candidates=>state%candidates
  sync=>state%sync
  s1=>state%s1
  s1w=>state%s1w
  s1a=>state%s1a
  ccf2=>state%ccf2
  ccf2_avg=>state%ccf2_avg
  lnewdat=>state%lnewdat
end subroutine

subroutine q65_initialize_state(state)
  type(q65_state), intent(inout) :: state
  state%apsym0=0
  state%aph10=0
  state%apmask1=0
  state%apsymbols1=0
  state%apmask=0
  state%apsymbols=0
  state%codewords=0
  state%navg=0
  state%nf0=0
  state%history=''
  state%candidates=0.
  state%sync=-22.0/63.0
  state%sync(isync)=1.
end subroutine

subroutine q65_release_state(state)
  use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw
  type(q65_state), intent(inout) :: state
  !$omp critical(fftw)
  call wsjt_tsan_acquire_fftw()
  if(state%symbol_plan/=0) call sfftw_destroy_plan(state%symbol_plan)
  if(state%analytic_forward/=0) call sfftw_destroy_plan(state%analytic_forward)
  if(state%analytic_inverse/=0) call sfftw_destroy_plan(state%analytic_inverse)
  call wsjt_tsan_release_fftw()
  !$omp end critical(fftw)
  state%symbol_plan=0
  state%analytic_forward=0
  state%analytic_inverse=0
  if(associated(state%s1)) deallocate(state%s1)
  if(associated(state%s1w)) deallocate(state%s1w)
  if(associated(state%s1a)) deallocate(state%s1a)
  if(associated(state%ccf2)) deallocate(state%ccf2)
  if(associated(state%ccf2_avg)) deallocate(state%ccf2_avg)
end subroutine


subroutine q65_dec0(iavg,iwave,ntrperiod,nfqso,ntol,lclearave,  &
     emedelay,xdt,f0,snr1,width,dat4,snr2,idec,stageno)

! Top-level routine in q65 module
!   - Compute symbol spectra
!   - Attempt sync and q3 decode using all 85 symbols
!   - If that fails, try sync with 22 symbols and standard q[0124] decode
  
! Input:  iavg                   0 for single-period decode, 1 for average
!         iwave(0:nmax-1)        Raw data
!         ntrperiod              T/R sequence length (s)
!         nfqso                  Target frequency (Hz)
!         ntol                   Search range around nfqso (Hz)
!         lclearave              Flag to clear the accumulating array
!         emedelay               Extra delay for EME signals
! Output: xdt                    Time offset from nominal (s)
!         f0                     Frequency of sync tone
!         snr1                   Relative SNR of sync signal
!         width                  Estimated Doppler spread
!         dat4(13)               Decoded message as 13 six-bit integers
!         snr2                   Estimated SNR of decoded signal
!         idec                   Flag for decoding results
!            -1  No decode
!             0  No AP
!             1  "CQ        ?    ?"
!             2  "Mycall    ?    ?"
!             3  "MyCall HisCall ?"

  use packjt77
  use timer_module, only: timer

  parameter (LN=2176*63)           !LN=LL*NN; LL=64*(mode_q65+2), NN=63
  integer*2 iwave(0:12000*ntrperiod-1)   !Raw data
  integer dat4(13)
  character*37 decoded
  logical lclearave
  real, pointer :: s3(:,:),ccf1(:)

  integer w3t
  integer w3f
  integer mm
  integer stageno

  NN=63

! Set some parameters and allocate storage for large arrays
  irc=-2
  nrc=-2
  idec=-1
  snr1=0.
  dat4=0
  LL=64*(2+mode_q65)
  nfft=nsps
  df=12000.0/nfft                        !Freq resolution = baud
  istep=nsps/NSTEP
  iz=5000.0/df                           !Uppermost frequency bin, at 5000 Hz
  ! Cover every complete symbol FFT window in the received period.
  ! Keep jz odd: q65_symspec interpolates even columns between odd ones.
  jz=(ntrperiod*12000-nsps)/istep + 1
  ftol=ntol
  ia=ntol/df
  ia2=max(ia,10*mode_q65,nint(100.0/df))
!  nsmo=int(0.7*mode_q65*mode_q65)
  nsmo=int(0.5*mode_q65*mode_q65)
  if(nsmo.lt.1) nsmo=1

  if(allocated(q65_work%symbol_energies)) then
     if(size(q65_work%symbol_energies,1)/=LL) deallocate(q65_work%symbol_energies)
  endif
  if(.not.allocated(q65_work%symbol_energies)) allocate(q65_work%symbol_energies(-64:LL-65,63))
  if(allocated(q65_work%correlation)) then
     if(size(q65_work%correlation)/=2*ia2+1) deallocate(q65_work%correlation)
  endif
  if(.not.allocated(q65_work%correlation)) allocate(q65_work%correlation(-ia2:ia2))
  s3=>q65_work%symbol_energies
  ccf1=>q65_work%correlation
  if(LL.ne.LL0 .or. iz.ne.iz0 .or. jz.ne.jz0 .or. lclearave) then
     if(associated(s1)) deallocate(s1)
     allocate(s1(iz,jz))
     if(associated(s1a)) deallocate(s1a)
     allocate(s1a(iz,jz,0:1))
     if(associated(ccf2)) deallocate(ccf2)
     allocate(ccf2(iz))
     if(associated(ccf2_avg)) deallocate(ccf2_avg)
     allocate(ccf2_avg(iz))
     s1=0.
     s1a=0.
     navg=0
     LL0=LL
     iz0=iz
     jz0=jz
     q65_work%s1=>s1
     q65_work%s1a=>s1a
     q65_work%ccf2=>ccf2
     q65_work%ccf2_avg=>ccf2_avg
     ccf2=0.
     ccf2_avg=0.
     q65_work%spectra_valid=.false.
     lclearave=.false.
  endif
  ccf1=0.
  if(iavg.eq.0) ccf2=0.
  dtstep=nsps/(NSTEP*12000.0)                 !Step size in seconds
  lag1=-1.0/dtstep
  lag2=1.0/dtstep + 0.9999
  if(nsps.ge.3600 .and. emedelay.gt.0) lag2=5.5/dtstep + 0.9999  !Include EME
  if(ntrperiod.eq.15 .and. nsps.ge.900 .and. emedelay.gt.0) lag2=4/dtstep + 0.9999  !EME Q65-15
  j0=0.5/dtstep
  if(nsps.ge.7200) j0=1.0/dtstep              !Nominal start-signal index

  s3=0.
!  if(iavg.eq.0 .and. lnewdat) then
  call q65_check_planning()
  if(iavg.eq.0) then
     call timer('q65_syms',0)
! Compute symbol spectra with NSTEP time bins per symbol
     if(.not.q65_work%spectra_valid) then
        call q65_symspec(iwave,ntrperiod*12000,iz,jz,s1)
        if(allocated(q65_work%prepared)) then
           if(any(shape(q65_work%prepared)/=shape(s1))) deallocate(q65_work%prepared)
        endif
        if(.not.allocated(q65_work%prepared)) allocate(q65_work%prepared(iz,jz))
        q65_work%prepared=s1
        q65_work%spectra_valid=.true.
     else
        s1=q65_work%prepared
     endif
     call timer('q65_syms',1)
!     lnewdat=.false.
  else
     s1=s1a(:,:,iseq)
  endif

  i0=nint(nfqso/df)                             !Target QSO frequency
  dat4=0
  if(ncw.gt.0 .and. iavg.le.1) then
! Try list decoding via "Deep Likelihood".
     call timer('ccf_85  ',0)
! Try to synchronize using all 85 symbols
     call q65_ccf_85(s1,iz,jz,nfqso,ia,ia2,ipk,jpk,f0,xdt,imsg_best,   &
          better,ccf1)
     call timer('ccf_85  ',1)

     if(better.ge.1.10 .or. mode_q65.ge.8) then
        call timer('list_dec',0)
        call q65_dec_q3(s1,iz,jz,s3,LL,ipk,jpk,snr2,dat4,idec,decoded)
        call timer('list_dec',1)
     endif
! If idec=3 we have a q3 decode.  Continue to compute sync curve for plotting.
  endif

! Get 2d CCF and ccf2 using sync symbols only
  if(iavg.eq.0) then
     call timer('ccf_22a ',0)
     call q65_ccf_22(s1,iz,jz,nfqso,ntol,ipk,jpk,f0a,xdta,ccf2)
     call timer('ccf_22a ',1)
  endif

! Get 2d CCF and ccf2_avg using sync symbols only
  if(iavg.ge.1) then
     call timer('ccf_22b ',0)
     call q65_ccf_22(s1,iz,jz,nfqso,ntol,ipk,jpk,f0a,xdta,ccf2_avg)
     call timer('ccf_22b ',1)
  endif
  if(idec.lt.0) then
     f0=f0a
     xdt=xdta
  endif

! Estimate rms on ccf2 baseline
  call q65_sync_curve(ccf2,1,iz,rms2)
  smax=maxval(ccf2)
  snr1=0.
  if(rms2.gt.0) snr1=smax/rms2

  if(idec.le.0) then
! The q3 decode attempt failed. Copy synchronized symbol energies from s1
! into s3 and prepare to try a more general decode.
     call q65_s1_to_s3(s1,iz,jz,ipk,jpk,LL,mode_q65,sync,s3)
  endif

  smax=maxval(ccf1)

! Estimate frequency spread
  i1=-9999
  i2=-9999
  do i=-ia,ia
     if(i1.eq.-9999 .and. ccf1(i).ge.0.5*smax) i1=i
     if(i2.eq.-9999 .and. ccf1(-i).ge.0.5*smax) i2=-i
  enddo
  width=df*(i2-i1)
  if(ncw.eq.0) ccf1=0.
  call q65_sync_curve(ccf2_avg,1,iz,rms1)
  call q65_sync_curve(ccf2,1,iz,rms2)
  curve_dt=xdt
  q65_work%curve_average_count=navg(iseq)
  if(q65_work%legacy_output) call q65_write_red(iz,xdt,ccf2_avg,ccf2)

  if(idec.lt.0 .and. (iavg.eq.0 .or. iavg.eq.2)) then
     call q65_dec_q012(s3,LL,snr2,dat4,idec,decoded)
  endif

  if(idec.lt.0 .and. max_drift.eq.50 .and. stageno.eq.5) then

     if(associated(s1w)) then
        if(any(shape(s1w)/=[iz,jz])) deallocate(s1w)
     endif
     if(.not.associated(s1w)) allocate(s1w(iz,jz))
     q65_work%s1w=>s1w
	 
     s1w=s1
     do w3t=1,jz
        do w3f=1,iz
           ! Drift is measured over the 85-symbol transmission.
           mm=w3f + nint(drift*w3t/(85*NSTEP*df))
           if(mm.ge.1 .and. mm.le.iz) then
              s1w(w3f,w3t)=s1(mm,w3t)
           endif
        end do
     end do

     if(ncw.gt.0 .and. iavg.le.1) then
        ! Try list decoding via "Deep Likelihood".
        call timer('ccf_85  ',0)
        ! Try to synchronize using all 85 symbols
        call q65_ccf_85(s1w,iz,jz,nfqso,ia,ia2,ipk,jpk,f0,xdt,imsg_best,   &
             better,ccf1)
        call timer('ccf_85  ',1)

! nsubmode  is Tone-spacing indicator, 0-4 for A-E: a 0; b 1; c 2; d 3; e 4.
! and mode_q65=2**nsubmode
        if(better.ge.1.10) then
           !     if(better.ge.1.04 .or. mode_q65.ge.8) then
           !     if(better.ge.1.10 .or. mode_q65.ge.8) then  ORIGINAL
           call timer('list_dec',0)
           call q65_dec_q3(s1w,iz,jz,s3,LL,ipk,jpk,snr2,dat4,idec,decoded)
           call timer('list_dec',1)
        endif ! if(better.ge.1.10)
     endif    ! if(ncw.gt.0 .and. iavg.le.1)
     ! If idec=3 we have a q3 decode.  Continue to compute sync curve for plotting.

     if(idec.eq.3) then
        idec=5
     endif

  endif        ! if(idec.lt.0 .and. max_drift.eq.50 .and. stageno.eq.5)

  return
end subroutine q65_dec0

subroutine q65_clravg

! Clear the averaging array to start a new average.

  if(associated(s1a)) s1a(:,:,iseq)=0.
  navg(iseq)=0
  
  return
end subroutine q65_clravg

subroutine q65_symspec(iwave,nmax,iz,jz,s1)
  use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw

! Compute symbol spectra with NSTEP time-steps per symbol.
  
  integer*2 iwave(0:nmax-1)              !Raw data
  real s1(iz,jz)
  complex, pointer :: c0(:)
  integer flags

  nfft=nsps
  if(.not.allocated(q65_work%symbol_fft)) allocate(q65_work%symbol_fft(0:41472))
  c0=>q65_work%symbol_fft
  call q65_check_planning()
  if(q65_work%symbol_length/=nfft.or.q65_work%symbol_plan==0) then
     !$omp critical(fftw)
     call wsjt_tsan_acquire_fftw()
     if(q65_work%symbol_plan/=0) call sfftw_destroy_plan(q65_work%symbol_plan)
     flags=q65_fft_flags()
     call sfftw_plan_dft_r2c_1d(q65_work%symbol_plan,nfft,c0,c0,flags)
     call wsjt_tsan_release_fftw()
     !$omp end critical(fftw)
     q65_work%symbol_length=nfft
  endif
  fac=1/32767.0
  do j=1,jz,2                     !Compute symbol spectra at 2*step size
     i1=(j-1)*istep
     i2=i1+nsps-1
     k=-1
     do i=i1,i2,2          !Load iwave data into complex array c0, for r2c FFT
        xx=0.
        yy=0.
        if(i<nmax) xx=iwave(i)
        if(i+1<nmax) yy=iwave(i+1)
        k=k+1
        c0(k)=fac*cmplx(xx,yy)
     enddo
     c0(k+1:nfft-1)=0.
     call sfftw_execute(q65_work%symbol_plan)
     do i=1,iz
        s1(i,j)=real(c0(i))**2 + aimag(c0(i))**2
     enddo
! For large Doppler spreads, should we smooth the spectra here?
     if(nsmo.le.1) nsmo=0
     do i=1,nsmo
        call smo121(s1(1:iz,j),iz)
     enddo
! Interpolate to fill in the skipped-over spectra.
     if(j.ge.3) s1(1:iz,j-1)=0.5*(s1(1:iz,j-2)+s1(1:iz,j))
  enddo
  if(lnewdat) then
     navg(iseq)=navg(iseq) + 1
     ntc=min(navg(iseq),4)               !Averaging time constant in sequences
     u=1.0/ntc
     s1a(:,:,iseq)=u*s1 + (1.0-u)*s1a(:,:,iseq)
   endif

  return
end subroutine q65_symspec

subroutine q65_dec_q3(s1,iz,jz,s3,LL,ipk,jpk,snr2,dat4,idec,decoded)

! Copy synchronized symbol energies from s1 into s3, then attempt a q3 decode.

  character*37 decoded
  integer dat4(13)
  real s1(iz,jz)
  real s3(-64:LL-65,63)

  call q65_s1_to_s3(s1,iz,jz,ipk,jpk,LL,mode_q65,sync,s3)

  nsubmode=0
  if(mode_q65.eq.2) nsubmode=1
  if(mode_q65.eq.4) nsubmode=2
  if(mode_q65.eq.8) nsubmode=3
  if(mode_q65.eq.16) nsubmode=4
  if(mode_q65.eq.32) nsubmode=5
  baud=12000.0/nsps

  do ibw=ibwa,ibwb
     b90=1.72**ibw
     b90ts=b90/baud
     call q65_dec1(s3,nsubmode,b90ts,esnodb,irc,dat4,decoded)
     nrc=irc
     if(irc.ge.0) then
        snr2=esnodb - db(2500.0/baud) + 3.0     !Empirical adjustment
        idec=3
        exit
     endif
  enddo

  return
end subroutine q65_dec_q3

subroutine q65_dec_q012(s3,LL,snr2,dat4,idec,decoded)

! Do separate passes attempting q0, q1, q2 decodes.
  
  character*37 decoded
  character*78 c78
  integer dat4(13)
  real s3(-64:LL-65,63)
  logical lapcqonly

  nsubmode=0
  if(mode_q65.eq.2) nsubmode=1
  if(mode_q65.eq.4) nsubmode=2
  if(mode_q65.eq.8) nsubmode=3
  if(mode_q65.eq.16) nsubmode=4
  
  baud=12000.0/nsps
  iaptype=0
  nQSOprogress=0    !### TEMPORARY  ? ###
  ncontest=0
  lapcqonly=.false.
  
  do ipass=0,npasses                  !Loop over AP passes
     apmask=0                         !Try first with no AP information
     apsymbols=0
     if(ipass.ge.1) then
        ! Subsequent passes use AP information appropiate for nQSOprogress
        call q65_ap(nQSOprogress,ipass,ncontest,.false.,lapcqonly,iaptype, &
             apsym0,apmask1,apsymbols1)
        write(c78,1050) apmask1
1050    format(78i1)
        read(c78,1060) apmask
1060    format(13b6.6)
        write(c78,1050) apsymbols1
        read(c78,1060) apsymbols
     endif

     do ibw=ibwa,ibwb
        b90=1.72**ibw
        b90ts=b90/baud
        call q65_dec2(s3,nsubmode,b90ts,esnodb,irc,dat4,decoded)
        nrc=irc
        if(irc.ge.0) then
           snr2=esnodb - db(2500.0/baud) + 3.0     !Empirical adjustment
           idec=iaptype
           go to 100
        endif
     enddo
  enddo

100 return
end subroutine q65_dec_q012

subroutine q65_ccf_85(s1,iz,jz,nfqso,ia,ia2,ipk,jpk,f0,xdt,imsg_best,   &
     better,ccf1)

! Attempt synchronization using all 85 symbols, in advance of an
! attempt at q3 decoding.  Return ccf1 for the "red sync curve".
  
  real s1(iz,jz)
  real, pointer :: ccf(:,:),best(:)
  real ccf1(-ia2:ia2)
  integer ijpk(2)
  integer itone(85)

  if(allocated(q65_work%list_correlation)) then
     if(size(q65_work%list_correlation,1)/=2*ia2+1) deallocate(q65_work%list_correlation)
  endif
  if(.not.allocated(q65_work%list_correlation)) allocate(q65_work%list_correlation(-ia2:ia2,-53:214))
  if(.not.allocated(q65_work%best_list)) allocate(q65_work%best_list(Q65_MAX_CODEWORDS))
  ccf=>q65_work%list_correlation
  best=>q65_work%best_list(:ncw)
  ipk=0
  jpk=0
  ccf_best=0.
  imsg_best=-1
  do imsg=1,ncw
     i=1
     k=0
     do j=1,85
        if(j.eq.isync(i)) then
           i=i+1
           itone(j)=0
        else
           k=k+1
           itone(j)=codewords(k,imsg) + 1
        endif
     enddo

! Compute 2D ccf using all 85 symbols in the list message
     ccf=0.
     iia=200.0/df

     do lag=lag1,lag2
        do k=1,85
           j=j0 + NSTEP*(k-1) + 1 + lag
           if(j.ge.1 .and. j.le.jz) then
              do i=-ia2,ia2
                 ii=i0+mode_q65*itone(k)+i
                 if(ii.ge.iia .and. ii.le.iz) ccf(i,lag)=ccf(i,lag) + s1(ii,j)
              enddo
           endif
        enddo
     enddo

     ccfmax=maxval(ccf(-ia:ia,:))
     if(ccfmax.gt.ccf_best) then
        ccf_best=ccfmax
        ijpk=maxloc(ccf(-ia:ia,:))
        ipk=ijpk(1)-ia-1
        jpk=ijpk(2)-53-1
        f0=nfqso + ipk*df
        xdt=jpk*dtstep
        imsg_best=imsg
        ccf1=ccf(:,jpk)
     endif
     best(imsg)=ccfmax
  enddo  ! imsg

  better=0.
  if(imsg_best.gt.0) then
     best(imsg_best)=0.
     if(maxval(best)>0.) better=ccf_best/maxval(best)
  endif

  return
end subroutine q65_ccf_85

subroutine q65_ccf_22(s1,iz,jz,nfqso,ntol,ipk,jpk,f0,xdt,ccf2)

! Attempt synchronization using only the 22 sync symbols.  Return ccf2
! for the "orange sync curve".

  real s1(iz,jz)
  real ccf2(iz)                               !Orange sync curve
  real tmp(20,3)
  real, pointer :: xdt2(:),s1avg(:)
  integer, pointer :: indx(:)

  if(allocated(q65_work%timing)) then
     if(size(q65_work%timing)<iz) deallocate(q65_work%timing,q65_work%spectrum_average,q65_work%ordering)
  endif
  if(.not.allocated(q65_work%timing)) then
     allocate(q65_work%timing(iz),q65_work%spectrum_average(iz),q65_work%ordering(iz))
  endif
  xdt2=>q65_work%timing(:iz)
  s1avg=>q65_work%spectrum_average(:iz)
  indx=>q65_work%ordering(:iz)

  ia=max(nfa,100)/df
  ib=min(nfb,4900)/df
  if(max_drift.ne.0) then
     ia=max(nint(100/df),nint((nfqso-ntol)/df))
     ib=min(nint(4900/df),nint((nfqso+ntol)/df))
  endif
  if(ia.ge.ib) ia=ib-ntol/df
  ia=max(1,ia)
  ib=min(iz,ib)
  if(ia>ib) then
     ipk=0
     jpk=0
     f0=nfqso
     xdt=0.
     drift=0.
     ncand=0
     candidates=0.
     ccf2=0.
     return
  endif

  do i=ia,ib
     s1avg(i)=sum(s1(i,1:jz))
  enddo

  ccfbest=0.
  ibest=0
  lagpk=0
  lagbest=0
  idrift_max=0
  idrift_best=0

  do i=ia,ib
     ccfmax=0.
     do lag=lag1,lag2
        do idrift=-max_drift,max_drift
           ccft=0.
           do kk=1,22
              k=isync(kk)
              ii=i + nint(idrift*(k-43)/85.0)
              if(ii.lt.1 .or. ii.gt.iz) cycle
              n=NSTEP*(k-1) + 1
              j=n+lag+j0
              if(j.ge.1 .and. j.le.jz) ccft=ccft + s1(ii,j)
           enddo  ! kk
           ccft=ccft - (22.0/jz)*s1avg(i)
           if(ccft.gt.ccfmax) then
              ccfmax=ccft
              lagpk=lag
              idrift_max=idrift
           endif
        enddo  ! idrift
     enddo  ! lag

     ccf2(i)=ccfmax
     xdt2(i)=lagpk*dtstep

     if(ccfmax.gt.ccfbest .and. abs(i*df-nfqso).le.ftol) then
        ccfbest=ccfmax
!        snrbest=snr  ! snrbest not used. snr may be uninitialized.
        ibest=i
        lagbest=lagpk
        idrift_best=idrift_max
     endif
  enddo  ! i

! Parameters for the top candidate:
  ipk=ibest - i0
  jpk=lagbest
  f0=nfqso + ipk*df
  xdt=jpk*dtstep
  drift=df*idrift_best
  ccf2(:ia)=0.
  ccf2(ib:)=0.

! Save parameters for best candidates
  jzz=ib-ia+1
  call indexx(ccf2(ia:ib),jzz,indx)

  call pctile(ccf2(ia:ib),jzz,50,ave)
  call pctile(ccf2(ia:ib),jzz,84,base)
  rms=base-ave
  ncand=0
  candidates=0.
  if(rms<=0.) return
  maxcand=20
  do j=1,20
     k=jzz-j+1
     if(k.lt.1 .or. k.gt.iz) cycle
     i=indx(k)+ia-1
     f=i*df
     i3=max(1, i-mode_q65)
     i4=min(iz,i+mode_q65)
     biggest=maxval(ccf2(i3:i4))
     if(ccf2(i).ne.biggest) cycle
     snr=(ccf2(i)-ave)/rms
     if(snr.lt.6.0) exit
     ncand=ncand+1
     candidates(ncand,1)=snr
     candidates(ncand,2)=xdt2(i)
     candidates(ncand,3)=f
     if(ncand.ge.maxcand) exit
  enddo

! Resort the candidates back into frequency order
  tmp(1:ncand,1:3)=candidates(1:ncand,1:3)
  candidates=0.
  call indexx(tmp(1:ncand,3),ncand,indx)
  do i=1,ncand
     candidates(i,1:3)=tmp(indx(i),1:3)
  enddo

  return
end subroutine q65_ccf_22

subroutine q65_dec1(s3,nsubmode,b90ts,esnodb,irc,dat4,decoded)

! Attmpt a full-AP list decode.

  use packjt77
  real s3(64*(2+2**nsubmode),63)
  real s3prob(0:63,63)                   !Symbol-value probabilities
  integer dat4(13)
  character c77*77,decoded*37
  logical unpk77_success

  nFadingModel=1
  decoded='                                     '
  if(ncw<=0.or.maxval(s3)<=0.) then
     dat4=0
     esnodb=0.
     irc=-1
     return
  endif
  call q65_intrinsics_ff(s3,nsubmode,b90ts,nFadingModel,s3prob)
  call q65_dec_fullaplist(s3,s3prob,codewords,ncw,esnodb,dat4,plog,irc)
  if(sum(dat4).le.0) irc=-2
  if(irc.ge.0 .and. plog.gt.PLOG_MIN) then
     write(c77,1000) dat4(1:12),dat4(13)/2
1000 format(12b6.6,b5.5)
     call q65_unpack(c77,0,decoded,unpk77_success) !Unpack to get msgsent
  else
     irc=-1
  endif
  nrc=irc

  return
end subroutine q65_dec1

subroutine q65_dec2(s3,nsubmode,b90ts,esnodb,irc,dat4,decoded)

! Attempt a q0, q1, or q2 decode using spcified AP information.

  use packjt77
  real s3(64*(2+2**nsubmode),63)
  real s3prob(0:63,63)                   !Symbol-value probabilities
  integer dat4(13)
  character c77*77,decoded*37
  logical unpk77_success

  nFadingModel=1
  decoded='                                     '
  dat4=0
  esnodb=0.
  irc=-1
  if(maxval(s3)<=0.) return
  call q65_intrinsics_ff(s3,nsubmode,b90ts,nFadingModel,s3prob)
  call q65_dec(s3,s3prob,APmask,APsymbols,maxiters,esnodb,dat4,irc)
  if(sum(dat4).le.0) irc=-2
  nrc=irc
  if(irc.ge.0) then
     write(c77,1000) dat4(1:12),dat4(13)/2
1000 format(12b6.6,b5.5)
     call q65_unpack(c77,0,decoded,unpk77_success) !Unpack to get msgsent
  endif

  return
end subroutine q65_dec2

subroutine q65_s1_to_s3(s1,iz,jz,ipk,jpk,LL,mode_q65,sync,s3)

! Copy synchronized symbol energies from s1 (or s1a) into s3.

  real s1(iz,jz)
  real s3(-64:LL-65,63)
  real sync(85)                          !sync vector
  
  i1=i0+ipk-64 + mode_q65
  i2=i1+LL-1
  if(i1.ge.1 .and. i2.le.iz) then
     j=j0+jpk-7
     n=0
     do k=1,85
        j=j+8
        if(sync(k).gt.0.0) then
           cycle
        endif
        n=n+1
        if(j.ge.1 .and. j.le.jz) s3(-64:LL-65,n)=s1(i1:i2,j)
     enddo
  endif
  call q65_bzap(s3,LL)                   !Zap birdies
  
  return
end subroutine q65_s1_to_s3

subroutine q65_write_red(iz,xdt,ccf2_avg,ccf2)

! Write data for the red and orange sync curves to LU 17.

  real ccf2_avg(iz)
  real ccf2(iz)

  i1=max(1,nint(nfa/df))
  i2=min(iz,int(nfb/df))
  y0=minval(ccf2(i1:i2))
  y0_avg=minval(ccf2_avg(i1:i2))
  g=0.4
  g_avg=0.
  if(navg(iseq).ge.2) g_avg=g
  rewind 17
  write(17,1000) xdt,g_avg*minval(ccf2_avg),g_avg*maxval(ccf2_avg)
  do i=i1,i2
     freq=i*df
     y1=g_avg*(ccf2_avg(i)-y0_avg)
     y2=g*(ccf2(i)-y0)
     write(17,1000) freq,y1,y2
1000 format(f10.3,2f15.6)
  enddo
  flush(17)

  return
end subroutine q65_write_red

subroutine q65_sync_curve(ccf1,ia,ib,rms1)

! Condition the red or orange sync curve for plotting.

  real ccf1(ia:ib)

  ic=(ib-ia)/8;
  nsum=2*(ic+1)

  base1=(sum(ccf1(ia:ia+ic)) + sum(ccf1(ib-ic:ib)))/nsum
  ccf1=ccf1-base1
  sq=dot_product(ccf1(ia:ia+ic),ccf1(ia:ia+ic)) +         &
       dot_product(ccf1(ib-ic:ib),ccf1(ib-ic:ib))
  rms1=0.
  if(nsum.gt.0) rms1=sqrt(sq/nsum)
  if(rms1.gt.0.0) ccf1=ccf1/rms1
!  smax1=maxval(ccf1)
!  if(smax1.gt.10.0) ccf1=10.0*ccf1/smax1

  return
end subroutine q65_sync_curve

subroutine q65_bzap(s3,LL)

  parameter (NBZAP=15)
  real s3(-64:LL-65,63)
  integer ipk1(1)
  integer, pointer :: hist(:)

  if(allocated(q65_work%birdie_histogram)) then
     if(size(q65_work%birdie_histogram)<LL) deallocate(q65_work%birdie_histogram)
  endif
  if(.not.allocated(q65_work%birdie_histogram)) allocate(q65_work%birdie_histogram(-64:LL-65))
  hist(-64:)=>q65_work%birdie_histogram(-64:LL-65)
  hist=0
  do j=1,63
     ipk1=maxloc(s3(:,j))
     i=ipk1(1) - 65
     hist(i)=hist(i)+1
  enddo
  if(maxval(hist).gt.NBZAP) then
     do i=-64,LL-65
        if(hist(i).gt.NBZAP) s3(i,1:63)=1.0
     enddo
  endif

  return
end subroutine q65_bzap

subroutine q65_snr(dat4,dtdec,f0dec,mode_q65,snr2)

! Estimate SNR of a decoded transmission by aligning the spectra of
! all 85 symbols.
  
  integer dat4(13)
  integer codeword(63)
  integer itone(85)
  real, pointer :: spec(:)

  if(allocated(q65_work%snr_spectrum)) then
     if(size(q65_work%snr_spectrum)<iz0) deallocate(q65_work%snr_spectrum)
  endif
  if(.not.allocated(q65_work%snr_spectrum)) allocate(q65_work%snr_spectrum(iz0))
  spec=>q65_work%snr_spectrum(:iz0)
  call q65_enc(dat4,codeword)
  i=1
  k=0
  do j=1,85
     if(j.eq.isync(i)) then
        i=i+1
        itone(j)=0
     else
        k=k+1
        itone(j)=codeword(k) + 1
     endif
  enddo

  spec=0.
  lagpk=nint(dtdec/dtstep)
  do k=1,85
     j=j0 + NSTEP*(k-1) + 1 + lagpk
     if(j.ge.1 .and. j.le.jz0) then
        do i=1,iz0
           ii=i+mode_q65*itone(k)
           if(ii.ge.1 .and. ii.le.iz0) spec(i)=spec(i) + s1(ii,j)
        enddo
     endif
  enddo

  i0=nint(f0dec/df)
  nsum=max(10*mode_q65,nint(50.0/df))
  ia=max(1,i0-2*nsum)
  ib=min(iz0,i0+2*nsum)
  sum1=sum(spec(ia:ia+nsum-1))
  sum2=sum(spec(ib-nsum+1:ib))
  avg=(sum1+sum2)/(2.0*nsum)
  spec=spec/avg                          !Baseline level is now 1.0
  smax=maxval(spec(ia:ib))
  sig_area=sum(spec(ia+nsum:ib-nsum)-1.0)
  w_equiv=sig_area/(smax-1.0)
  snr2=db(max(1.0,sig_area)) - db(2500.0/df)

  return
end subroutine q65_snr

subroutine q65_hist(if0,msg0,dxcall,dxgrid)

! Save the MAXHIST most receent decodes, and their f0 values; or, if
! dxcall is present, look up the most recent dxcall and dxgrid at the
! specified f0.

  parameter (MAXHIST=100)
  integer,intent(in) :: if0                         !Audio freq of decode
  character(len=37),intent(in),optional :: msg0     !Decoded message
  character(len=12),intent(inout),optional :: dxcall  !Second callsign in message
  character(len=6),intent(inout),optional :: dxgrid   !Third word in msg, if grid

  character*6 g1
  character(len=37), pointer :: msg(:)
  logical isgrid                                 !Statement function


  isgrid(g1)=g1(1:1).ge.'A' .and. g1(1:1).le.'R' .and. g1(2:2).ge.'A' .and. &
       g1(2:2).le.'R' .and. g1(3:3).ge.'0' .and. g1(3:3).le.'9' .and.       &
       g1(4:4).ge.'0' .and. g1(4:4).le.'9' .and. g1(1:4).ne.'RR73'

  msg=>q65_work%history
  if(present(dxcall)) go to 100                  !This is a lookup request

  if(nhist.eq.MAXHIST) then
     nf0(1:MAXHIST-1)=nf0(2:MAXHIST)             !List is full, must make room
     msg(1:MAXHIST-1)=msg(2:MAXHIST)
     nhist=MAXHIST-1
  endif
  nhist=nhist+1                                  !Insert msg0 at end of list
  nf0(nhist)=if0
  msg(nhist)=msg0
  go to 900

100 if(dxcall(1:3).ne.'   ') go to 900
  dxcall='            '                        !This is a lookup request
  dxgrid='      '
! Look for a decode close to if0, starting with most recent ones
  do i=nhist,1,-1                     
     if(abs(nf0(i)-if0).gt.10) cycle
     i1=index(msg(i),' ')
     if(i1.ge.4 .and. i1.le.13) then
        i2=index(msg(i)(i1+1:),' ') + i1
        dxcall=msg(i)(i1+1:i2-1)                 !Extract dxcall
        g1=msg(i)(i2+1:i2+4)
        if(isgrid(g1)) dxgrid=g1(1:4)            !Extract dxgrid
        exit
     endif
  enddo

900 return
end subroutine q65_hist

subroutine q65_hist2(nfreq,msg0,callers,nhist2,persist)

  use types, only: q3list
  use prog_args, only: data_dir
  use q65_callers, only: Q65_MAX_CALLERS,q65_record_caller,q65_save_callers
  implicit none
  integer, intent(in) :: nfreq
  character(len=*), intent(in) :: msg0
  type(q3list), intent(inout) :: callers(Q65_MAX_CALLERS)
  integer, intent(inout) :: nhist2
  logical, intent(in) :: persist
  integer :: status,time

  call q65_record_caller(callers,nhist2,nfreq,msg0,time())
  if(persist) call q65_save_callers(trim(data_dir)//'/tsil.3q',callers,nhist2,status)
end subroutine q65_hist2

end module q65
