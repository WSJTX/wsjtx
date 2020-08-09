subroutine sync66(iwave,nmax,mode66,nsps,nfqso,ntol,xdt,f0,snr1)

  parameter (NSTEP=4)                      !Quarter-symbol steps
  integer*2 iwave(0:nmax-1)                !Raw data
  integer b11(11)                          !Barker 11 code
  integer ijpk(2)                          !Indices i and j at peak of sync_sig
  real, allocatable :: s1(:,:)             !Symbol spectra
  real, allocatable :: x(:)                !Work array; demoduated 2FSK sync signal
  real sync(4*85)                          !sync vector
  real sync_sig(-64:64,-15:15)
  complex, allocatable :: c0(:)            !Complex spectrum of symbol
  data b11/1,1,1,0,0,0,1,0,0,1,0/          !Barker 11 code
  data sync(1)/99.0/
  save sync

  nfft=2*nsps
  df=12000.0/nfft
  istep=nsps/NSTEP
  iz=5000.0/df                             !Uppermost frequency bin, at 5000 Hz
  txt=85.0*nsps/12000.0
  jz=(txt+1.0)*12000.0/istep               !Number of quarter-symbol steps
  if(nsps.ge.7680) jz=(txt+2.0)*12000.0/istep   !For TR 60 s and higher
  allocate(s1(iz,jz))
  allocate(x(jz))
  allocate(c0(0:nsps))

  if(sync(1).eq.99.0) then
     sync=0.
     do k=1,22
        kk=k
        if(kk.gt.11) kk=k-11
        i=4*NSTEP*(k-1) + 1
        sync(i)=2.0*b11(kk) - 1.0
     enddo
  endif

  fac=1/32767.0
  do j=1,jz                     !Compute symbol spectra at quarter-symbol steps
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
     do i=1,iz
        s1(i,j)=real(c0(i))**2 + aimag(c0(i))**2
     enddo
  enddo

  i0=nint(nfqso/df)
  call pctile(s1(i0-64:i0+192,1:jz),129*jz,40,base)
  s1=s1/base
  s1max=20.0

! Apply AGC
  do j=1,jz
     x(j)=maxval(s1(i0-64:i0+192,j))
     if(x(j).gt.s1max) s1(i0-64:i0+192,j)=s1(i0-64:i0+192,j)*s1max/x(j)
  enddo

  sync_sig=0.
  ia=min(64,nint(ntol/df))
  dt4=nsps/(NSTEP*12000.0)                      !duration of 1/4 symbol
!  lag2=nint(0.5/dt4)
!  if(nsps.ge.7680) lag2=nint(1.0/dt4)
!  lag1=-lag2

  jadd=11
  if(nsps.ge.3600) jadd=7
  if(nsps.ge.7680) jadd=6
  if(nsps.ge.16000) jadd=3
  if(nsps.ge.41472) jadd=1
  
  do i=-ia,ia
     x=s1(i0+2*mode66+i,:)-s1(i0+i,:)              !Do the 2FSK demodulation
!     do lag=lag1,lag2
     do lag=-15,15
        do k=1,22
           n=4*NSTEP*(k-1) + 1
           j=n+lag+jadd
           if(j.ge.1 .and. j.le.jz) sync_sig(i,lag)=sync_sig(i,lag) + sync(n)*x(j)
        enddo
     enddo
  enddo

  ijpk=maxloc(sync_sig)
  ii=ijpk(1)-65
  jj=ijpk(2)-16

! Use peakup() here?
  f0=nfqso + ii*df
  jdt=jj
  
!  j0=0.5/dt4
!  if(nsps.ge.7680) j0=1.0/dt4

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
end subroutine sync66
