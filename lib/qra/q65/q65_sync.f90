subroutine q65_sync(nutc,iwave,ntrperiod,mode_q65,codewords,ncw,nsps,   &
     nfqso,ntol,ndepth,lclearave,emedelay,xdt,f0,snr1,width,dat4,snr2,idec)

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
  parameter (NSTEP=8)                    !Step size nsps/NSTEP
  parameter (LN=2176*63)           !LN=LL*NN; LL=64*(mode_q65+2), NN=63
  parameter (PLOG_MIN=-240.0)            !List decoding threshold
  integer*2 iwave(0:12000*ntrperiod-1)   !Raw data
  integer isync(22)                      !Indices of sync symbols
  integer itone(85)
  integer codewords(63,206)
  integer dat4(13)
  integer ijpk(2)
  logical lclearave
  character*37 decoded
  real, allocatable :: s1(:,:)           !Symbol spectra, 1/8-symbol steps
  real, allocatable :: s3(:,:)           !Data-symbol energies s3(LL,63)
  real, allocatable :: ccf(:,:)          !CCF(freq,lag)
  real, allocatable :: ccf1(:)           !CCF(freq) at best lag
  real sync(85)                          !sync vector
  complex, allocatable :: c0(:)          !Complex spectrum of symbol
  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/
  data sync(1)/99.0/
  save sync

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

  allocate(s1(iz,jz))
  allocate(s3(-64:LL-65,63))
  allocate(c0(0:nfft-1))
  allocate(ccf(-ia2:ia2,-53:214))
  allocate(ccf1(-ia2:ia2))
  
  if(sync(1).eq.99.0) then               !Generate the sync vector
     sync=-22.0/63.0                     !Sync tone OFF  
     do k=1,22
        sync(isync(k))=1.0               !Sync tone ON
     enddo
  endif

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
     do i=1,nsmo
        call smo121(s1(1:iz,j),iz)
     enddo
  enddo

  i0=nint(nfqso/df)                           !Target QSO frequency
  if(i0-64.lt.1 .or. i0-65+LL.gt.iz) go to 900
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

  if(ncw.lt.1) go to 100
  
!######################################################################
! Try list decoding via "Deep Likelihood".

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
           itone(j)=-1
        else
           k=k+1
           itone(j)=codewords(k,imsg)
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
        f0=nfqso + (ipk-mode_q65)*df
        xdt=jpk*dtstep
        imsg_best=imsg
        ccf1=ccf(:,jpk)
     endif
  enddo  ! imsg

  i1=i0+ipk-64
  i2=i1+LL-1
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

  nsubmode=0
  if(mode_q65.eq.2) nsubmode=1
  if(mode_q65.eq.4) nsubmode=2
  if(mode_q65.eq.8) nsubmode=3
  if(mode_q65.eq.16) nsubmode=4
  nFadingModel=1
  baud=12000.0/nsps
  ibwa=1.8*log(baud*mode_q65) + 2
  ibwb=min(10,ibwa+4)

  do ibw=ibwa,ibwb
     b90=1.72**ibw
     b90ts=b90/baud
     call timer('dec1    ',0)
     call q65_dec1(s3,nsubmode,b90ts,codewords,ncw,esnodb,irc,dat4,decoded)
     call timer('dec1    ',1)
     if(irc.ge.0) then
        snr2=esnodb - db(2500.0/baud) + 3.0     !Empirical adjustment
        idec=1
        ic=ia2/4;
        base=(sum(ccf1(-ia2:-ia2+ic)) + sum(ccf1(ia2-ic:ia2)))/(2.0+2.0*ic);
        ccf1=ccf1-base
        smax=maxval(ccf1)
        if(smax.gt.10.0) ccf1=10.0*ccf1/smax
        go to 200
     endif
  enddo

!######################################################################
! Establish xdt, f0, and snr1 using sync symbols (and perhaps some AP symbols)
100 ccf=0.
  irc=-2
  dat4=0
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
  ijpk=maxloc(ccf(-ia:ia,:))
  ipk=ijpk(1)-ia-1
  jpk=ijpk(2)-53-1
  f0=nfqso + ipk*df
  xdt=jpk*dtstep
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
  ccf1=ccf(:,jpk)/rms
  if(snr1.gt.10.0) ccf1=(10.0/snr1)*ccf1

  if(iand(ndepth,16).eq.16) then
! Fill s3() from s1() here, then call q65_avg().
     i1=i0+ipk-64
     i2=i1+LL-1
     if(snr1.ge.2.8 .and. i1.ge.1 .and. i2.le.iz) then
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
!        write(*,3002) 'B',xdt,f0,sum(s3)
!3002    format(a1,f7.2,2f8.1)
        call q65_avg(nutc,ntrperiod,LL,nfqso,ntol,lclearave,xdt,f0,snr1,s3)
     endif
  endif

200 smax=maxval(ccf1)
  i1=-9999
  i2=-9999
  do i=-ia,ia
     if(i1.eq.-9999 .and. ccf1(i).ge.0.5*smax) i1=i
     if(i2.eq.-9999 .and. ccf1(-i).ge.0.5*smax) i2=-i
  enddo
  do i=-ia2,ia2
     freq=nfqso + i*df
     write(17,1100) freq,ccf1(i),xdt
1100 format(3f10.3)
  enddo
  close(17)
  width=df*(i2-i1)

900 return
end subroutine q65_sync

subroutine q65_dec1(s3,nsubmode,b90ts,codewords,ncw,esnodb,irc,dat4,decoded)

  use q65
  use packjt77
  real s3prob(0:63,63)                   !Symbol-value probabilities
  integer codewords(63,206)
  integer dat4(13)
  character c77*77,decoded*37
  logical unpk77_success
  
  nFadingModel=1
  decoded='                                     '
  call q65_intrinsics_ff(s3,nsubmode,b90ts,nFadingModel,s3prob)
  call q65_dec_fullaplist(s3,s3prob,codewords,ncw,esnodb,dat4,plog,irc)
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

  use q65
  use packjt77
  real s3prob(0:63,63)                   !Symbol-value probabilities
  integer dat4(13)
  character c77*77,decoded*37
  logical unpk77_success

  nFadingModel=1
  decoded='                                     '
  call q65_intrinsics_ff(s3,nsubmode,b90ts,nFadingModel,s3prob)
  call q65_dec(s3,s3prob,APmask,APsymbols,esnodb,dat4,irc)
  if(irc.ge.0) then
     write(c77,1000) dat4(1:12),dat4(13)/2
1000 format(12b6.6,b5.5)
     call unpack77(c77,0,decoded,unpk77_success) !Unpack to get msgsent
  endif

  return
end subroutine q65_dec2
