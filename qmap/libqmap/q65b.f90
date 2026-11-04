module qmap_q65_samples_mod
  use iso_fortran_env, only: int16
  implicit none
contains
  subroutine build_q65_samples(k0,nfft2,df,iseq,iwave)
    use cacb_mod, only: ca
    integer, intent(in) :: k0,nfft2,iseq
    real, intent(in) :: df
    integer(int16), intent(out) :: iwave(:)
    integer, parameter :: MAXFFT2=336000
    complex cx(0:MAXFFT2-1),cz(0:MAXFFT2)
    save cx,cz
    integer i,j,ja,jb
    real fac,r

    fac=1.0/nfft2
    cx(0:nfft2-1)=fac*ca(k0:k0+nfft2-1)
    cz(0:MAXFFT2-1)=cx
    cz(MAXFFT2)=0.
    ja=nint(500.0/df)
    jb=nint(2500.0/df)
    do i=0,ja
       r=0.5*(1.0+cos(i*3.14159/ja))
       cz(ja-i)=r*cz(ja-i)
       cz(jb+i)=r*cz(jb+i)
    enddo
    cz(ja+jb+1:)=0.

    call four2a(cz,2*nfft2,1,1,-1)
    do i=0,nfft2-1
       j=nfft2-1-i
       iwave(2*i+2)=nint(real(cz(j)))
       iwave(2*i+1)=nint(aimag(cz(j)))
    enddo
    iwave(2*nfft2+1:)=0
    if(iseq.eq.1) iwave(1:360000)=iwave(360001:720000)
  end subroutine build_q65_samples
end module qmap_q65_samples_mod

subroutine q65b(nutc,nqd,fcenter,nfcal,nfsample,ikhz,mousedf,ntol,          &
     ntrperiod,iseq,mycall0,hiscall0,hisgrid,mode_q65,f0,fqso,nkhz_center,  &
     newdat,nagain,bClickDecode,max_drift,offset,ndepth,datetime,nCFOM,     &
     ndop00,nhsym,idec)

! This routine provides an interface between QMAP and the Q65 decoder
! in WSJT-X.  All arguments are input data obtained from the QMAP GUI.
! Raw Rx data are available as the complex spectrum ca(nfft_big_active)
! in cacb_mod.  Decoded messages are sent back to the GUI.

  use q65_decode
  use wavhdr
  use timer_module, only: timer
  use map65_mmdec_mod, only: map65_mmdec
  use qmap_decode_ipc, only: max_decode_rows, decode_row_length,           &
       ndecodes, result, result2
  use iso_fortran_env, only: int16
  use cacb_mod, only: init_cacb
  use qmap_q65_samples_mod, only: build_q65_samples
  use qmap_params, only: nrate_active, nfft_active, nfft_big_active

  parameter (MAXFFT2=336000)               !56*6000 (downsampled to 12 kHz: independent of input rate)
  parameter (NMAX=60*12000)
  parameter (RAD=57.2957795)
  type(hdr) h
  integer(int16) iwave(300*12000)
  integer offset
  real*8 fcenter,freq0,freq1
  logical*1 bClickDecode
  character*12 mycall0,hiscall0
  character*12 mycall,hiscall
  character*6 hisgrid
  character*4 grid4
  character*3 csubmode
  character*17 fname
  character(len=decode_row_length) ctmp
  character*20 datetime,datetime1
  data ifile/0/
  save
  
  call init_cacb(nfft_big_active)
  bwhalf=real(nrate_active)/2000.0         !Half the sampled bandwidth (kHz): 48 at 96 kHz

  if(mycall0(1:1).ne.' ') mycall=mycall0
  if(hiscall0(1:1).ne.' ') hiscall=hiscall0
  if(hisgrid(1:4).ne.'    ') grid4=hisgrid(1:4)

