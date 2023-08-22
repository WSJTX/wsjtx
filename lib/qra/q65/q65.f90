module q65

  parameter (NSTEP=8)          !Number of time bins per symbol in s1, s1a, s1b
  parameter (PLOG_MIN=-242.0)        !List decoding threshold
  integer iz0,jz0
!  integer listutc(10)
  integer apsym0(58),aph10(10)
  integer apmask1(78),apsymbols1(78)
  integer apmask(13),apsymbols(13)
  integer,dimension(22) ::  isync = (/1,9,12,13,15,22,23,26,27,33,35,   &
                                     38,46,50,55,60,62,66,69,74,76,85/)
  integer codewords(63,206)
  integer ibwa,ibwb,ncw,nsps,mode_q65,nfa,nfb,nqd
  integer idfbest,idtbest,ibw,ndistbest,maxiters,max_drift
  integer istep,nsmo,lag1,lag2,npasses,iseq,ncand,nrc
  integer i0,j0
  integer navg(0:1)
  logical lnewdat
  real candidates(20,3)                  !snr, xdt, and f0 of top candidates
  real, allocatable :: s1raw(:,:)        !Symbol spectra, 1/8-symbol steps
  real, allocatable :: s1(:,:)           !Symbol spectra w/suppressed peaks
  real, allocatable :: s1w(:,:)       !Symbol spectra w/suppressed peaks (W3SZ)
  real, allocatable,save :: s1a(:,:,:)   !Cumulative symbol spectra
  real, allocatable,save :: ccf2(:)      !Max CCF(freq) at any lag (orange curve)
  real, allocatable,save :: ccf2_avg(:)  !Like ccf2, but for avg (red curve)
  real sync(85)                          !sync vector
  real df,dtstep,dtdec,f0dec,ftol,plog,drift

contains

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
  logical first,lclearave
  real, allocatable :: s3(:,:)           !Data-symbol energies s3(LL,63)
  real, allocatable :: ccf1(:)           !CCF(freq) at fixed lag (red)
  data first/.true./
  save first,LL0

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
  txt=85.0*nsps/12000.0
  jz=(txt+1.0)*12000.0/istep             !Number of symbol/NSTEP bins
  if(nsps.ge.6912) jz=(txt+2.0)*12000.0/istep   !For TR 60 s and higher
  ftol=ntol
  ia=ntol/df
  ia2=max(ia,10*mode_q65,nint(100.0/df))
!  nsmo=int(0.7*mode_q65*mode_q65)
  nsmo=int(0.5*mode_q65*mode_q65)
  if(nsmo.lt.1) nsmo=1
  if(first) then                         !Generate the sync vector
     sync=-22.0/63.0                     !Sync tone OFF  
     do k=1,22
        sync(isync(k))=1.0               !Sync tone ON
     enddo
  endif

  allocate(s3(-64:LL-65,63))
  allocate(ccf1(-ia2:ia2))
  if(LL.ne.LL0 .or. iz.ne.iz0 .or. jz.ne.jz0 .or. lclearave) then
     if(allocated(s1raw)) deallocate(s1raw)
     allocate(s1raw(iz,jz))
     if(allocated(s1)) deallocate(s1)
     allocate(s1(iz,jz))
     if(allocated(s1a)) deallocate(s1a)
     allocate(s1a(iz,jz,0:1))
     if(allocated(ccf2)) deallocate(ccf2)
     allocate(ccf2(iz))
     if(allocated(ccf2_avg)) deallocate(ccf2_avg)
     allocate(ccf2_avg(iz))
     s1=0.
     s1a=0.
     navg=0
     LL0=LL
     iz0=iz
     jz0=jz
     lclearave=.false.
  endif
  ccf1=0.
  if(iavg.eq.0) ccf2=0.
  dtstep=nsps/(NSTEP*12000.0)                 !Step size in seconds
  lag1=-1.0/dtstep
  lag2=1.0/dtstep + 0.9999
  if(nsps.ge.3600 .and. emedelay.gt.0) lag2=5.5/dtstep + 0.9999  !Include EME
  j0=0.5/dtstep
  if(nsps.ge.7200) j0=1.0/dtstep              !Nominal start-signal index

  s3=0.
!  if(iavg.eq.0 .and. lnewdat) then
  if(iavg.eq.0) then
     call timer('q65_syms',0)
! Compute symbol spectra with NSTEP time bins per symbol
     call q65_symspec(iwave,ntrperiod*12000,iz,jz,s1)
     call timer('q65_syms',1)
!     lnewdat=.false.
  else
     s1=s1a(:,:,iseq)
  endif

  i0=nint(nfqso/df)                             !Target QSO frequency
  ii1=max(1,i0-64)
  ii2=i0-65+LL
  call pctile(s1(ii1:ii2,1:jz),ii2-ii1+1*jz,45,base)
