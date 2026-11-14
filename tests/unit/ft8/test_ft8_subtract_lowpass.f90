program test_ft8_subtract_lowpass
! subtractft8 against a direct double-precision evaluation of the same
! operation: cfilt(i)=sum(j) window(j)/sumw*camp(i+1+j), the edge correction,
! then dd(j)=dd(j)-2*real(cfilt(i)*cref(i)).  Cases cover both frame edges,
! the windows at the frame start and end, and frames cut off by either end of
! the buffer.
! NMAX, NFRAME and NFILT repeat the values in subtractft8.f90.
  use iso_fortran_env, only: int64,real64
  implicit none
  integer, parameter :: NMAX=15*12000,NFRAME=1920*79,NFILT=4000,H=NFILT/2
  real :: dd0(NMAX),dd(NMAX),xjunk
  real(real64) :: corr(NMAX),worst,sumw,window(-H:H),endc(H+1)
  complex :: cref(NFRAME)
  complex(real64) :: camp(NFRAME),cfilt(NFRAME)
  integer :: itone(79),i,j,k,nstart,case_id,nfail
  integer(int64) :: state
  real :: f0,dt,amp,noise

  nfail=0
  sumw=0
  do j=-H,H
    window(j)=cos(4*atan(1.0_real64)*j/NFILT)**2
    sumw=sumw+window(j)
  enddo
  do j=1,H+1
    endc(j)=1/(1-sum(window(j-1:H))/sumw)
  enddo
  do i=1,79
    itone(i)=modulo(3*i+i*i/7,8)
  enddo
  f0=1500.0
  state=20261112_int64

  do case_id=1,5
    select case(case_id)
    case(1); dt=0.5;  noise=50.0     ! frame inside the buffer
    case(2); dt=0.5;  noise=0.0      ! five impulses: both frame edges, both window edges, mid-frame
    case(3); dt=-0.5; noise=50.0     ! frame starts before the buffer
    case(4); dt=2.4;  noise=50.0     ! frame runs past the buffer
    case(5); dt=0.0;  noise=50.0     ! nstart=1
    end select
    nstart=int(dt*12000+1)
    call gen_ft8wave(itone,79,1920,2.0,12000.0,f0,cref,xjunk,1,NFRAME)
    dd0=0
    do j=1,NMAX
      state=modulo(48271_int64*state,2147483647_int64)
      dd0(j)=noise*(2*real(state)/2147483647.-1)
    enddo
    if(case_id.eq.2) then
      do k=1,5
        select case(k)
        case(1); i=1
        case(2); i=2+H
        case(3); i=NFRAME/2
        case(4); i=NFRAME-H
        case(5); i=NFRAME
        end select
        j=nstart-1+i
        if(j.ge.1 .and. j.le.NMAX) dd0(j)=dd0(j)+1000.0*(-1)**k
      enddo
    else
      amp=1000.0
      do i=1,NFRAME
        j=nstart-1+i
        if(j.ge.1 .and. j.le.NMAX) dd0(j)=dd0(j)+amp*real(cref(i))
      enddo
    endif

    camp=0
    do i=1,NFRAME
      j=nstart-1+i
      if(j.ge.1 .and. j.le.NMAX) camp(i)=dd0(j)*conjg(cmplx(cref(i),kind=real64))
    enddo
    do i=1,NFRAME
      cfilt(i)=0
      do j=max(-H,1-(i+1)),min(H,NFRAME-(i+1))
        cfilt(i)=cfilt(i)+window(j)*camp(i+1+j)
      enddo
      cfilt(i)=cfilt(i)/sumw
    enddo
    do j=1,H+1
      cfilt(j)=cfilt(j)*endc(j)
      cfilt(NFRAME+1-j)=cfilt(NFRAME+1-j)*endc(j)
    enddo
    corr=0
    do i=1,NFRAME
      j=nstart-1+i
      if(j.ge.1 .and. j.le.NMAX) corr(j)=-2*real(cfilt(i)*cmplx(cref(i),kind=real64),real64)
    enddo

    dd=dd0
    call subtractft8(dd,itone,f0,dt,.false.)
    worst=0
    do j=1,NMAX
      worst=max(worst,abs(real(dd(j),real64)-real(dd0(j),real64)-corr(j)))
    enddo
    print '(a,i2,a,es10.3,a,es10.3)','case',case_id,'  max |difference| ',worst, &
         '   max |correction| ',maxval(abs(corr))
    if(worst.gt.1.0e-5_real64*maxval(abs(corr))) then
      print *,'FAIL: case',case_id
      nfail=nfail+1
    endif
  enddo
  if(nfail.ne.0) error stop 'test_ft8_subtract_lowpass failed'
  print *,'test_ft8_subtract_lowpass passed'
end program
