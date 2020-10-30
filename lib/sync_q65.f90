subroutine sync_q65(iwave,nmax,mode65,nsps,nfqso,ntol,xdt,f0,snr1)

! Detect and align with the Q65 sync vector, returning time and frequency
! offsets and SNR estimate.

! Input:  iwave(0:nmax-1)        Raw data
!         mode65                 Tone spacing 1 2 4 8 16 (A-E)
!         nsps                   Samples per symbol at 12000 Sa/s
!         nfqso                  Target frequency (Hz)
!         ntol                   Search range around nfqso (Hz)
! Output: xdt                    Time offset from nominal (s)
!         f0                     Frequency of sync tone
!         snr1                   Relative SNR of sync signal
  
  parameter (NSTEP=8)                    !Step size nsps/NSTEP
  integer*2 iwave(0:nmax-1)              !Raw data
  integer isync(22)                      !Indices of sync symbols
  integer ijpk(2)                        !Indices i and j at peak of ccf
  real, allocatable :: s1(:,:)           !Symbol spectra, quarter-symbol steps
  real sync(85)                          !sync vector 
  real ccf(-64:64,-53:214)               !CCF(freq,time)
  complex, allocatable :: c0(:)          !Complex spectrum of symbol
  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/
  data sync(1)/99.0/
  save sync

  nfft=2*nsps
  df=12000.0/nfft                        !Freq resolution = 0.5*baud
  istep=nsps/NSTEP
  iz=5000.0/df                           !Uppermost frequency bin, at 5000 Hz
  txt=85.0*nsps/12000.0
  jz=(txt+1.0)*12000.0/istep             !Number of quarter-symbol steps
  if(nsps.ge.6912) jz=(txt+2.0)*12000.0/istep   !For TR 60 s and higher

  allocate(s1(iz,jz))
  allocate(c0(0:nfft-1))

  if(sync(1).eq.99.0) then               !Generate the sync vector
     sync=-22.0/63.0                     !Sync tone OFF  
     do k=1,22
        sync(isync(k))=1.0               !Sync tone ON
     enddo
  endif

  fac=1/32767.0
  do j=1,jz                              !Compute symbol spectra at step size
     ia=(j-1)*istep
     ib=ia+nsps-1
     k=-1
     do i=ia,ib,2          !Load iwave data into complex array c0, for r2c FFT
        xx=iwave(i)
        yy=iwave(i+1)
        k=k+1
        c0(k)=fac*cmplx(xx,yy)
     enddo
     c0(k+1:)=0.
     call four2a(c0,nfft,1,-1,0)              !r2c FFT
     do i=1,iz
        s1(i,j)=real(c0(i))**2 + aimag(c0(i))**2
     enddo
! For large Doppler spreads, should we smooth the spectra here?
     call smo121(s1(1:iz,j),iz)
  enddo

  i0=nint(nfqso/df)                           !Target QSO frequency
  call pctile(s1(i0-64:i0+192,1:jz),129*jz,40,base)
  s1=s1/base - 1.0

! Apply fast AGC
  s1max=20.0                                  !Empirical choice
  do j=1,jz
     smax=maxval(s1(i0-64:i0+192,j))
     if(smax.gt.s1max) s1(i0-64:i0+192,j)=s1(i0-64:i0+192,j)*s1max/smax
  enddo

  dtstep=nsps/(NSTEP*12000.0)                 !Step size in seconds
  ia=min(64,nint(ntol/df))
  lag1=-1.0/dtstep
  lag2=1.0/dtstep + 0.9999
  j0=0.5/dtstep
  if(nsps.ge.6192) then
     j0=1.0/dtstep              !Nominal index for start of signal
     lag2=4.0/dtstep + 0.9999   !Include EME delays
  endif
  ccf=0.

  do lag=lag1,lag2
     do k=1,85
        n=NSTEP*(k-1) + 1
        j=n+lag+j0
        if(j.ge.1 .and. j.le.jz) then
           ccf(-ia:ia,lag)=ccf(-ia:ia,lag) + sync(k)*s1(i0-ia:i0+ia,j)
        endif
     enddo
  enddo

  ijpk=maxloc(ccf)
  ipk=ijpk(1)-65
  jpk=ijpk(2)-54
  f0=nfqso + ipk*df
  xdt=jpk*dtstep
  
  sq=0.
  nsq=0
  do j=lag1,lag2
     if(abs(j-jpk).gt.6) then
        sq=sq + ccf(ipk,j)**2
        nsq=nsq+1
     endif
  enddo
  rms=sqrt(sq/nsq)
  smax=ccf(ipk,jpk)
  snr1=smax/rms

!  do j=lag1,lag2
!     write(55,3055) j,j*dtstep,ccf(ipk,j)/rms
!3055 format(i5,f8.3,f10.3)
!  enddo

!  do i=-ia,ia
!     write(56,3056) i*df,ccf(i,0)/rms
!3056 format(2f10.3)
!  enddo

  return
end subroutine sync_q65