!  s1=s1/base
  s1raw=s1

! Apply fast AGC to the symbol spectra
!  s1max=20.0                                  !Empirical choice
!  do j=1,jz                                   !### Maybe wrong way? ###
!     smax=maxval(s1(ii1:ii2,j))
!     if(smax.gt.s1max) s1(ii1:ii2,j)=s1(ii1:ii2,j)*s1max/smax
!  enddo

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
     call q65_ccf_22(s1,iz,jz,nfqso,ntol,iavg,ipk,jpk,  &
          f0a,xdta,ccf2)
     call timer('ccf_22a ',1)
  endif

! Get 2d CCF and ccf2_avg using sync symbols only
  if(iavg.ge.1) then
     call timer('ccf_22b ',0)
     call q65_ccf_22(s1,iz,jz,nfqso,ntol,iavg,ipk,jpk,  &
          f0a,xdta,ccf2_avg)
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
  call q65_write_red(iz,xdt,ccf2_avg,ccf2)   !### Need this call for WSJT-X

  if(idec.lt.0 .and. (iavg.eq.0 .or. iavg.eq.2)) then
     call q65_dec_q012(s3,LL,snr2,dat4,idec,decoded)
  endif

  if(idec.lt.0 .and. max_drift.eq.50 .and. stageno.eq.5) then

     if(allocated(s1w)) deallocate(s1w) ! w3sz
     allocate(s1w(iz,jz))               ! w3sz	 
	 
     s1w=s1
     do w3t=1,jz
        do w3f=1,iz
           mm=w3f + nint(drift*w3t/(jz*df))
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

  if(allocated(s1a)) s1a(:,:,iseq)=0.
  navg(iseq)=0
  
  return
end subroutine q65_clravg

subroutine q65_symspec(iwave,nmax,iz,jz,s1)

