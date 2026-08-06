subroutine subtract_jtty(c0, n, nvalid, tone_symbols, nsym_total, nss, f1, xdt)

! Subtract a decoded JTTY signal from the complex analytic buffer c0 (at
! 6000 Sa/s -- the domain jtty_mdecode.f90 already works in, produced from
! the raw audio by ana64a), so a second, weaker signal at a different
! time/frequency can be recovered in a follow-up decode pass over the
! residual. Modeled on lib/ft8/subtractft8.f90: build a unit-amplitude
! reference waveform for the already re-encoded (error-corrected) tone
! sequence, estimate its actual complex (amplitude+phase) channel gain via
! a matched-filter/low-pass correlation against c0, then subtract.
!
! Measured signal  : c0(t)   = a(t) * cref(t) + noise
! Reference signal : cref(t) = exp( j*(2*pi*f1*t+phi(t)) ), unit amplitude
! Complex gain     : cfilt(t) = LPF[ c0(t) * CONJG(cref(t)) ]
! Subtract         : c0(t)   = c0(t) - cref(t) * cfilt(t)
!
! Unlike subtractft8 (a real-valued passband buffer, hence its factor-of-2
! real-part subtraction to reconstruct a real signal from an analytic
! estimate), c0 here is already complex analytic, so the full complex
! estimate is subtracted directly with no real-part/factor-of-2 step.
!
! Input:  complex   c0(0:n-1)          In/out: buffer to subtract from
!         integer   n                  Size of c0 (must be a power of 2)
!         integer   nvalid             Number of valid samples in c0 (<=n)
!         integer   tone_symbols(nsym_total)  Full frame: sync + info tones
!         integer   nsym_total         Total symbols/frame (sync+info)
!         integer   nss                Samples per symbol at 6000 Sa/s
!         real      f1                 Signal's estimated audio frequency, Hz
!         real      xdt                Signal's estimated DT (start time), s

  implicit none
  integer, intent(in)    :: n, nvalid, nsym_total, nss
  complex, intent(inout) :: c0(0:n-1)
  integer, intent(in)    :: tone_symbols(nsym_total)
  real, intent(in)       :: f1, xdt

  real, parameter            :: FSAMPLE6 = 6000.0
  integer, save              :: nframe_save=-1, nfft_save=-1
  complex, allocatable, save :: cref(:), camp(:), cfilt(:), cw(:)
  real, allocatable, save    :: window(:)
  integer :: nframe, nfft, nfilt, i, j, nstart
  real    :: pi, fac, sumw, xjunk(1)
  complex :: z

  nframe=nsym_total*nss
  nfft=n
  ! ~2 symbol periods, matching subtractft8's NFILT (4000 samples @ 12000
  ! Sa/s = 333 ms) being ~2 FT8 symbol periods (160 ms each) -- smooths the
  ! channel-gain estimate enough to track slow fading without blurring out
  ! genuine amplitude structure within the frame.
  nfilt=2*nss

  if(nframe.ne.nframe_save .or. nfft.ne.nfft_save) then
     if(allocated(cref)) deallocate(cref,camp,cfilt,cw,window)
     allocate(cref(nframe), camp(nfft), cfilt(nfft), cw(nfft))
     allocate(window(-nfilt/2:nfilt/2))
     pi=4.0*atan(1.0)
     fac=1.0/float(nfft)
     sumw=0.0
     do j=-nfilt/2,nfilt/2
        window(j)=cos(pi*j/nfilt)**2
        sumw=sumw+window(j)
     enddo
     cw=0.
     cw(1:nfilt+1)=window/sumw
     cw=cshift(cw,nfilt/2+1)
     call four2a(cw,nfft,1,-1,1)
     cw=cw*fac
     nframe_save=nframe
     nfft_save=nfft
  endif

! Generate a unit-amplitude complex reference waveform for the full frame
! (sync + info tones), at this signal's own estimated frequency.
  call gen_jttywave(tone_symbols, nsym_total, nss, 2.0, FSAMPLE6, f1,        &
       cref, xjunk, 1, nframe)

  nstart=nint(xdt*FSAMPLE6)      ! 0-based c0 index of the frame's first sample
  camp=(0.,0.)
  do i=1,nframe
     j=nstart+i-1
     if(j.ge.0 .and. j.lt.nvalid) camp(i)=c0(j)*conjg(cref(i))
  enddo

  cfilt=camp
  call four2a(cfilt,nfft,1,-1,1)     ! forward FFT
  cfilt=cfilt*cw                     ! apply the low-pass filter
  call four2a(cfilt,nfft,1,1,1)      ! inverse FFT -> smoothed complex gain

  do i=1,nframe
     j=nstart+i-1
     if(j.ge.0 .and. j.lt.nvalid) then
        z=cfilt(i)*cref(i)
        c0(j)=c0(j)-z            !Subtract the reconstructed signal
     endif
  enddo

  return
end subroutine subtract_jtty
