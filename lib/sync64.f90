subroutine sync64(c0,nf1,nf2,nfqso,ntol,mode64,emedelay,dtx,f0,jpk,sync,  &
     sync2,width)

  use timer_module, only: timer

  parameter (NMAX=60*12000)                  !Max size of raw data at 12000 Hz
  parameter (NSPS=3456)                      !Samples per symbol at 6000 Hz
  parameter (NSPC=7*NSPS)                    !Samples per Costas waveform
  real s1(0:NSPC-1)                          !Power spectrum of Costas 1
  real s2(0:NSPC-1)                          !Power spectrum of Costas 2
  real s3(0:NSPC-1)                          !Power spectrum of Costas 3
  real s0(0:NSPC-1)                          !Sum of s1+s2+s3
  real s0a(0:NSPC-1)                         !Best synchromized spectrum (saved)
  real s0b(0:NSPC-1)                         !tmp
  real a(5)                                  !Parameters of Lorentzian fit
  integer icos7(0:6)                         !Costas 7x7 tones
  integer ipk0(1)
  complex cc(0:NSPC-1)                       !Costas waveform
  complex c0(0:720000)                       !Complex spectrum of dd()
  complex c1(0:NSPC-1)                       !Complex spectrum of Costas 1
  complex c2(0:NSPC-1)                       !Complex spectrum of Costas 2
  complex c3(0:NSPC-1)                       !Complex spectrum of Costas 3
  data icos7/2,5,6,0,4,1,3/                  !Costas 7x7 tone pattern
  data mode64z/-1/
  save

  if(mode64.ne.mode64z) then
! Submode has changed, recompute the complex Costas waveform
     twopi=8.0*atan(1.0)
     dfgen=mode64*12000.0/6912.0
     k=-1
     phi=0.
     do j=0,6
        dphi=twopi*10.0*icos7(j)*dfgen/6000.0
        do i=1,NSPS
           phi=phi + dphi
           if(phi.gt.twopi) phi=phi-twopi
           k=k+1
           cc(k)=cmplx(cos(phi),sin(phi))
        enddo
     enddo
     mode64z=mode64
  endif

  nfft3=NSPC
  df3=6000.0/nfft3
  
  fa=max(nf1,nfqso-ntol)
  fb=min(nf2,nfqso+ntol)
  iaa=max(0,nint(fa/df3))
  ibb=min(NSPC-1,nint(fb/df3))

  maxtol=max(ntol,500)
  fa=max(nf1,nfqso-maxtol)
  fb=min(nf2,nfqso+maxtol)
  ia=max(0,nint(fa/df3))
  ib=min(NSPC-1,nint(fb/df3))
  id=0.1*(ib-ia)
  iz=ib-ia+1
  sync=-1.e30
  smaxall=0.
  jpk=0
  ja=0
  jb=(5.0+emedelay)*6000                 !Bigger range than necessary?
  jstep=100
  ipk=0
  kpk=0
  nadd=10*mode64
  if(mod(nadd,2).eq.0) nadd=nadd+1       !Make nadd odd
  nskip=max(49,nadd)

  do j1=ja,jb,jstep                      !Loop over DT
     call timer('sync64_1',0)
     j2=j1 + 39*NSPS
     j3=j1 + 77*NSPS
     c1=1.e-4*c0(j1:j1+NSPC-1) * conjg(cc)
     c2=1.e-4*c0(j2:j2+NSPC-1) * conjg(cc)
     c3=1.e-4*c0(j3:j3+NSPC-1) * conjg(cc)
     call four2a(c1,nfft3,1,-1,1)
     call four2a(c2,nfft3,1,-1,1)
     call four2a(c3,nfft3,1,-1,1)
     s1=0.
     s2=0.
     s3=0.
     s0b=0.
     do i=ia,ib
        freq=i*df3
        s1(i)=real(c1(i))**2 + aimag(c1(i))**2
        s2(i)=real(c2(i))**2 + aimag(c2(i))**2
        s3(i)=real(c3(i))**2 + aimag(c3(i))**2
     enddo
     call timer('sync64_1',1)

     call timer('sync64_2',0)
     s0(ia:ib)=s1(ia:ib) + s2(ia:ib) + s3(ia:ib)
     s0(:ia-1)=0.
     s0(ib+1:)=0.
     if(nadd.ge.3) then                    !Smooth the spectrum
        do ii=1,3
           s0b(ia:ib)=s0(ia:ib)
           call smo(s0b(ia:ib),iz,s0(ia:ib),nadd)
        enddo
     endif
     call averms(s0(ia+id:ib-id),iz-2*id,nskip,ave,rms)
     s=(maxval(s0(iaa:ibb))-ave)/rms
     ipk0=maxloc(s0(iaa:ibb))
     ip=ipk0(1) + iaa - 1
     if(s.gt.sync) then
        jpk=j1
        s0a=(s0-ave)/rms
        sync=s
        dtx=jpk/6000.0 - 1.0
        ipk=ip
        f0=ip*df3
     endif
     call timer('sync64_2',1)
  enddo  ! j1 (DT loop)

  s0a=s0a+2.0
!  write(17) ia,ib,s0a(ia:ib)                !Save data for red curve
!  close(17)

  nskip=50
  call lorentzian(s0a(ia+nskip:ib-nskip),iz-2*nskip,a)
  f0a=(a(3)+ia+49)*df3
  w1=df3*a(4)
  w2=2*nadd*df3
  width=w1
  if(w1.gt.1.2*w2) width=sqrt(w1**2 - w2**2)

  sq=0.
  do i=1,20
     j=ia+nskip+1
     k=ib-nskip-21+i
     sq=sq + (s0a(j)-a(1))**2 + (s0a(k)-a(1))**2
  enddo
  rms2=sqrt(sq/40.0)
  sync2=10.0*log10(a(2)/rms2)

  slimit=6.0
  rewind 17
  write(17,1110) 0.0,0.0
  rewind 17
  do i=2,iz-2*nskip-1,3
     x=i
     z=(x-a(3))/(0.5*a(4))
     yfit=a(1)
     if(abs(z).lt.3.0) then
        d=1.0 + z*z
        yfit=a(1) + a(2)*(1.0/d - 0.1)
     endif
     j=i+ia+49
     freq=j*df3
     ss=(s0a(j-1)+s0a(j)+s0a(j+1))/3.0
     if(ss.gt.slimit) write(17,1110) freq,ss
1110 format(3f10.3)
  enddo
  flush(17)
  close(17)

  return
end subroutine sync64
