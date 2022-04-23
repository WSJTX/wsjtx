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

!  call pctile(sb(i1),i2-i1,50,r0)
!  call pctile(sb(i3+1),i4-i3,50,r1)
!  ave=0.5*(r0+r1)
!  blue=sa/ave
!  red=sb/ave

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
  db_err=99.0
  if(psig.gt.perr) db_err=snrdb - db((psig-perr)/pnoise_2500)
  if(db_err.lt.0.5) db_err=0.5

  return
end subroutine echo_snr
