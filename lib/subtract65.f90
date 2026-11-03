subroutine subtract65(dd,npts,f0,dt)
  use fftw_tsan, only: wsjt_tsan_acquire_fftw, wsjt_tsan_release_fftw

! Subtract a jt65 signal
!
! Measured signal  : dd(t)    = a(t)cos(2*pi*f0*t+theta(t))
! Reference signal : cref(t)  = exp( j*(2*pi*f0*t+phi(t)) )
! Complex amp      : cfilt(t) = LPF[ dd(t)*CONJG(cref(t)) ]
! Subtract         : dd(t)    = dd(t) - 2*REAL{cref*cfilt}

  use packjt
  use jt65_mod, only: jt65_work,ensure_jt65_workspace
  use FFTW3
  use, intrinsic :: iso_c_binding
  use timer_module, only: timer

  integer correct(63)
  parameter (NMAX=60*12000) !Samples per 60 s
  parameter (NFILT=1600)
  real*4  dd(NMAX), window(-NFILT/2:NFILT/2)
  integer nprc(126)
  real*8 dphi,phi
  data nprc/                                   &
    1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0, &
    0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1, &
    0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1, &
    0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1, &
    1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1, &
    0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1, &
    1,1,1,1,1,1/
  common/chansyms65/correct
  common/patience/npatience,nthreads

  call ensure_jt65_workspace()
  if(.not.allocated(jt65_work%cref)) then
    allocate(jt65_work%cref(NMAX),jt65_work%camp(NMAX),jt65_work%cfilt(NMAX),jt65_work%cw(NMAX))
  endif
  associate(cref=>jt65_work%cref,camp=>jt65_work%camp,cfilt=>jt65_work%cfilt,cw=>jt65_work%cw)

  pi=4.0*atan(1.0)

! Symbol duration is 4096/11025 s.
! Sample rate is 12000/s, so 12000*(4096/11025)=4458.23 samples/symbol.
! For now, call it 4458 samples/symbol. Over the message duration, we'll be off
! by about (4458.23-4458)*126=28.98 samples; 29 samples, or 0.7% of 1 symbol.
! Could eliminate accumulated error by injecting one extra sample every
! 5 or so symbols... Maybe try this later.

  nstart=dt*12000+1;
  nsym=126
  ns=4458 
  nref=nsym*ns
  camp(1:nref)=0.
  nend=nstart+nref-1
  phi=0.0
  iref=1
  ind=1
  isym=1
  call timer('subtr_1 ',0)
  do k=1,nsym
    if( nprc(k) .eq. 1 ) then
        omega=2*pi*f0
    else
        omega=2*pi*(f0+2.6917*(correct(isym)+2))
        isym=isym+1
    endif
    dphi=omega/12000.0
    do i=1,ns
        cref(ind)=cexp(cmplx(0.0,phi))
        phi=modulo(phi+dphi,2*pi)
        id=nstart-1+ind
        if(id.ge.1 .and. id.le.npts) camp(ind)=dd(id)*conjg(cref(ind))
        ind=ind+1
     enddo
  enddo
  call timer('subtr_1 ',1)

  call timer('subtr_2 ',0)
! Smoothing filter: do the convolution by means of FFTs. Ignore end-around 
! cyclic effects for now.

  nfft=564480

  if(.not.c_associated(jt65_work%subtract_forward).or. &
       jt65_work%sub_patience/=npatience.or.jt65_work%sub_threads/=nthreads) then
    nflags=FFTW_ESTIMATE
    if(npatience.eq.1) nflags=FFTW_ESTIMATE_PATIENT
    if(npatience.eq.2) nflags=FFTW_MEASURE
    if(npatience.eq.3) nflags=FFTW_PATIENT
    if(npatience.eq.4) nflags=FFTW_EXHAUSTIVE
    !$omp critical(fftw)
    call wsjt_tsan_acquire_fftw()
    if(c_associated(jt65_work%subtract_forward)) call fftwf_destroy_plan(jt65_work%subtract_forward)
    if(c_associated(jt65_work%subtract_inverse)) call fftwf_destroy_plan(jt65_work%subtract_inverse)
    if(c_associated(jt65_work%window_plan)) call fftwf_destroy_plan(jt65_work%window_plan)
    call fftwf_plan_with_nthreads(nthreads)
    ! in==out requests an intentional in-place FFTW transform.
    jt65_work%subtract_forward=fftwf_plan_dft_1d(nfft,cfilt,cfilt,-1,nflags)
    jt65_work%subtract_inverse=fftwf_plan_dft_1d(nfft,cfilt,cfilt,1,nflags)
    jt65_work%window_plan=fftwf_plan_dft_1d(nfft,cw,cw,-1,nflags)
    call fftwf_plan_with_nthreads(1)
    call wsjt_tsan_release_fftw()
    !$omp end critical(fftw)
    jt65_work%sub_patience=npatience
    jt65_work%sub_threads=nthreads
! Create and normalize the filter
     sum=0.0
     do j=-NFILT/2,NFILT/2
        window(j)=cos(pi*j/NFILT)**2
        sum=sum+window(j)
     enddo
     cw=0.
     do i=-NFILT/2,NFILT/2
        j=i+1
        if(j.lt.1) j=j+nfft
        cw(j)=window(i)/sum
     enddo
     call fftwf_execute_dft(jt65_work%window_plan,cw,cw)
  endif

  nz=561708
  cfilt(1:nz)=camp(1:nz)
  cfilt(nz+1:nfft)=0.
  call fftwf_execute_dft(jt65_work%subtract_forward,cfilt,cfilt)
  fac=1.0/float(nfft)
  cfilt(1:nfft)=fac*cfilt(1:nfft)*cw(1:nfft)
  call fftwf_execute_dft(jt65_work%subtract_inverse,cfilt,cfilt)
  call timer('subtr_2 ',1)

! Subtract the reconstructed signal
  call timer('subtr_3 ',0)
  do i=1,nref
     j=nstart+i-1
     if(j.ge.1 .and. j.le.npts) dd(j)=dd(j)-2*REAL(cfilt(i)*cref(i))
  enddo
  call timer('subtr_3 ',1)

  end associate
  return
end subroutine subtract65 
