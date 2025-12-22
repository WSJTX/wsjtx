subroutine analytic(d,npts,nfft,c,pc,beq)

! Convert real data to analytic signal

  parameter (NFFTMAX=1024*1024)

  real d(npts)              ! passband signal
  real h(NFFTMAX/2)         ! real BPF magnitude
  real*8 pc(5),pclast(5)    ! static phase coeffs
  real*8 ac(5),aclast(5)    ! amp coeffs
  real*8 fp

  complex corr(NFFTMAX/2)  ! complex frequency-dependent correction 
  complex c(NFFTMAX)        ! analytic signal

  logical*1 beq            ! boolean static equalizer flag

  data nfft0/0/
  data aclast/0.0,0.0,0.0,0.0,0.0/
  data pclast/0.0,0.0,0.0,0.0,0.0/
!  data ac/1.0,0.05532,0.11438,0.12918,0.09274/ ! amp coeffs for TS2000
  data ac/1.0,0.0,0.0,0.0,0.0/ 

  save corr,nfft0,h,ac,aclast,pclast,pi,t,beta

  df=12000.0/nfft
  nh=nfft/2
  if( nfft.ne.nfft0 ) then
     pi=4.0*atan(1.0)
     t=1.0/2000.0
     beta=0.1
     do i=1,nh+1
        ff=(i-1)*df
        f=ff-1500.0
        h(i)=1.0
        if(abs(f).gt.(1-beta)/(2*t) .and. abs(f).le.(1+beta)/(2*t)) then
           h(i)=h(i)*0.5*(1+cos((pi*t/beta )*(abs(f)-(1-beta)/(2*t))))
        elseif( abs(f) .gt. (1+beta)/(2*t) ) then
           h(i)=0.0
        endif
     enddo
     nfft0=nfft
  endif

  if( any(aclast .ne. ac) .or. any(pclast .ne. pc) ) then
     aclast=ac
     pclast=pc
!     write(*,3001) pc
!3001 format('Phase coeffs:',5f12.6)
     do i=1,nh+1
        ff=(i-1)*df
        f=ff-1500.0
        fp=f/1000.0
        corr(i)=ac(1)+fp*(ac(2)+fp*(ac(3)+fp*(ac(4)+fp*ac(5))))
        pd=fp*fp*(pc(3)+fp*(pc(4)+fp*pc(5))) ! ignore 1st two terms
        corr(i)=corr(i)*cmplx(cos(pd),sin(pd))
     enddo
  endif

  fac=2.0/nfft
  c(1:npts)=fac*d(1:npts)
  c(npts+1:nfft)=0.
  call four2a(c,nfft,1,-1,1)               !Forward c2c FFT

  if( beq ) then
    c(1:nh+1)=h(1:nh+1)*corr(1:nh+1)*c(1:nh+1)
  else
    c(1:nh+1)=h(1:nh+1)*c(1:nh+1)
  endif

  c(1)=0.5*c(1)                            !Half of DC term
  c(nh+2:nfft)=0.                          !Zero the negative frequencies
  call four2a(c,nfft,1,1,1)                !Inverse c2c FFT
  return
end subroutine analytic
