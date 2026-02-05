subroutine echo_snr(sa,sb,fspread,blue,red,snrdb,db_err,fpeak,snr_detect)

  parameter (NZ=4096)
  real sa(NZ)
  real sb(NZ)
  real blue(NZ)
  real red(NZ)
  integer ipkv(1)
  equivalence (ipk,ipkv)

  df=12000.0/32768.0
  wh=0.5*fspread+10.0
  i1=nint((1500.0 - 2.0*wh)/df) - 2048
  i2=nint((1500.0 - wh)/df) - 2048
  i3=nint((1500.0 + wh)/df) - 2048
  i4=nint((1500.0 + 2.0*wh)/df) - 2048

  baseline=(sum(sb(i1:i2-1)) + sum(sb(i3+1:i4)))/(i2+i4-i1-i3)
  blue=sa/baseline
  red=sb/baseline
  psig=sum(red(i2:i3)-1.0)
  pnoise_2500 = 2500.0/df
  snrdb=db(psig/pnoise_2500)

  smax=0.
  mh=max(1,nint(0.2*fspread/df))
  do i=i2,i3
     ssum=sum(red(i-mh:i+mh))
     if(ssum.gt.smax) then
        smax=ssum
        ipk=i
     endif
  enddo
  fpeak=ipk*df - 750.0

  call averms(red(i1:i2-1),i2-i1,-1,ave1,rms1)
  call averms(red(i3+1:i4),i4-i3,-1,ave2,rms2)
  perr=0.707*(rms1+rms2)*sqrt(float(i2-i1+i4-i3))
  snr_detect=psig/perr
  db_err=0.8
  if(snrdb.lt.-10.0) db_err=0.8
  if(snrdb.lt.-11.0) db_err=0.9
  if(snrdb.lt.-12.0) db_err=1.0
  if(snrdb.lt.-13.0) db_err=1.1
  if(snrdb.lt.-14.0) db_err=1.2
  if(snrdb.lt.-15.0) db_err=1.3
  if(snrdb.lt.-16.0) db_err=1.4
  if(snrdb.lt.-17.0) db_err=1.5
  if(snrdb.lt.-18.0) db_err=1.6
  if(snrdb.lt.-19.0) db_err=1.7
  if(snrdb.lt.-20.0) db_err=1.8

  return
end subroutine echo_snr
