subroutine foxfilt(nslots,nfreq,width,wave)

  parameter (NN=79,ND=58,KK=87,NSPS=4*1920)
  parameter (NWAVE=NN*NSPS,NFFT=614400,NH=NFFT/2)
  real wave(NWAVE)
  real x(NFFT)
  complex cx(0:NH)
  equivalence (x,cx)

  x(1:NWAVE)=wave
  x(NWAVE+1:)=0.
  call four2a(cx,NFFT,1,-1,0)              !r2c
  df=48000.0/NFFT
  fa=nfreq - 0.5*6.25
  fb=nfreq + 7.5*6.25 + (nslots-1)*60.0
  ia2=nint(fa/df)
  ib1=nint(fb/df)
  ia1=nint(ia2-width/df)
  ib2=nint(ib1+width/df)
  pi=4.0*atan(1.0)
  do i=ia1,ia2
     fil=(1.0 + cos(pi*df*(i-ia2)/width))/2.0
     cx(i)=fil*cx(i)
  enddo
  do i=ib1,ib2
     fil=(1.0 + cos(pi*df*(i-ib1)/width))/2.0
     cx(i)=fil*cx(i)
  enddo
  cx(0:ia1-1)=0.
  cx(ib2+1:)=0.

  call four2a(cx,nfft,1,1,-1)                  !c2r
  wave=x(1:NWAVE)/nfft

  return
end subroutine foxfilt
