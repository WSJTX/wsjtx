subroutine sync_q65(iwave,nmax,mode65,nQSOprogress,nsps,nfqso,ntol,    &
     xdt,f0,snr1,width)

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
  parameter (LN=2176*63)           !LN=LL*NN; LL=64*(mode_q65+2), NN=63
  character*37 msg,msgsent
  integer*2 iwave(0:nmax-1)              !Raw data
  integer isync(22)                      !Indices of sync symbols
  integer itone(85)
  real, allocatable :: s1(:,:)           !Symbol spectra, 1/8-symbol steps
  real, allocatable :: ccf(:,:)          !CCF(freq,lag)
  real, allocatable :: ccf1(:)           !CCF(freq) at best lag
  real sync(85)                          !sync vector
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
  ia=ntol/df

  allocate(s1(iz,jz))
  allocate(c0(0:nfft-1))
  allocate(ccf(-ia:ia,-53:214))
  allocate(ccf1(-ia:ia))

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
  ia=ntol/df
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

  ic=ntol/df
  ccfmax=0.
  ipk=0
  jpk=0
  do i=-ic,ic
     do j=lag1,lag2
        if(ccf(i,j).gt.ccfmax) then
           ipk=i
           jpk=j
           ccfmax=ccf(i,j)
        endif
     enddo
  enddo
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
!     write(56,3056) i*df,ccf(i,jpk)/rms
!3056 format(2f10.3)
!  enddo
!  flush(56)

  ccf1=ccf(-ia:ia,jpk)
  acf0=dot_product(ccf1,ccf1)
  do i=1,ia
     acf=dot_product(ccf1,cshift(ccf1,i))
     if(acf.le.0.5*acf0) exit
  enddo
  width=i*1.414*df

!### Experimental:
  if(nQSOprogress.lt.1) go to 900
! "Deep Likelihood" decode attempt
  snr1a_best=0.
  do imsg=1,4
     ccf=0.
     msg='K1ABC W9XYZ RRR'
     if(imsg.eq.2) msg='K1ABC W9XYZ RR73'
     if(imsg.eq.3) msg='K1ABC W9XYZ 73'
     if(imsg.eq.4) msg='CQ K9AN EN50'
     call genq65(msg,0,msgsent,itone,i3,n3)

     do lag=lag1,lag2
        do k=1,85
           j=j0 + NSTEP*(k-1) + 1 + lag
           if(j.ge.1 .and. j.le.jz) then
              do i=-ia,ia
                 ii=i0+2*itone(k)+i
                 ccf(i,lag)=ccf(i,lag) + s1(ii,j)
              enddo
           endif
        enddo
     enddo

     ic=ntol/df
     ccfmax=0.
     ipk=0
     jpk=0
     do i=-ic,ic
        do j=lag1,lag2
           if(ccf(i,j).gt.ccfmax) then
              ipk=i
              jpk=j
              ccfmax=ccf(i,j)
           endif
        enddo
     enddo
     f0a=nfqso + ipk*df
     xdta=jpk*dtstep
     
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
     snr1a=smax/rms
     if(snr1a.gt.snr1a_best) then
        snr1a_best=snr1a
        imsg_best=imsg
        xdta_best=xdta
        f0a_best=f0a
     endif
!     write(57,3001) imsg,xdt,xdta,f0,f0a,snr1,snr1a
!3001 format(i1,6f8.2)
  
!     do j=lag1,lag2
!        write(55,3055) j,j*dtstep,ccf(ipk,j)/rms
!3055    format(i5,f8.3,f10.3)
!     enddo

!     do i=-ia,ia
!        write(56,3056) i*df,ccf(i,jpk)/rms
!3056    format(2f10.3)
!     enddo
  enddo
  if(snr1a_best.gt.2.0) then
     xdt=xdta_best
     f0=f0a_best
     snr1=1.4*snr1a_best
  endif
  
!  write(58,3006) xdta_best,f0a_best,snr1a_best,imsg_best
!3006 format(3f8.2,i3)

900 return
end subroutine sync_q65