! Compute symbol spectra with NSTEP time-steps per symbol.
  
  integer*2 iwave(0:nmax-1)              !Raw data
  real s1(iz,jz)
  complex, allocatable :: c0(:)          !Complex spectrum of symbol

  allocate(c0(0:nsps-1))
  nfft=nsps
  fac=1/32767.0
  do j=1,jz,2                     !Compute symbol spectra at 2*step size
     i1=(j-1)*istep
     i2=i1+nsps-1
     k=-1
     do i=i1,i2,2          !Load iwave data into complex array c0, for r2c FFT
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
        call q65_ap(nQSOprogress,ipass,ncontest,lapcqonly,iaptype,   &
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
  real, allocatable :: ccf(:,:)          !CCF(freq,lag)
  real, allocatable :: best(:)           !best(imsg) -- for checking 2nd best
  real ccf1(-ia2:ia2)
  integer ijpk(2)
  integer itone(85)

  allocate(ccf(-ia2:ia2,-53:214))
  allocate(best(ncw))
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

  deallocate(ccf)
  better=0.
  if(imsg_best.gt.0) then
     best(imsg_best)=0.
     better=ccf_best/maxval(best)
  endif

  return
end subroutine q65_ccf_85

subroutine q65_ccf_22(s1,iz,jz,nfqso,ntol,iavg,ipk,jpk,  &
     f0,xdt,ccf2)

! Attempt synchronization using only the 22 sync symbols.  Return ccf2
! for the "orange sync curve".

  real s1(iz,jz)
  real ccf2(iz)                               !Orange sync curve
  real tmp(20,3)
  real, allocatable :: xdt2(:)
  real, allocatable :: s1avg(:)
  integer, allocatable :: indx(:)

  allocate(xdt2(iz))
  allocate(s1avg(iz))
  allocate(indx(iz))

  ia=max(nfa,100)/df
  ib=min(nfb,4900)/df
!  if(nqd.ne.1 .or. iavg.ne.0) max_drift=0    !### Disabled March 22, 2023
  if(max_drift.ne.0) then
     ia=max(nint(100/df),nint((nfqso-ntol)/df))
     ib=min(nint(4900/df),nint((nfqso+ntol)/df))
  endif

  do i=ia,ib
     s1avg(i)=sum(s1(i,1:jz))
  enddo

  call pctile(s1avg(ia:ib),ib-ia+1,40,base0)
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
        snrbest=snr
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
  real s3(1,1)       !Silence compiler warning that wants to see a 2D array
  real s3prob(0:63,63)                   !Symbol-value probabilities
  integer dat4(13)
  character c77*77,decoded*37
  logical unpk77_success

  nFadingModel=1
  decoded='                                     '
  call q65_intrinsics_ff(s3,nsubmode,b90ts,nFadingModel,s3prob)
  call q65_dec_fullaplist(s3,s3prob,codewords,ncw,esnodb,dat4,plog,irc)
  if(sum(dat4).le.0) irc=-2
  if(irc.ge.0 .and. plog.gt.PLOG_MIN) then
     write(c77,1000) dat4(1:12),dat4(13)/2
1000 format(12b6.6,b5.5)
     call unpack77(c77,0,decoded,unpk77_success) !Unpack to get msgsent
  else
     irc=-1
  endif
  nrc=irc

  return
end subroutine q65_dec1

subroutine q65_dec2(s3,nsubmode,b90ts,esnodb,irc,dat4,decoded)

! Attempt a q0, q1, or q2 decode using spcified AP information.

  use packjt77
  real s3(iz0,jz0)       !Silence compiler warning that wants to see a 2D array
  real s3prob(0:63,63)                   !Symbol-value probabilities
  integer dat4(13)
  character c77*77,decoded*37
  logical unpk77_success

  nFadingModel=1
  decoded='                                     '
  call q65_intrinsics_ff(s3,nsubmode,b90ts,nFadingModel,s3prob)
  call q65_dec(s3,s3prob,APmask,APsymbols,maxiters,esnodb,dat4,irc)
  if(sum(dat4).le.0) irc=-2
  nrc=irc
  if(irc.ge.0) then
     write(c77,1000) dat4(1:12),dat4(13)/2
1000 format(12b6.6,b5.5)
     call unpack77(c77,0,decoded,unpk77_success) !Unpack to get msgsent
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

  call q65_sync_curve(ccf2_avg,1,iz,rms1)
  call q65_sync_curve(ccf2,1,iz,rms2)

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
  integer, allocatable :: hist(:)

  allocate(hist(-64:LL-65))
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
  real, allocatable :: spec(:)

  allocate(spec(iz0))
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
           if(ii.ge.1 .and. ii.le.iz0) spec(i)=spec(i) + s1raw(ii,j)
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
  character(len=12),intent(out),optional :: dxcall  !Second callsign in message
  character(len=6),intent(out),optional :: dxgrid   !Third word in msg, if grid

  character*6 g1
  character*37 msg(MAXHIST)                      !Saved messages
  integer nf0(MAXHIST)                           !Saved frequencies
  logical isgrid                                 !Statement function
  data nhist/0/
  save nhist,nf0,msg

  isgrid(g1)=g1(1:1).ge.'A' .and. g1(1:1).le.'R' .and. g1(2:2).ge.'A' .and. &
       g1(2:2).le.'R' .and. g1(3:3).ge.'0' .and. g1(3:3).le.'9' .and.       &
       g1(4:4).ge.'0' .and. g1(4:4).le.'9' .and. g1(1:4).ne.'RR73'

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

subroutine q65_hist2(nfreq,msg0,callers,nhist2)

  use types
  use prog_args
  parameter (MAX_CALLERS=40)  !For multiple q3 decodes in NA VHf Contest mode
  character*37 msg0,msg
  type(q3list) callers(MAX_CALLERS)
  character*6 c6
  character*4 g4
  logical newcall,isgrid

  isgrid(g4)=g4(1:1).ge.'A' .and. g4(1:1).le.'R' .and. g4(2:2).ge.'A' .and. &
       g4(2:2).le.'R' .and. g4(3:3).ge.'0' .and. g4(3:3).le.'9' .and.       &
       g4(4:4).ge.'0' .and. g4(4:4).le.'9' .and. g4(1:4).ne.'RR73'

  msg=msg0
  if(index(msg,'/').gt.0) goto 900         !Ignore messages with compound calls
  i0=index(msg,' R ')
  if(i0.ge.7) msg=msg(1:i0)//msg(i0+3:)
  i1=index(msg,' ')
  c6='      '
  g4='    '
  if(i1.ge.4 .and. i1.le.13) then
     i2=index(msg(i1+1:),' ') + i1
     c6=msg(i1+1:i2-1)                     !Extract DX call
     g4=msg(i2+1:i2+4)                     !Extract DX grid
  endif

  newcall=.true.
  do i=1,nhist2
     if(callers(i)%call .eq. c6) then
        newcall=.false.
        callers(i)%nsec=time()
        callers(i)%nfreq=nfreq
        exit
     endif
  enddo

  if(newcall .and. isgrid(g4)) then
     if(nhist2.eq.MAX_CALLERS) then
! Purge the oldest caller
        callers(1:MAX_CALLERS-1)=callers(2:MAX_CALLERS)
        nhist2=nhist2-1
     endif
     nhist2=nhist2+1
     callers(nhist2)%call=c6
     callers(nhist2)%grid=g4
     callers(nhist2)%nsec=time()
     callers(nhist2)%nfreq=nfreq
  endif

  if(nhist2.ge.1 .and. nhist2.le.40) then
     open(24,file=trim(data_dir)//'/tsil.3q',status='unknown',     &
          form='unformatted')
     write(24) nhist2
     write(24) callers(1:nhist2)
     close(24)
  endif

900 return
end subroutine q65_hist2

end module q65