! Find best frequency from sync_dat, the "orange sync curve".
  df3=real(nrate_active)/real(nfft_active)
  ipk=(1000.0*f0-1.0)/df3
  if(nagain.ge.2) ipk = nint(1000.0*(fqso-nkhz_center+bwhalf)/df3)
  nfft1=nfft_big_active
  nfft2=MAXFFT2
  df=real(nrate_active)/nfft1
  nh=nfft2/2
  k0=nint((ipk*df3-1000.0)/df)
  if(k0.lt.nh .or. k0.gt.nfft1-nfft2+1) go to 900
  call build_q65_samples(k0,nfft2,df,iseq,iwave)

  nsubmode=mode_q65-1
  nfa=990                   !Tight limits around ipk for the wideband decode
  nfb=1010
  if(nagain.ge.1) then      !For nagain>=1, use limits of +/- ntol
     nfa=max(100,1000-ntol)
     nfb=min(2500,1000+ntol)
  endif
  nsnr0=-99             !Default snr for no decode

  csubmode(1:2)='60'
  csubmode(3:3)=char(ichar('A')+nsubmode)
  nhhmmss=100*nutc
  nutc1=nutc
  datetime(12:13)='00'
  datetime1=datetime
  if(ntrperiod.eq.30) then
     csubmode(1:2)='30'
     nhhmmss=100*nutc + iseq*30
     nutc1=nhhmmss
     if(iseq.eq.1) datetime1(12:13)='30'
  endif

  if(nagain.ge.2) then
     ifile=ifile+1
     write(fname,1000) ifile
1000 format('000000_',i6.6,'.wav')
     open(27,file=fname,status='unknown',access='stream')
     if(nagain.eq.2) then
        h=default_header(12000,60*12000)
        ia=1
        ib=60*12000
     else if(nagain.eq.3) then
        h=default_header(12000,30*12000)
        ia=1
        ib=30*12000
     else
        h=default_header(12000,30*12000)
        ia=30*12000 + 1
        ib=60*12000
     endif
     write(27) h,iwave(ia:ib)
     close(27)
     go to 900
  endif
  
! NB: Frequency of ipk is now shifted to 1000 Hz.
  nagain2=0
  call map65_mmdec(nutc1,iwave,nqd,ntrperiod,nsubmode,nfa,nfb,1000,ntol,     &
       newdat,nagain2,max_drift,ndepth,mycall,hiscall,hisgrid)
  MHz=fcenter
  freq0=MHz + 0.001d0*ikhz

  if(nsnr0.gt.-99) then

     do i=1,ndecodes                        !Check for dupes
        i1=index(result(i)(42:),trim(msg0))
!          If this is a dupe, don't save it again:
        if(i1.gt.0 .and. (.not.bClickDecode .or. nhsym.eq.390)) go to 800
     enddo
     
     nq65df=nint(1000*(0.001*k0*df+nkhz_center-bwhalf+1.000-1.27046-ikhz))-nfcal
     nq65df=nq65df + nfreq0 - 1000
     ikhz1=ikhz
     ndf=nq65df
     if(ndf.gt.500) ikhz1=ikhz + (nq65df+500)/1000
     if(ndf.lt.-500) ikhz1=ikhz + (nq65df-500)/1000
     ndf=nq65df - 1000*(ikhz1-ikhz)
     freq1=freq0 + 0.001d0*(ikhz1-ikhz)
     frx=0.001*k0*df+nkhz_center-bwhalf+1.0 - 0.001*nfcal
     fsked=frx - 0.001*ndop00/2.0 - 0.001*offset
     ctmp=csubmode//'  '//trim(msg0)
     ndecodes=min(ndecodes+1,max_decode_rows)
     write(result(ndecodes),1120) nhhmmss,frx,fsked,xdt0,nsnr0,trim(ctmp)
1120 format(i6.6,f9.3,f7.1,f7.2,i5,2x,a)
     write(result2(ndecodes),1125) fsked    !liveCQ
1125 format(f7.3)                           !liveCQ & changed from(f7.1) to give fsked Hz accuracy
     write(12,1130) datetime1,trim(result(ndecodes)(7:))
1130 format(a13,1x,a)
     result(ndecodes)=trim(result(ndecodes))//char(0)
     result2(ndecodes)=trim(result2(ndecodes))//char(0)
800  idec=0
  endif

900 flush(12)
  return
end subroutine q65b
