module q65

  parameter (NSTEP=8)          !Number of time bins per symbol in s1, s1a, s1b
  parameter (PLOG_MIN=-242.0)        !List decoding threshold
  integer nsave,nlist,LL0,iz0,jz0
  integer listutc(10)
  integer apsym0(58),aph10(10)
  integer apmask1(78),apsymbols1(78)
  integer apmask(13),apsymbols(13)
  integer,dimension(22) ::  isync = (/1,9,12,13,15,22,23,26,27,33,35,   &
                                     38,46,50,55,60,62,66,69,74,76,85/)
  integer codewords(63,206)
  integer ibwa,ibwb,ncw,nsps,mode_q65,nfa,nfb
  integer idf,idt,ibw
  integer istep,nsmo,lag1,lag2,npasses,nused,iseq,ncand
  integer i0,j0
  integer navg(0:1)
  logical lnewdat
  real candidates(20,3)                    !snr, xdt, and f0 of top candidates
  real,allocatable,save :: s1a(:,:,:)      !Cumulative symbol spectra
  real sync(85)                            !sync vector
  real df,dtstep,dtdec,f0dec,ftol

contains

subroutine q65_dec0(iavg,nutc,iwave,ntrperiod,nfqso,ntol,ndepth,lclearave,  &
     emedelay,xdt,f0,snr1,width,dat4,snr2,idec)

! Top-level routine in q65 module
!   - Compute symbol spectra
!   - Attempt sync and q3 decode using all 85 symbols
!   - If that fails, try sync with 22 symbols and standard q[0124] decode
  
! Input:  iavg                   0 for single-period decode, 1 for average
!         iwave(0:nmax-1)        Raw data
!         ntrperiod              T/R sequence length (s)
!         nfqso                  Target frequency (Hz)
!         ntol                   Search range around nfqso (Hz)
!         ndepth                 Requested decoding depth
!         lclearave              Flag to clear the accumulating array
!         emedelay               Extra delay for EME signals
! Output: xdt                    Time offset from nominal (s)
!         f0                     Frequency of sync tone
!         snr1                   Relative SNR of sync signal
!         width                  Estimated Doppler spread
!         dat4(13)               Decoded message as 13 six-bit integers
!         snr2                   Estimated SNR of decoded signal
!         idec                   Flag for decing results
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
  real, allocatable :: s1(:,:)           !Symbol spectra, 1/8-symbol steps
  real, allocatable :: s3(:,:)           !Data-symbol energies s3(LL,63)
  real, allocatable :: ccf1(:)           !CCF(freq) at fixed lag (red)
  real, allocatable :: ccf2(:)           !Max CCF(freq) at any lag (orange)
  data first/.true./
  save first

  if(nutc+ndepth.eq.-999) stop           !Silence compiler warnings

! Set some parameters and allocate storage for large arrays
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
  jz=(txt+1.0)*12000.0/istep             !Number of symbol/NSTEP bins
  if(nsps.ge.6912) jz=(txt+2.0)*12000.0/istep   !For TR 60 s and higher
  ftol=ntol
  ia=ntol/df
  ia2=max(ia,10*mode_q65,nint(100.0/df))
  nsmo=int(0.7*mode_q65*mode_q65)
  if(nsmo.lt.1) nsmo=1
  if(first) then                         !Generate the sync vector
     sync=-22.0/63.0                     !Sync tone OFF  
     do k=1,22
        sync(isync(k))=1.0               !Sync tone ON
     enddo
  endif

  allocate(s1(iz,jz))
  allocate(s3(-64:LL-65,63))
  allocate(ccf1(-ia2:ia2))
  allocate(ccf2(iz))
  if(LL.ne.LL0 .or. iz.ne.iz0 .or. jz.ne.jz0 .or. lclearave) then
     if(allocated(s1a)) deallocate(s1a)
     allocate(s1a(iz,jz,0:1))
     s1a=0.
     navg=0
     LL0=LL
     iz0=iz
     jz0=jz
     lclearave=.false.
  endif
  dtstep=nsps/(NSTEP*12000.0)                 !Step size in seconds
  lag1=-1.0/dtstep
  lag2=1.0/dtstep + 0.9999
  if(nsps.ge.3600 .and. emedelay.gt.0) lag2=4.0/dtstep + 0.9999  !Include EME
  j0=0.5/dtstep
  if(nsps.ge.7200) j0=1.0/dtstep              !Nominal start-signal index

  s3=0.
  if(iavg.eq.0) then
     call timer('q65_syms',0)
