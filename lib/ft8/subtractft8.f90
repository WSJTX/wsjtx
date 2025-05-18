subroutine subtractft8(dd0,itone,f0,dt,lrefinedt)

! Subtract an ft8 signal
!
! Measured signal  : dd(t)    = a(t)cos(2*pi*f0*t+theta(t))
! Reference signal : cref(t)  = exp( j*(2*pi*f0*t+phi(t)) )
! Complex amp      : cfilt(t) = LPF[ dd(t)*CONJG(cref(t)) ]
! Subtract         : dd(t)    = dd(t) - 2*REAL{cref*cfilt}

  parameter (NMAX=15*12000,NFRAME=1920*79)
  parameter (NFFT=NMAX,NFILT=4000)
  real dd(NMAX),dd0(NMAX)
  real window(-NFILT/2:NFILT/2)
  real x(NFFT+2)
  real endcorrection(NFILT/2+1)
  complex cx(0:NFFT/2)
  complex cref,camp,cfilt,cw,z
  integer itone(79)
  logical first,lrefinedt,ldt
  data first/.true./
  common/heap8/cref(NFRAME),camp(NMAX),cfilt(NMAX),cw(NMAX)
  equivalence (x,cx)
  save first,/heap8/,endcorrection

  if(first) then                         ! Create and normalize the filter
     pi=4.0*atan(1.0)
     fac=1.0/float(nfft)
     sumw=0.0
     do j=-NFILT/2,NFILT/2
        window(j)=cos(pi*j/NFILT)**2
        sumw=sumw+window(j)
     enddo
     cw=0.
     cw(1:NFILT+1)=window/sumw
     cw=cshift(cw,NFILT/2+1)
     call four2a(cw,nfft,1,-1,1)
     cw=cw*fac
     first=.false.
     do j=1,NFILT/2+1
       endcorrection(j)=1.0/(1.0-sum(window(j-1:NFILT/2))/sumw)
     enddo
  endif

! Generate complex reference waveform cref
  call gen_ft8wave(itone,79,1920,2.0,12000.0,f0,cref,xjunk,1,NFRAME)

  ldt=lrefinedt
  if(ldt) then                         !Are we refining DT ?
     sqa=sqf(-90)
     sqb=sqf(+90)
     sq0=sqf(0)
     call peakup(sqa,sq0,sqb,dx)
     if(abs(dx).gt.1.0) return         !No acceptable minimum: do not subtract
     i2=nint(90.0*dx)                  !Best estimate of idt
     ldt=.false.
     sq0=sqf(i2)                       !Do the subtraction with idt=i2
  else
     sq0=sqf(0)                        !Do the subtraction with idt=0
  endif
  dd0=dd                               !Return dd0 with this signal subtracted
!  write(44,3044) nint(f0),dt-0.5,1.e-8*sum(dd*dd)
!3044 format(i4,f7.2,f10.6)
  return

contains

  real function sqf(idt)         !Internal function: all variables accessible
    nstart=dt*12000+1 + idt
    camp=0.
    dd=dd0
    do i=1,nframe
       j=nstart-1+i 
       if(j.ge.1.and.j.le.NMAX) camp(i)=dd(j)*conjg(cref(i))
    enddo

    cfilt(1:nframe)=camp(1:nframe)
    cfilt(nframe+1:)=0.0
    call four2a(cfilt,nfft,1,-1,1)
    cfilt(1:nfft)=cfilt(1:nfft)*cw(1:nfft)
    call four2a(cfilt,nfft,1,1,1)
    cfilt(1:NFILT/2+1)=cfilt(1:NFILT/2+1)*endcorrection
    cfilt(nframe:nframe-NFILT/2:-1)=cfilt(nframe:nframe-NFILT/2:-1)*endcorrection
    x=0.
    do i=1,nframe
       j=nstart+i-1
       if(j.ge.1 .and. j.le.NMAX) then
          z=cfilt(i)*cref(i)
          dd(j)=dd(j)-2.0*real(z)      !Subtract the reconstructed signal
          x(i)=dd(j)
       endif
    enddo
    sqq=0.
    if(ldt) then
       call four2a(cx,NFFT,1,-1,0)                    !Forward FFT, r2c
       df=12000.0/NFFT
       ia=(f0-1.5*6.25)/df
       ib=(f0+8.5*6.25)/df
       do i=ia,ib
          sqq=sqq + real(cx(i))*real(cx(i)) + aimag(cx(i))*aimag(cx(i))
       enddo
    endif
    sqf=sqq
    return
  end function sqf

end subroutine subtractft8
