subroutine getcand2(ss,savg0,nts_q65,nagain,nhsym,ntx30a,ntx30b,     &
     ntol,f0_selected,bAlso30,cand,ncand2)

! Get candidates for Q65 decodes, based on presence of sync tone.
  
  type candidate
     real :: snr          !Relative S/N of sync detection
     real :: f            !Freq of sync tone, 0 to 96000 Hz
     real :: xdt          !DT of matching sync pattern, -1.0 to +4.0 s
     integer :: ntrperiod !60 for Q65-60x, 30 for Q65-30x
     integer :: iseq      !0 for first half-minute, 1 for second half
  end type candidate

  parameter (NFFT=32768)                !FFTs done in symspec()
  parameter (MAX_CANDIDATES=50)
  type(candidate) :: cand(MAX_CANDIDATES)
  real ss(400,NFFT)                     !Symbol spectra
  real savg0(NFFT),savg(NFFT)           !Average spectra over whole Rx sequence
  integer ipk1(1)                       !Peak index of local portion of spectrum
  logical sync_ok                       !True if sync pattern is present
  logical*1 bAlso30
  data nseg/16/,npct/40/

  savg=savg0                            !Save the original spectrum
  nlen=NFFT/nseg
  do iseg=1,nseg                        !Normalize spectrum with nearby baseline
     ja=(iseg-1)*nlen + 1
     jb=ja + nlen - 1
     call pctile(savg(ja),nlen,npct,base)
     savg(ja:jb)=savg(ja:jb)/(1.015*base)
     savg0(ja:jb)=savg0(ja:jb)/(1.015*base)
  enddo

  df=96000.0/NFFT
  bw=65*nts_q65*1.666666667             !Bandwidth of Q65 signal
  nbw=bw/df + 1                         !Bandwidth in bins
  nb0=2*nts_q65                         !Range of peak search, in bins
  smin=1.4                              !First threshold
  nguard=5                              !Guard range in bins
  i1=1
  i2=NFFT-nbw-nguard
  if(nagain.ge.1) then
     i1=nint((1000.0*f0_selected-ntol)/df)
     i2=nint((1000.0*f0_selected+ntol)/df)
  endif

  j=0
  do i=i1,i2                         !Look for local peaks in average spectrum
     if(savg(i).lt.smin) cycle
     spk=maxval(savg(i:i+nb0))
     ipk1=maxloc(savg(i:i+nb0))
     i0=ipk1(1) + i - 1                         !Index of local peak in savg()
     fpk=0.001*i0*df                            !Frequency of peak (kHz)
! Check to see if sync tone is present.
     ntrperiod=60
     iseq=0
     if(nhsym.ge.200) then
        call q65_sync(ss,i0,nts_q65,ntrperiod,iseq,sync_ok,snr_sync,xdt)
        if(sync_ok) then
! Sync tone is present, we have a candidate for decoding
           j=j+1
           cand(j)%f=fpk
           cand(j)%xdt=xdt
           cand(j)%snr=snr_sync
           cand(j)%ntrperiod=ntrperiod
           cand(j)%iseq=iseq
           ia=max(1,min(i,i0-nguard))
           ib=min(i0+nbw+nguard,32768)
           savg(ia:ib)=0.
           if(j.ge.MAX_CANDIDATES) exit
        endif
     endif

     if(.not.bAlso30) cycle
     ntrperiod=30
     
     if(nhsym.le.200 .and. ntx30a.le.5) then
        call q65_sync(ss,i0,nts_q65,ntrperiod,iseq,sync_ok,snr_sync,xdt)
        if(sync_ok) then
! Sync tone is present, we have a candidate for decoding
           j=j+1
           cand(j)%f=fpk
           cand(j)%xdt=xdt
           cand(j)%snr=snr_sync
           cand(j)%ntrperiod=ntrperiod
           cand(j)%iseq=iseq
           ia=max(1,min(i,i0-nguard))
           ib=min(i0+nbw+nguard,32768)
           savg(ia:ib)=0.
           if(j.ge.MAX_CANDIDATES) exit
        endif
     endif

     iseq=1
     if(nhsym.ge.330 .and. ntx30b.le.5) then
        call q65_sync(ss,i0,nts_q65,ntrperiod,iseq,sync_ok,snr_sync,xdt)
        if(sync_ok) then
! Sync tone is present, we have a candidate for decoding
           j=j+1
           cand(j)%f=fpk
           cand(j)%xdt=xdt
           cand(j)%snr=snr_sync
           cand(j)%ntrperiod=ntrperiod
           cand(j)%iseq=iseq
           ia=max(1,min(i,i0-nguard))
           ib=min(i0+nbw+nguard,32768)
           savg(ia:ib)=0.
           if(j.ge.MAX_CANDIDATES) exit
        endif
     endif

  enddo
  ncand2=j                              !Total number of candidates found

  return
end subroutine getcand2
