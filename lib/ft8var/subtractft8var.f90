subroutine subtractft8var(residual,itone,f0,dt,delta)

! Subtract an ft8 signal
!
! Measured signal  : dd(t)    = a(t)cos(2*pi*f0*t+theta(t))
! Reference signal : cref(t)  = exp( j*(2*pi*f0*t+phi(t)) )
! Complex amp      : cfilt(t) = LPF[ dd(t)*CONJG(cref(t)) ]
! Subtract         : dd(t)    = dd(t) - 2*REAL{cref*cfilt}

  use ft8_mod1, only : cw,NFILT1,NFILT2,endcorr
  use ft8_mtd_residual, only : mtd_start_sample
! NMAX=15*12000,NFFT=15*12000,NFRAME=1920*79
  parameter (NFFT=180000,NMAX=180000,NFRAME=151680)
  complex cref(nframe),cfilt(nmax)
  real, intent(inout) :: residual(NMAX)
  real, intent(out), optional :: delta(NFRAME)
  integer, intent(in) :: itone(79)
  real, intent(in) :: f0,dt
  save cref,cfilt
  !$omp threadprivate(cref,cfilt)

  nstart=mtd_start_sample(dt)
  if(present(delta)) delta=0.0
  call gen_ft8wavevar(itone,79,1920,2.0,12000.0,f0,cref,xjunk,1,NFRAME)
  do i=1,nframe
    id=nstart-1+i 
    if(id.ge.1.and.id.le.NMAX) then
      cfilt(i)=residual(id)*conjg(cref(i))
    else
      cfilt(i)=0.0
    endif
  enddo
  cfilt(nframe+1:)=0.0
  call four2avar(cfilt,nfft,1,-1,1)
  cfilt(1:nfft)=cfilt(1:nfft)*cw(1:nfft)
  call four2avar(cfilt,nfft,1,1,1)
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
end subroutine subtractft8var
