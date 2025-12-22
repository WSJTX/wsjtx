subroutine symspec2(c5,nz3,nsps8,nspsd,fsample,freq,drift,snrdb,schk,    &
     i1SoftSymbolsScrambled)

! Compute soft symbols from the final downsampled data

  complex c5(0:4096-1)
  complex z
  integer*1 i1SoftSymbolsScrambled(207)
  real aa(3)
  real ss2(0:8,85)
  real ss3(0:7,69)
  include 'jt9sync.f90'
  data scale/10.0/

  aa(1)=-1500.0/nsps8
  aa(2)=0.
  aa(3)=0.
  do i=0,8                                         !Loop over the 9 tones
     if(i.ge.1) call twkfreq(c5,c5,nz3,fsample,aa)
     m=0
     k=-1
     do j=1,85                                     !Loop over all symbols
        z=0.
        do n=1,nspsd                               !Sum over 16 samples
           k=k+1
           z=z+c5(k)
        enddo
        ss2(i,j)=real(z)**2 + aimag(z)**2        !Symbol speactra, data and sync
        if(i.ge.1 .and. isync(j).eq.0) then
           m=m+1
           ss3(i-1,m)=ss2(i,j)                   !Symbol speactra, data only
        endif
     enddo
  enddo

  call chkss2(ss2,freq,drift,schk)

  ss=0.
  sig=0.
  do j=1,69
     smax=0.
     do i=0,7
        smax=max(smax,ss3(i,j))
        ss=ss+ss3(i,j)
     enddo
     sig=sig+smax
     ss=ss-smax
  enddo
  ave=ss/(69*7)                           !Baseline
  call pctile(ss2,9*85,35,xmed)
  ss3=ss3/ave
  sig=sig/69.                             !Signal
  t=max(1.0,sig - 1.0)
  snrdb=db(t) - 61.3
     
  m0=3
  k=0
  do j=1,69
        smax=0.
        do i=0,7
           if(ss3(i,j).gt.smax) smax=ss3(i,j)
        enddo

     do m=m0-1,0,-1                   !Get bit-wise soft symbols
        if(m.eq.2) then
           r1=max(ss3(4,j),ss3(5,j),ss3(6,j),ss3(7,j))
           r0=max(ss3(0,j),ss3(1,j),ss3(2,j),ss3(3,j))
        else if(m.eq.1) then
           r1=max(ss3(2,j),ss3(3,j),ss3(4,j),ss3(5,j))
           r0=max(ss3(0,j),ss3(1,j),ss3(6,j),ss3(7,j))
        else
           r1=max(ss3(1,j),ss3(2,j),ss3(4,j),ss3(7,j))
           r0=max(ss3(0,j),ss3(3,j),ss3(5,j),ss3(6,j))
        endif

        k=k+1
        i4=nint(scale*(r1-r0))
        if(i4.lt.-127) i4=-127
        if(i4.gt.127) i4=127
!        i4=i4+128
!        if(i4.le.127) i1SoftSymbolsScrambled(k)=i4
!        if(i4.ge.128) i1SoftSymbolsScrambled(k)=i4-256
        i1SoftSymbolsScrambled(k)=i4
     enddo
  enddo

  return
end subroutine symspec2
