subroutine calibrate(data_dir,iz,a,b,rms,sigmaa,sigmab,irc)

! Average groups of frequency-calibration measurements, then fit a
! straight line for slope and intercept.

  parameter (NZ=1000)
  implicit real*8 (a-h,o-z)
  character*(*) data_dir
  character*256 infile,outfile
  character*8 cutc,cutc1
  character*1 c1
  real*8 fd(NZ),deltaf(NZ),r(NZ),rmsd(NZ)
  integer nn(NZ)

  infile=trim(data_dir)//'/'//'fmt.all'
  outfile=trim(data_dir)//'/'//'fcal2.out'

  open(10,file=trim(infile),status='old',err=996)
  open(12,file=trim(outfile),status='unknown',err=997)

  nkhz0=0
  sum=0.d0
  sumsq=0.d0
  n=0
  j=0
  do i=1,99999
     read(10,*,end=10,err=995) cutc,nkHz,ncal,noffset,faudio,df,dblevel,snr
     if((nkHz.ne.nkHz0) .and. i.ne.1) then
        ave=sum/n
        rms=0.d0
        if(n.gt.1) then
           rms=sqrt(abs(sumsq - sum*sum/n)/(n-1.d0))
        endif
        fMHz=0.001d0*nkHz0
        j=j+1
        fd(j)=fMHz
        deltaf(j)=ave
        r(j)=0.d0
        rmsd(j)=rms
        nn(j)=n
        sum=0.d0
        sumsq=0.d0
        n=0
     endif
     dial_error=faudio-noffset
     sum=sum + dial_error
     sumsq=sumsq + dial_error**2
     n=n+1
     if(n.eq.1) then
        cutc1=cutc
        ncal0=ncal
     endif
     nkHz0=nkHz
  enddo

10 ave=sum/n
  rms=0.d0
  if(n.gt.0) then
     rms=sqrt((sumsq - sum*sum/n)/(n-1.d0))
  endif
  fMHz=0.001d0*nkHz
  j=j+1
  fd(j)=fMHz
  deltaf(j)=ave
  r(j)=0.d0
  rmsd(j)=rms
  nn(j)=n
  iz=j
  if(iz.lt.2) go to 998

  call fitcal(fd,deltaf,r,iz,a,b,sigmaa,sigmab,rms)

  write(12,1002) 
1002 format('    Freq      DF     Meas Freq    N    rms    Resid'/        &
            '   (MHz)     (Hz)      (MHz)          (Hz)     (Hz)'/        &
            '----------------------------------------------------')       
  irc=0
  do i=1,iz
     fm=fd(i) + 1.d-6*deltaf(i)
     c1=' '
     if(rmsd(i).gt.1.0d0) c1='*'
     write(12,1012)  fd(i),deltaf(i),fm,nn(i),rmsd(i),r(i),c1
1012 format(f8.3,f9.3,f14.9,i4,f7.2,f9.3,1x,a1)
  enddo
  go to 999

995 irc=-4; iz=i; go to 999
996 irc=-1; go to 999
997 irc=-2; go to 999
998 irc=-3
999 continue
  close(10)
  close(12)

  return
end subroutine calibrate
