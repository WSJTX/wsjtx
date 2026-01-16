subroutine avecho(id2_0,ndop,nfrit,nauto,ndf,navg,nqual,f1,xlevel,  &
     snrdb,db_err,dfreq,width,bDiskData,bEchoCall,txcall,rxcall)

  parameter (NTX=6*4096)
  parameter (NFFT=32768,NH=NFFT/2)
  parameter (NZ=4096)
  integer*2 id2_0(NTX+4096)                 !Raw Rx data
  integer*2 id2(NTX+4096)                   !Local copy of Rx data
  integer*2 id2a(15)                        !Just the parameter portion
  integer*4 itone4(6)
  integer*1 itone1(6)
  real sa(NZ)      !Avg spectrum relative to initial Doppler echo freq
  real sb(NZ)      !Avg spectrum with Dither and changing Doppler removed
  real, dimension (:,:), allocatable :: sax  !2D sa spectrum with navg slots
  real, dimension (:,:), allocatable :: sbx  !2D sb spectrum with navg slots
  integer nsum       !Number of integrations
  real dop0          !Doppler shift for initial integration (Hz)
  real dop           !Doppler shift for current integration (Hz)
  real s(8192)
  real x(NFFT)
  integer ipkv(1)
  logical*1 bDiskData,bEchoCall
  complex c(0:NH)
  character*6 txcall,rxcall
  equivalence (itone1,id2a(13))
  equivalence (x,c),(ipk,ipkv)
  common/echocom/nclearave,nsum,blue(NZ),red(NZ)
  common/echocom2/fspread_self,fspread_dx
  data navg0/-1/
  save dop0,navg0,sax,sbx

  if(ndf.eq.-999) stop                !Silence compiler warning
  if(bEchoCall .and. .not.bDiskData) then
! Calculate the transmitted tones for real-time Echo Call testing
     call gen_echocall(txcall,itone4)
     itone1=itone4
     id2_0(13:15)=id2a(13:15)           !Copy the tone values into id2_0
  endif

  fspread=fspread_dx                    !Use the predicted spread
  if(bDiskData) fspread=width
  if(nauto.eq.1) fspread=fspread_self
  fspread=min(max(0.04,fspread),700.0)
  width=fspread

  if(navg.ne.navg0) then
! If navg changes, start fresh by reallocating sax and sbx
     if(allocated(sax)) deallocate(sax)
     if(allocated(sbx)) deallocate(sbx)
     allocate(sax(1:navg,1:NZ))
     allocate(sbx(1:navg,1:NZ))
     nsum=0
     navg0=navg
  endif
  
  sq=sum(float(id2_0(16:NTX))**2)
  xlevel=10.0*log10(sq/(NTX-15))

  if(navg.eq.1) nclearave=1
  if(nclearave.ne.0) nsum=0
  dop=ndop
  if(nsum.eq.0) then
     dop0=dop                             !Remember the initial Doppler
     sax=0.                               !Clear the average arrays
     sbx=0.
  endif

  df=12000.0/NFFT
  fnominal=1500.0           !Nominal audio frequency w/o doppler or dither
  ia=nint((fnominal+dop0-nfrit)/df)
  ib=nint((f1+dop-nfrit)/df)
  snrdb=0.
  db_err=0.
  dfreq=0.

! If some parameter is way off, abort this procedure
  if(ia.lt.2048 .or. ib.lt.2048 .or. ia.gt.6144 .or. ib.gt.6144) go to 900
       
! Copy data from id2_0 to id2
  id2=id2_0

! Shift all tone frequencies to 1500 Hz, returning modified data in id2
! and decoded message in rxcall.
  rxcall='      '
  call decode_echo(id2,.false.,rxcall)

  x(1:15)=0.
  x(16:NTX)=id2(16:NTX)
  x(NTX+1:)=0.
  x=x/NTX
  call four2a(x,NFFT,1,-1,0)              !Compute the tone-aligned spectrum
  do i=1,8192                             !Array s(1:8192) is spectrum 0-3 kHz
     s(i)=real(c(i))**2 + aimag(c(i))**2
  enddo

  nsum=nsum+1
  j=mod(nsum-1,navg)+1
  do i=1,NZ
     sax(j,i)=s(ia+i-2048)    !Center at initial doppler freq
     sbx(j,i)=s(ib+i-2048)    !Center at expected echo freq
     sa(i)=sum(sax(1:navg,i))
     sb(i)=sum(sbx(1:navg,i))
  enddo

  call echo_snr(sa,sb,fspread,blue,red,snrdb,db_err,dfreq,snr_detect)

  db_err=db_err/sqrt(float(nsum))
  nqual=snr_detect-2
  if(nqual.lt.0) nqual=0
  if(nqual.gt.10) nqual=10
  xdt=0.
 
! Scale for plotting
  redmax=maxval(red)
  fac=10.0/max(redmax,10.0)
  blue=fac*blue
  red=fac*red
  nsmo=max(0.0,0.25*width/df)
  do i=1,nsmo
     call smo121(red,NZ)
     call smo121(blue,NZ)
  enddo

  ia=50.0/df
  ib=250.0/df
  call pctile(red(ia:ib),ib-ia+1,50,bred1)
  call pctile(blue(ia:ib),ib-ia+1,50,bblue1)
  ia=1250.0/df
  ib=1450.0/df
  call pctile(red(ia:ib),ib-ia+1,50,bred2)
  call pctile(blue(ia:ib),ib-ia+1,50,bblue2)

  red=red-0.5*(bred1+bred2)
  blue=blue-0.5*(bblue1+bblue2)

900 call sleep_msec(10)   !Avoid the "blue Decode button" syndrome
  if(abs(dfreq).ge.1000.0) dfreq=0.

  return
end subroutine avecho
