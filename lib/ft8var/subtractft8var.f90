subroutine subtractft8var(residual,itone,f0,dt,delta)

! Subtract an ft8 signal
!
! Measured signal  : dd(t)    = a(t)cos(2*pi*f0*t+theta(t))
! Reference signal : cref(t)  = exp( j*(2*pi*f0*t+phi(t)) )
! Complex amp      : cfilt(t) = LPF[ dd(t)*CONJG(cref(t)) ]
! Subtract         : dd(t)    = dd(t) - 2*REAL{cref*cfilt}

  use ft8_mod1, only : NFILT1,endcorr
  use ft8_mtd_residual, only : mtd_start_sample
! NMAX=15*12000,NFRAME=1920*79
  parameter (NMAX=180000,NFRAME=151680)
  complex cref(nframe),cfilt(nframe)
  complex*16 erot(0:NFILT1-1)
  real sumw
  real*8 twopi8
  logical first
  real, intent(inout) :: residual(NMAX)
  real, intent(out), optional :: delta(NFRAME)
  integer, intent(in) :: itone(79)
  real, intent(in) :: f0,dt
  data first/.true./
  save cref,cfilt,erot,sumw,first
  !$omp threadprivate(cref,cfilt,erot,sumw,first)

  if(first) then
     pi=4.0*atan(1.0)
! sumw must equal the one cwfilter uses for endcorr
     sumw=0.0
     do j=-NFILT1/2,NFILT1/2
        sumw=sumw+cos(pi*j/NFILT1)**2
     enddo
     twopi8=8.0d0*atan(1.0d0)
     do k=0,NFILT1-1
        erot(k)=dcmplx(cos(twopi8*k/NFILT1),sin(twopi8*k/NFILT1))
     enddo
     first=.false.
  endif

  nstart=mtd_start_sample(dt)
  if(present(delta)) delta=0.0
  call gen_ft8wavevar(itone,79,1920,2.0,12000.0,f0,cref,xjunk,1,NFRAME)
  call lowpass(cfilt,nframe)
  cfilt(1:NFILT1/2+1)=cfilt(1:NFILT1/2+1)*endcorr
  cfilt(nframe:nframe-NFILT1/2:-1)=cfilt(nframe:nframe-NFILT1/2:-1)*endcorr
  do i=1,nframe
     j=nstart+i-1
     if(j.ge.1 .and. j.le.NMAX) then
        correction=-2*REAL(cfilt(i)*cref(i))
        residual(j)=residual(j)+correction
        if(present(delta)) delta(i)=correction
     endif
  enddo
  return

contains

  complex function cin(k)
! The signal times the conjugate reference, zero outside the frame and the buffer.
    integer k,id
    id=nstart-1+k
    if(k.ge.1 .and. k.le.nframe .and. id.ge.1 .and. id.le.NMAX) then
       cin=residual(id)*conjg(cref(k))
    else
       cin=0.0
    endif
  end function cin

  subroutine lowpass(cout,n)
! cout(i) = sum(j=-NFILT1/2:NFILT1/2) window(j)/sumw * cin(i+1+j), with cin=0
! outside 1..n (the assumption endcorr is built on).  The window,
! cos(pi*j/NFILT1)**2 = (1+cos(2*pi*j/NFILT1))/2, is a constant plus one cosine,
! so the sum is sa/2 + (conjg(e)*sc + e*sb)/4, where e=erot(mod(i+1,NFILT1))
! and sa, sb, sc are the running sums of cin, cin*conjg(erot) and cin*erot over
! the window; each output slides them by one sample.  The sums are double
! precision so that adding and removing about 150000 samples does not drift.
! The window is centred on sample i+1, not i.  The tables are the same in every
! thread but are built per thread so that no thread depends on another's first call.
    integer n
    complex cout(n)
    complex*16 sa,sb,sc,t,u
    integer i,k,h
    h=NFILT1/2
    sa=0; sb=0; sc=0
    do k=1,min(n,2+h)                       !window for i=1 is k=2-h..2+h
       t=cin(k)
       sa=sa+t
       sb=sb+t*conjg(erot(modulo(k,NFILT1)))
       sc=sc+t*erot(modulo(k,NFILT1))
    enddo
    do i=1,n
       u=0.25d0*(conjg(erot(modulo(i+1,NFILT1)))*sc + erot(modulo(i+1,NFILT1))*sb)
       cout(i)=(0.5d0*sa+u)/sumw
       k=i+2+h                              !enters for the next sample
       if(k.le.n) then
          t=cin(k)
          sa=sa+t
          sb=sb+t*conjg(erot(modulo(k,NFILT1)))
          sc=sc+t*erot(modulo(k,NFILT1))
       endif
       k=i+1-h                              !leaves
       if(k.ge.1) then
          t=cin(k)
          sa=sa-t
          sb=sb-t*conjg(erot(modulo(k,NFILT1)))
          sc=sc-t*erot(modulo(k,NFILT1))
       endif
    enddo
  end subroutine lowpass

end subroutine subtractft8var