! Compute symbol spectra with NSTEP time bins per symbol
     call q65_symspec(iwave,ntrperiod*12000,iz,jz,s1)
     call timer('q65_syms',1)
  else
     s1=s1a(:,:,iseq)
  endif

  i0=nint(nfqso/df)                             !Target QSO frequency
  if(i0-64.lt.1 .or. i0-65+LL.gt.iz) go to 900  !Frequency out of range
  call pctile(s1(i0-64:i0-65+LL,1:jz),LL*jz,40,base)
  s1=s1/base

! Apply fast AGC to the symbol spectra
  s1max=20.0                                  !Empirical choice
  do j=1,jz                                   !### Maybe wrong way? ###
     smax=maxval(s1(i0-64:i0-65+LL,j))
     if(smax.gt.s1max) s1(i0-64:i0-65+LL,j)=s1(i0-64:i0-65+LL,j)*s1max/smax
  enddo

  dat4=0
  if(ncw.gt.0 .and. iavg.lt.2) then
! Try list decoding via "Deep Likelihood".
     call timer('ccf_85  ',0)
! Try to synchronize using all 85 symbols
     call q65_ccf_85(s1,iz,jz,nfqso,ia,ia2,ipk,jpk,f0,xdt,imsg_best,ccf1)
     call timer('ccf_85  ',1)

     call timer('list_dec',0)
     call q65_dec_q3(s1,iz,jz,s3,LL,ipk,jpk,snr2,dat4,idec,decoded)
     call timer('list_dec',1)
  endif

! Get 2d CCF and ccf2 using sync symbols only
  call q65_ccf_22(s1,iz,jz,nfqso,ipk,jpk,f0a,xdta,ccf2)
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

! Estimate frequenct spread
  i1=-9999
  i2=-9999
  do i=-ia,ia
     if(i1.eq.-9999 .and. ccf1(i).ge.0.5*smax) i1=i
     if(i2.eq.-9999 .and. ccf1(-i).ge.0.5*smax) i2=-i
  enddo
  width=df*(i2-i1)

  if(ncw.eq.0) ccf1=0.
  call q65_write_red(iz,ia2,xdt,ccf1,ccf2)

  if(iavg.eq.2) then
     call q65_dec_q012(s3,LL,snr2,dat4,idec,decoded)
  endif

900 return
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
  do j=1,jz                              !Compute symbol spectra at step size
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
  enddo
  if(lnewdat) then
     s1a(:,:,iseq)=s1a(:,:,iseq) + s1
     navg(iseq)=navg(iseq) + 1
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
        if(irc.ge.0) then
           snr2=esnodb - db(2500.0/baud) + 3.0     !Empirical adjustment
           idec=iaptype
           go to 100
        endif
     enddo
  enddo

100 return
end subroutine q65_dec_q012

subroutine q65_ccf_85(s1,iz,jz,nfqso,ia,ia2,ipk,jpk,f0,xdt,imsg_best,ccf1)

! Attempt synchronization using all 85 symbols, in advance of an
! attempt at q3 decoding.  Return ccf1 for the "red sync curve".
  
  real s1(iz,jz)
  real, allocatable :: ccf(:,:)          !CCF(freq,lag)
  real ccf1(-ia2:ia2)
  integer ijpk(2)
  integer itone(85)

  allocate(ccf(-ia2:ia2,-53:214))
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
  enddo  ! imsg
  deallocate(ccf)

  return
