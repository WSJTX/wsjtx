program fcal

! Compute Intercept (A) and Slope (B) for a series of FreqCal measurements. 
  parameter(NZ=1000)
  implicit real*8 (a-h,o-z)
  real*8 fd(NZ),deltaf(NZ),r(NZ)
  character infile*50
  character line*80
  character cutc*8

  nargs=iargc()
  if(nargs.ne.1) then
     print*,'Usage:   fcal <infile>'
     print*,'Example: fcal fmtave.out'
     go to 999
  endif
  call getarg(1,infile)

  open(10,file=infile,status='old',err=997)
  open(12,file='fcal.out',status='unknown')
  open(13,file='fcal.plt',status='unknown')

  i=0
  do j=1,9999
     read(10,1000,end=10) line
1000 format(a80)
     i0=index(line,' 0 ')
     i1=index(line,' 1 ')
     if(i0.le.0 .and. i1.le.0) then
        read(line,*,err=5) f,df
        ncal=1
        i=i+1
        fd(i)=f
        deltaf(i)=df
     else if(i1.gt.0) then
        i=i+1
        read(line,*,err=5) f,df,ncal,nn,rr,cutc
        fd(i)=f
        deltaf(i)=df
        r(i)=0.d0
     endif
5    continue
  enddo

10 iz=i
  if(iz.lt.2) go to 998
  call fit(fd,deltaf,r,iz,a,b,sigmaa,sigmab,rms)

  write(*,1002) 
1002 format('    Freq      DF     Meas Freq     Resid'/        &
            '   (MHz)     (Hz)      (MHz)        (Hz)'/        &
            '-----------------------------------------')       
  do i=1,iz
     fm=fd(i) + 1.d-6*deltaf(i)
     calfac=1.d0 + 1.d-6*deltaf(i)/fd(i)
     write(*,1010) fd(i),deltaf(i),fm,r(i)
     write(13,1010) fd(i),deltaf(i),fm,r(i)
1010 format(f8.3,f9.3,f14.9,f9.3,2x,a6)
  enddo
  calfac=1.d0 + 1.d-6*b
  err=1.d-6*sigmab

  if(iz.ge.3) then
     write(*,1100) a,b,rms
1100 format(/'A:',f8.2,' Hz    B:',f9.4,' ppm    StdDev:',f7.3,' Hz')
  if(iz.gt.2) write(*,1110) sigmaa,sigmab
1110 format('err:',f6.2,9x,f9.4,23x,f13.9)
  else
     write(*,1120) a,b
1120 format(/'A:',f8.2,' Hz    B:',f9.4)
  endif

  write(12,1130) a,b
1130 format(f10.4)

  go to 999

997 print*,'Cannot open input file: ',infile
  go to 999
998 print*,'Input file must contain at least 2 valid measurement pairs'

999 end program fcal

subroutine fit(x,y,r,iz,a,b,sigmaa,sigmab,rms)
  implicit real*8 (a-h,o-z)
  real*8 x(iz),y(iz),r(iz)

  sx=0.d0
  sy=0.d0
  sxy=0.d0
  sx2=0.d0
  do i=1,iz
     sx=sx + x(i)
     sy=sy + y(i)
     sxy=sxy + x(i)*y(i)
     sx2=sx2 + x(i)*x(i)
  enddo
  delta=iz*sx2 - sx*sx
  a=(sx2*sy - sx*sxy)/delta
  b=(iz*sxy - sx*sy)/delta

  sq=0.d0
  do i=1,iz
     r(i)=y(i) - (a + b*x(i))
     sq=sq + r(i)**2
  enddo
  rms=0.
  sigmaa=0.
  sigmab=0.
  if(iz.ge.3) then
     rms=sqrt(sq/(iz-2))
     sigmaa=sqrt(rms*rms*sx2/delta)
     sigmab=sqrt(iz*rms*rms/delta)
  endif

  return
end subroutine fit
