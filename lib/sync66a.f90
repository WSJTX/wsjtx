subroutine sync66a(iwave,nmax,nsps,nfqso,ntol,xdt,f0,snr1)

  parameter (NSTEP=4)                      !Quarter-symbol steps
  parameter (IZ=1600,JZ=352,NSPSMAX=1920)
  integer*2 iwave(0:nmax-1)                !Raw data
  integer b11(11)                          !Barker 11 code
  integer ijpk(2)                          !Indices i and j at peak of sync_sig
  real s1(IZ,JZ)                           !Symbol spectra
  real x(JZ)                               !Work array; 2FSK sync modulation
  real sync(4*85)                          !sync vector
  real sync_sig(-64:64,-15:15)
  complex c0(0:NSPSMAX)                    !Complex spectrum of symbol
  data b11/1,1,1,0,0,0,1,0,0,1,0/          !Barker 11 code
  data sync(1)/99.0/
  save sync

  if(sync(1).eq.99.0) then
     sync=0.
     do k=1,22
        kk=k
        if(kk.gt.11) kk=k-11
        sync(16*k-15)=2.0*b11(kk) - 1.0
     enddo
  endif

  nfft=2*NSPS
  df=12000.0/nfft                            !3.125 Hz
  istep=nsps/NSTEP
  fac=1/32767.0
  do j=1,JZ                                  !Compute symbol spectra
     ia=(j-1)*istep
     ib=ia+nsps-1
     k=-1
     do i=ia,ib,2
        xx=iwave(i)
        yy=iwave(i+1)
        k=k+1
        c0(k)=fac*cmplx(xx,yy)
     enddo
     c0(k+1:nfft/2)=0.
     call four2a(c0,nfft,1,-1,0)              !r2c FFT
     do i=1,IZ
        s1(i,j)=real(c0(i))**2 + aimag(c0(i))**2
     enddo
  enddo

  i0=nint(nfqso/df)
  call pctile(s1(i0-64:i0+192,1:JZ),129*JZ,40,base)
  s1=s1/base
  s1max=20.0

! Apply AGC
  do j=1,JZ
     x(j)=maxval(s1(i0-64:i0+192,j))
     if(x(j).gt.s1max) s1(i0-64:i0+192,j)=s1(i0-64:i0+192,j)*s1max/x(j)
  enddo

  dt4=nsps/(NSTEP*12000.0)
  j0=0.5/dt4

  sync_sig=0.
  ia=min(64,nint(ntol/df))
  do i=-ia,ia
     x=s1(i0+2+i,:)-s1(i0+i,:)
     do lag=-15,15
! Make this simpler: just add the 22 nonzero values?
        do n=1,4*85
           j=n+lag+11
           if(j.ge.1 .and. j.le.JZ) sync_sig(i,lag)=sync_sig(i,lag) + sync(n)*x(j)
        enddo
     enddo
  enddo

  ijpk=maxloc(sync_sig)
  ii=ijpk(1)-65
  jj=ijpk(2)-16

! Use peakup() here?
  f0=nfqso + ii*df
  jdt=jj
  tsym=nsps/12000.0
  xdt=jdt*tsym/4.0

  snr1=maxval(sync_sig)/22.0
  
!  do i=-64,64
!     write(62,3062) nfqso+i*df,sync_sig(i,jj)
!3062 format(2f12.3)
!  enddo

!  do j=-15,15
!     write(63,3063) j,j*dt4,sync_sig(ii,j)
!3063 format(i5,2f12.3)
!  enddo

  return
end subroutine sync66a

