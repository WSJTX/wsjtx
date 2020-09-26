subroutine sync_qra65(iwave,nmax,mode65,nsps,nfqso,ntol,xdt,f0,snr1)

  parameter (NSTEP=4)                    !Quarter-symbol steps
  integer*2 iwave(0:nmax-1)              !Raw data
  integer isync(22)                      !Indices of sync symbols
  integer ijpk(2)                        !Indices i and j at peak of ccf
  real, allocatable :: s1(:,:)           !Symbol spectra, quarter-symbol steps
  real sync(85)                          !sync vector 
  real ccf(-64:64,-26:107)
  complex, allocatable :: c0(:)          !Complex spectrum of symbol
  data isync/1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85/
  data sync(1)/99.0/
  save sync

  nfft=2*nsps
  df=12000.0/nfft
  istep=nsps/NSTEP
  iz=5000.0/df                           !Uppermost frequency bin, at 5000 Hz
  txt=85.0*nsps/12000.0
  jz=(txt+1.0)*12000.0/istep             !Number of quarter-symbol steps
  if(nsps.ge.7680) jz=(txt+2.0)*12000.0/istep   !For TR 60 s and higher

  allocate(s1(iz,jz))
  allocate(c0(0:nfft-1))

  if(sync(1).eq.99.0) then
     sync=-22.0/63.0                     !Sync OFF  
     do k=1,22
        sync(isync(k))=1.0               !Sync ON
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
     c0(k+1:)=0.
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
     smax=maxval(s1(i0-64:i0+192,j))
     if(smax.gt.s1max) s1(i0-64:i0+192,j)=s1(i0-64:i0+192,j)*s1max/smax
  enddo

!  do i=1,iz
!     write(60,3060) i,i*df,sum(s1(i,1:jz))
!3060 format(i6,f10.3,e12.3)
!  enddo

  dt4=nsps/(NSTEP*12000.0)                      !1/4 of symbol duration
  j0=0.5/dt4
  if(nsps.ge.7680) j0=1.0/dt4
  
  ccf=0.
  ia=min(64,nint(ntol/df))
  lag1=-1.0/dt4
  lag2=4.0/dt4 + 0.9999

  do i=-ia,ia
     do lag=lag1,lag2
        do k=1,85
           n=NSTEP*(k-1) + 1
           j=n+lag+j0
           if(j.ge.1 .and. j.le.jz) then
              ccf(i,lag)=ccf(i,lag) + sync(k)*s1(i0+i,j)
           endif
        enddo
     enddo
  enddo

!  do i=-64,64
!     write(61,3061) i,ccf(i,jpk)
!3061 format(i5,e12.3)
!  enddo
!  do j=lag1,lag2
!     write(62,3061) j,ccf(ipk,j)
!  enddo

  ijpk=maxloc(ccf)
  ipk=ijpk(1)-65
!  jpk=ijpk(2)-16
  jpk=ijpk(2)-27
  f0=nfqso + ipk*df
  xdt=jpk*dt4
  snr1=maxval(ccf)/22.0
!  write(*,3100) ipk,jpk,xdt,f0,snr1
!3100 format(2i5,f7.2,2f10.2)

  return
end subroutine sync_qra65
