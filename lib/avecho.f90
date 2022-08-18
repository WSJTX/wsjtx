subroutine avecho(id2,ndop,nfrit,nauto,nqual,f1,xlevel,snrdb,db_err,  &
     dfreq,width,bDiskData)

  integer TXLENGTH
  parameter (TXLENGTH=27648)           !27*1024
  parameter (NFFT=32768,NH=NFFT/2)
  parameter (NZ=4096)
  integer*2 id2(34560)                 !Buffer for Rx data
  real sa(NZ)      !Avg spectrum relative to initial Doppler echo freq
  real sb(NZ)      !Avg spectrum with Dither and changing Doppler removed
  integer nsum       !Number of integrations
  real dop0          !Doppler shift for initial integration (Hz)
  real dop           !Doppler shift for current integration (Hz)
  real s(8192)
  real x(NFFT)
  integer ipkv(1)
  logical ex
  logical*1 bDiskData
  complex c(0:NH)
  equivalence (x,c),(ipk,ipkv)
  common/echocom/nclearave,nsum,blue(NZ),red(NZ)
  common/echocom2/fspread_self,fspread_dx
  save dop0,sa,sb

  fspread=fspread_dx                !### Use the predicted Doppler spread ###
  if(bDiskData) fspread=width
  if(nauto.eq.1) fspread=fspread_self
  inquire(file='fspread.txt',exist=ex)
  if(ex) then
     open(39,file='fspread.txt',status='old')
     read(39,*) fspread
     close(39)
  endif
  fspread=min(max(0.1,fspread),700.0)
  width=fspread
  dop=ndop
  sq=0.
  do i=1,TXLENGTH
     x(i)=id2(i)
     sq=sq + x(i)*x(i)
  enddo
  xlevel=10.0*log10(sq/TXLENGTH)

  if(nclearave.ne.0) nsum=0
  if(nsum.eq.0) then
     dop0=dop                             !Remember the initial Doppler
     sa=0.                                !Clear the average arrays
     sb=0.
  endif

  x(TXLENGTH+1:)=0.
  x=x/TXLENGTH
  call four2a(x,NFFT,1,-1,0)
  df=12000.0/NFFT
  do i=1,8192                             !Get spectrum 0 - 3 kHz
     s(i)=real(c(i))**2 + aimag(c(i))**2
  enddo

  fnominal=1500.0           !Nominal audio frequency w/o doppler or dither
  ia=nint((fnominal+dop0-nfrit)/df)
  ib=nint((f1+dop-nfrit)/df)
  if(ia.lt.2048 .or. ib.lt.2048 .or. ia.gt.6144 .or. ib.gt.6144) then
     snrdb=0.
     db_err=0.
     dfreq=0.
     go to 900
  endif

  nsum=nsum+1
  do i=1,NZ
     sa(i)=sa(i) + s(ia+i-2048)    !Center at initial doppler freq
     sb(i)=sb(i) + s(ib+i-2048)    !Center at expected echo freq
  enddo
  call echo_snr(sa,sb,fspread,blue,red,snrdb,db_err,dfreq,snr_detect)
  nqual=snr_detect-2
  if(nqual.lt.0) nqual=0
  if(nqual.gt.10) nqual=10

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

!  write(*,3001) snrdb,db_err,dfreq,snr_detect,redmax,nqual,nsmo,nclearave,nsum
!3001 format('A',5f10.1,4i4)

900  return
end subroutine avecho