end subroutine q65_ccf_85

subroutine q65_ccf_22(s1,iz,jz,nfqso,ipk,jpk,f0,xdt,ccf2)

! Attempt synchronization using only the 22 sync symbols.  Return ccf2
! for the "orange sync curve".

  real s1(iz,jz)
  real ccf2(iz)                               !Orange sync curve
  real, allocatable :: xdt2(:)
  integer, allocatable :: indx(:)

  allocate(xdt2(iz))
  allocate(indx(iz))

  ccfbest=0.
  ibest=0
  lagpk=0
  lagbest=0
  do i=1,iz
     ccfmax=0.
     do lag=lag1,lag2
        ccft=0.
        do k=1,85
           n=NSTEP*(k-1) + 1
           j=n+lag+j0
           if(j.ge.1 .and. j.le.jz) then
              ccft=ccft + sync(k)*s1(i,j)
           endif
        enddo
        if(ccft.gt.ccfmax) then
           ccfmax=ccft
           lagpk=lag
        endif
     enddo
     ccf2(i)=ccfmax
     xdt2(i)=lagpk*dtstep
     if(ccfmax.gt.ccfbest .and. abs(i*df-nfqso).le.ftol) then
        ccfbest=ccfmax
        ibest=i
        lagbest=lagpk
     endif
  enddo

! Parameters for the top candidate:
  ipk=ibest - i0
  jpk=lagbest
  f0=nfqso + ipk*df
  xdt=jpk*dtstep

! Save parameters for best candidates
  i1=max(nfa,100)/df
  i2=min(nfb,4900)/df
  jzz=i2-i1+1
  call pctile(ccf2(i1:i2),jzz,40,base)
  ccf2=ccf2/base
  call indexx(ccf2(i1:i2),jzz,indx)
  ncand=0
  maxcand=20
  do j=1,20
     i=indx(jzz-j+1)+i1-1
     if(ccf2(i).lt.3.4) exit                !Candidate limit
     f=i*df
     if(f.ge.(nfqso-ftol) .and. f.le.(nfqso+ftol)) cycle
     i3=max(1,i-67*mode_q65)
     i4=min(iz,i+3*mode_q65)
     biggest=maxval(ccf2(i3:i4))
     if(ccf2(i).ne.biggest) cycle
     ncand=ncand+1
     candidates(ncand,1)=ccf2(i)
     candidates(ncand,2)=xdt2(i)
     candidates(ncand,3)=f
     if(ncand.ge.maxcand) exit
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
  call q65_dec(s3,s3prob,APmask,APsymbols,esnodb,dat4,irc)
  if(sum(dat4).le.0) irc=-2
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

  return
end subroutine q65_s1_to_s3

subroutine q65_write_red(iz,ia2,xdt,ccf1,ccf2)

! Write data for the red and orange sync curves to LU 17.

  real ccf1(-ia2:ia2)
  real ccf2(iz)

  call q65_sync_curve(ccf1,-ia2,ia2,rms1)
  call q65_sync_curve(ccf2,1,iz,rms2)

  rewind 17
  write(17,1000) xdt
  do i=1,iz
     freq=i*df
     ii=i-i0
     if(freq.ge.float(nfa) .and. freq.le.float(nfb)) then
        ccf1a=-99.0
        if(ii.ge.-ia2 .and. ii.le.ia2) ccf1a=ccf1(ii)
        write(17,1000) freq,ccf1a,ccf2(i)
1000    format(3f10.3)
     endif
  enddo

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
  if(rms1.gt.0.0) ccf1=2.0*ccf1/rms1
  smax1=maxval(ccf1)
  if(smax1.gt.10.0) ccf1=10.0*ccf1/smax1

  return
end subroutine q65_sync_curve

end module q65
