subroutine q65_sync(nutc,iwave,ntrperiod,nfqso,ntol,ndepth,lclearave,  &
     emedelay,xdt,f0,snr1,width,dat4,snr2,idec)

! Detect and align with the Q65 sync vector, returning time and frequency
! offsets and SNR estimate.

! Input:  iwave(0:nmax-1)        Raw data
!         mode_q65               Tone spacing 1 2 4 8 16 (A-E)
!         nsps                   Samples per symbol at 12000 Sa/s
!         nfqso                  Target frequency (Hz)
!         ntol                   Search range around nfqso (Hz)
! Output: xdt                    Time offset from nominal (s)
!         f0                     Frequency of sync tone
!         snr1                   Relative SNR of sync signal

  use packjt77
  use timer_module, only: timer
  use q65

  parameter (LN=2176*63)           !LN=LL*NN; LL=64*(mode_q65+2), NN=63
  integer*2 iwave(0:12000*ntrperiod-1)   !Raw data
  integer dat4(13)
  integer ijpk(2)
  character*37 decoded
  logical first,lclearave
  real, allocatable :: s1(:,:)           !Symbol spectra, 1/8-symbol steps
  real, allocatable :: s3(:,:)           !Data-symbol energies s3(LL,63)
  real, allocatable :: ccf(:,:)          !CCF(freq,lag)
  real, allocatable :: ccf1(:)           !CCF(freq) at best lag
  real, allocatable :: ccf2(:)           !CCF(freq) at any lag
  data first/.true./
  save first

  if(nutc+ndepth.eq.-999) stop
  irc=-2
  idec=-1
  snr1=0.
  dat4=0
  LL=64*(2+mode_q65)
  nfft=nsps
  df=12000.0/nfft                        !Freq resolution = baud
  istep=nsps/NSTEP
  iz=5000.0/df                           !Uppermost frequency bin, at 5000 Hz
  txt=85.0*nsps/12000.0
  jz=(txt+1.0)*12000.0/istep             !Number of quarter-symbol steps
  if(nsps.ge.6912) jz=(txt+2.0)*12000.0/istep   !For TR 60 s and higher
  ia=ntol/df
  ia2=max(ia,10*mode_q65,nint(100.0/df))
  nsmo=int(0.7*mode_q65*mode_q65)
  if(nsmo.lt.1) nsmo=1
!  nsmo=1  !### TEMPORARY ###

  allocate(s1(iz,jz))
  allocate(s3(-64:LL-65,63))
  allocate(ccf(-ia2:ia2,-53:214))
  allocate(ccf1(-ia2:ia2))
  allocate(ccf2(-ia2:ia2))
  if(LL.ne.LL0 .or. lclearave) then
     if(allocated(s1a)) deallocate(s1a)
     allocate(s1a(iz,jz))
     s1a=0.
     navg=0
     LL0=LL
  endif

  s3=0.
  if(first) then                         !Generate the sync vector
     sync=-22.0/63.0                     !Sync tone OFF  
     do k=1,22
        sync(isync(k))=1.0               !Sync tone ON
     enddo
  endif

  call timer('s1      ',0)
! Compute spectra with symbol length and NSTEP time bins per symbol.
  call q65_symspec(iwave,ntrperiod*12000,iz,jz,s1)
  call timer('s1      ',1)

  i0=nint(nfqso/df)                           !Target QSO frequency
  if(i0-64.lt.1 .or. i0-65+LL.gt.iz) go to 900  !Frequency out of range
  call pctile(s1(i0-64:i0-65+LL,1:jz),LL*jz,40,base)
  s1=s1/base

! Apply fast AGC
  s1max=20.0                                  !Empirical choice
  do j=1,jz                                   !### Maybe wrong way? ###
     smax=maxval(s1(i0-64:i0-65+LL,j))
     if(smax.gt.s1max) s1(i0-64:i0-65+LL,j)=s1(i0-64:i0-65+LL,j)*s1max/smax
  enddo

  dtstep=nsps/(NSTEP*12000.0)                 !Step size in seconds
  lag1=-1.0/dtstep
  lag2=1.0/dtstep + 0.9999
  if(nsps.ge.3600 .and. emedelay.gt.0) lag2=4.0/dtstep + 0.9999  !Include EME
  j0=0.5/dtstep
  if(nsps.ge.7200) j0=1.0/dtstep              !Nominal start-signal index

  idec=-1
  dat4=0

  if(ncw.gt.0) then
! Try list decoding via "Deep Likelihood".
     call timer('list_dec',0)
     call q65_dec_q3(df,s1,iz,jz,ia,lag1,lag2,i0,j0,ccf,ccf1,ccf2, &
          ia2,s3,LL,snr2,dat4,idec,decoded)
     call timer('list_dec',1)
  endif

!######################################################################
! Get 2d CCF and ccf2 using sync symbols only
  ccf=0.
  call timer('2dccf   ',0)
  do lag=lag1,lag2
     do k=1,85
        n=NSTEP*(k-1) + 1
        j=n+lag+j0
        if(j.ge.1 .and. j.le.jz) then
           do i=-ia2,ia2
              if(i0+i.lt.1 .or. i0+i.gt.iz) cycle
              ccf(i,lag)=ccf(i,lag) + sync(k)*s1(i0+i,j)
           enddo
        endif
     enddo
  enddo
  do i=-ia2,ia2
     ccf2(i)=maxval(ccf(i,:))
  enddo

! Estimate rms on ccf baseline
  ijpk=maxloc(ccf(-ia:ia,:))
  ipk=ijpk(1)-ia-1
  jpk=ijpk(2)-53-1
  sq=0.
  nsq=0
  jd=(lag2-lag1)/4
  do i=-ia2,ia2
     do j=lag1,lag2
        if(abs(j-jpk).gt.jd .and. abs(i-ipk).gt.ia/2) then
           sq=sq + ccf(i,j)**2
           nsq=nsq+1
        endif
     enddo
  enddo
  rms=sqrt(sq/nsq)
  smax=ccf(ipk,jpk)
  snr1=smax/rms
  ccf2=ccf2/rms
  if(snr1.gt.10.0) ccf2=(10.0/snr1)*ccf2
  call timer('2dccf   ',1)

  if(idec.le.0) then
! The q3 decode attempt failed, so we'll try a more general decode.
     f0=nfqso + ipk*df
     xdt=jpk*dtstep
     ccf1=ccf(:,jpk)/rms
     if(snr1.gt.10.0) ccf1=(10.0/snr1)*ccf1
     call q65_s1_to_s3(s1,iz,jz,i0,j0,ipk,jpk,LL,mode_q65,sync,s3)
  endif

  smax=maxval(ccf1)
  i1=-9999
  i2=-9999
  do i=-ia,ia
     if(i1.eq.-9999 .and. ccf1(i).ge.0.5*smax) i1=i
     if(i2.eq.-9999 .and. ccf1(-i).ge.0.5*smax) i2=-i
  enddo
  width=df*(i2-i1)

! Write data for the red and orange sync curves.
  do i=-ia2,ia2
     freq=nfqso + i*df
     write(17,1100) freq,ccf1(i),xdt,ccf2(i)
1100 format(4f10.3)
  enddo
  close(17)

900 return
end subroutine q65_sync
