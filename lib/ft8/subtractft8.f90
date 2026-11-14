subroutine subtractft8(dd0,itone,f0,dt,lrefinedt)

! Subtract an ft8 signal
!
! Measured signal  : dd(t)    = a(t)cos(2*pi*f0*t+theta(t))
! Reference signal : cref(t)  = exp( j*(2*pi*f0*t+phi(t)) )
! Complex amp      : cfilt(t) = LPF[ dd(t)*CONJG(cref(t)) ]
! Subtract         : dd(t)    = dd(t) - 2*REAL{cref*cfilt}

  parameter (NMAX=15*12000,NFRAME=1920*79)
  parameter (NFFT=NMAX,NFILT=4000)
  real dd0(NMAX)
  real, allocatable, save :: dd(:)
  real window(-NFILT/2:NFILT/2)
  real x(NFFT+2)
  real endcorrection(NFILT/2+1)
  real sumw
  complex*16 erot(0:NFILT-1)
  real*8 twopi
  complex cx(0:NFFT/2)
  complex cref,camp,cfilt,z
  integer itone(79)
  logical first,lrefinedt,ldt
  data first/.true./
  common/heap8/cref(NFRAME),camp(NFRAME),cfilt(NFRAME)
  equivalence (x,cx)
  save first,/heap8/,endcorrection,sumw,erot

  if(.not.allocated(dd)) allocate(dd(NMAX))

  if(first) then                         ! Create and normalize the filter
     pi=4.0*atan(1.0)
     sumw=0.0
     do j=-NFILT/2,NFILT/2
        window(j)=cos(pi*j/NFILT)**2
        sumw=sumw+window(j)
     enddo
     do j=1,NFILT/2+1
       endcorrection(j)=1.0/(1.0-sum(window(j-1:NFILT/2))/sumw)
     enddo
     twopi=8.0d0*atan(1.0d0)
     do k=0,NFILT-1
        erot(k)=dcmplx(cos(twopi*k/NFILT),sin(twopi*k/NFILT))
     enddo
     first=.false.
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
    dd=dd0
    do i=1,nframe
       j=nstart-1+i
       if(j.ge.1.and.j.le.NMAX) then
          camp(i)=dd(j)*conjg(cref(i))
       else
          camp(i)=0.
       endif
    enddo
    call lowpass(camp,cfilt,nframe)
    cfilt(1:NFILT/2+1)=cfilt(1:NFILT/2+1)*endcorrection
    cfilt(nframe:nframe-NFILT/2:-1)=cfilt(nframe:nframe-NFILT/2:-1)*endcorrection
    if(ldt) x=0.
    do i=1,nframe
       j=nstart+i-1
       if(j.ge.1 .and. j.le.NMAX) then
          z=cfilt(i)*cref(i)
          dd(j)=dd(j)-2.0*real(z)      !Subtract the reconstructed signal
          if(ldt) x(i)=dd(j)
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

  subroutine lowpass(cin,cout,n)
! cout(i) = sum(j=-NFILT/2:NFILT/2) window(j)/sumw * cin(i+1+j), with cin=0
! outside 1..n (the assumption endcorrection is built on).  The window,
! cos(pi*j/NFILT)**2 = (1+cos(2*pi*j/NFILT))/2, is a constant plus one cosine,
! so the sum is sa/2 + (conjg(e)*sc + e*sb)/4, where e=erot(mod(i+1,NFILT))
! and sa, sb, sc are the running sums of cin, cin*conjg(erot) and cin*erot over
! the window; each output slides them by one sample.  The sums are double
! precision so that adding and removing about 150000 samples does not drift.
! The window is centred on sample i+1, not i.
    integer n
    complex cin(n),cout(n)
    complex*16 sa,sb,sc,t,u
    integer i,k,h
    h=NFILT/2
    sa=0; sb=0; sc=0
    do k=1,min(n,2+h)                       !window for i=1 is k=2-h..2+h
       t=cin(k)
       sa=sa+t
       sb=sb+t*conjg(erot(modulo(k,NFILT)))
       sc=sc+t*erot(modulo(k,NFILT))
    enddo
    do i=1,n
       u=0.25d0*(conjg(erot(modulo(i+1,NFILT)))*sc + erot(modulo(i+1,NFILT))*sb)
       cout(i)=(0.5d0*sa+u)/sumw
       k=i+2+h                              !enters for the next sample
       if(k.le.n) then
          t=cin(k)
          sa=sa+t
          sb=sb+t*conjg(erot(modulo(k,NFILT)))
          sc=sc+t*erot(modulo(k,NFILT))
       endif
       k=i+1-h                              !leaves
       if(k.ge.1) then
          t=cin(k)
          sa=sa-t
          sb=sb-t*conjg(erot(modulo(k,NFILT)))
          sc=sc-t*erot(modulo(k,NFILT))
       endif
    enddo
  end subroutine lowpass

end subroutine subtractft8
