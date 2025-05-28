subroutine decode_echo(id2,searching,rxcall)

! For EchoCall mode, recovers transmitted callsign from received echoes.
!     id2(NZ)  received echo data
!     rxcall     decoded message (callsign, usually)
! Time alignment of received data is assumed accurate, as EME delay is known.

  parameter (NSPS=4096,NZ=6*NSPS)
  integer*2 id2(NZ)      !Raw Rx data
  integer itone(6)         !Tone offsets corresponding to ransmitted callsign
  integer ipk(1)
  logical searching
  complex c0(0:NZ)         !Analytic data, 12000 Hz sample rate
  complex c1(0:NSPS-1)     !Data for a single echo character
  complex c2(0:NZ)         !Analytic data with shifted tone freqs
  character*6 rxcall       !The recovered callsign
  real s(0:NSPS-1)         !Spectrum for one received character
  real p(0:NSPS-1,6)       !Summed spectra for all six received characters
  real a(3)
  character*37 c
  common/echocom/nclearave,nsum,blue(4096),red(4096)
  equivalence (ipk1,ipk(1))
  data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/

! Retrieve params known at time of transmission and saved in id2
  call save_echo_params(nDop,nDopAudio,nfrit,f1,fspread,ndf,itone,id2,-1)

  df=12000.0/NSPS
  if(ndf.lt.10 .or. ndf.gt.30) return
  if(f1.eq.0.0) f1=1500.0
  i1=nint((f1 - 5*ndf)/df)
  i2=nint((f1 + 42*ndf)/df)
  if(i1.le.0 .or. i1.gt.4096 .or. i2.le.0 .or. i2.gt.4096) return
  nn=i2-i1+1

  if(nclearave.ne.0) p=0.
  nfft=NZ
  df1=12000.0/nfft
  fac=2.0/(32767.0*nfft)
  c0(0:NZ-1)=fac*id2(1:NZ)
  c0(0:14)=0.
  call four2a(c0,nfft,1,-1,1)           !Forward c2c FFT
  c0(nfft/2+1:nfft-1)=0.
  c0(0)=0.5*c0(0)
  call four2a(c0,nfft,1,1,1)            !Inverse c2c FFT; c0 is analytic sig

  rxcall='      '
  nskip=2*fspread/df

  do j=1,6
     ia=(j-1)*NSPS
     ib=ia+NSPS-1
     c1=c0(ia:ib)
     call four2a(c1,NSPS,1,-1,1)           !Forward c2c
     do i=0,NSPS/2
        s(i)=real(c1(i))**2 + aimag(c1(i))**2
     enddo
     if(.not.searching) then
        p(:,j)=p(:,j) + s                     !Sum the spectra for each character
        ipk=maxloc(p(i1:i2,j))
        k=nint(((ipk1+i1-1)*df - f1)/ndf) + 1
        if(k.ge.1 .and. k.le.37) rxcall(j:j)=c(k:k)    !SNR test here ???
     endif
  enddo

  do j=1,6                               !Move all tone frequencies to f1
     ia=(j-1)*NSPS
     ib=ia+NSPS-1
     a=0.
     a(1)=-itone(j)*ndf
     call twkfreq(c0(ia:ib),c2(ia:ib),NSPS,12000.0,a)
  enddo

! Return real(c2) as id2 ...
  id2(16:NZ)=32767.0*real(c2(15:NZ-1))
  
  return
end subroutine decode_echo
