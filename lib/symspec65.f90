!subroutine symspec65(dd,npts,ss,nqsym,savg)
subroutine symspec65(dd,npts,nqsym,savg)

! Compute JT65 symbol spectra at quarter-symbol steps

  parameter (NFFT=8192)
  parameter (NSZ=3413)                         !NFFT*5000/12000
  parameter (MAXHSYM=322)
  parameter (MAXQSYM=552)
  real*8 hstep
  real*4 dd(npts)
!  real*4 ss(MAXHSYM,NSZ)
  real*4 ss(MAXQSYM,NSZ)
  real*4 savg(NSZ)
  real*4 x(NFFT)
  real*4 w(NFFT)
  complex c(0:NFFT/2)
  logical first
  common/refspec/dfref,ref(NSZ)
  equivalence (x,c)
  data first/.true./
  save /refspec/,first,w
  common/sync/ss

  hstep=2048.d0*12000.d0/11025.d0              !half-symbol = 2229.116 samples
  qstep=hstep/2.0              !quarter-symbol = 1114.558 samples
  nsps=nint(2*hstep)
  df=12000.0/NFFT
  nhsym=(npts-NFFT)/hstep
  nqsym=(npts-NFFT)/qstep
  savg=0.
  fac1=1.e-3

  if(first) then
! Compute the FFT window
!     width=0.25*nsps
     do i=1,NFFT
!        z=(i-NFFT/2)/width
        w(i)=1
        if(i.gt.4458) w(i)=0
!        w(i)=exp(-z*z)
      enddo
     first=.false.
  endif

  do j=1,nqsym
     i0=(j-1)*qstep
     x=fac1*w*dd(i0+1:i0+NFFT)
     call four2a(c,NFFT,1,-1,0)                !r2c forward FFT
     do i=1,NSZ
        s=real(c(i))**2 + aimag(c(i))**2
        ss(j,i)=s
        savg(i)=savg(i)+s
     enddo
  enddo
  savg=savg/nhsym

!  call flat65(ss,nhsym,MAXQSYM,NSZ,ref)  !Flatten the 2d spectrum, saving
  call flat65(ss,nqsym,MAXQSYM,NSZ,ref)  !Flatten the 2d spectrum, saving
  dfref=df                               ! the reference spectrum ref()
  savg=savg/ref
!  do j=1,nhsym
  do j=1,nqsym
     ss(j,1:NSZ)=ss(j,1:NSZ)/ref
  enddo

  return
end subroutine symspec65
