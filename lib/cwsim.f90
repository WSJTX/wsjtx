program cwsim

! Generate simulated audio for a CW message sent repeatedly for 60 seconds

  use wavhdr
  parameter (NMAX=60*12000)
  type(hdr) h                            !Header for the .wav file
  integer*2 iwave(NMAX)                  !Generated waveform (no noise)
  integer icw(500)                       !Encoded CW message bits
  complex cspread(0:NMAX-1)              !Complex amplitude for Rayleigh fading
  complex cdat(0:NMAX-1)                 !Complex waveform
  real dat(NMAX)                         !Audio waveform
  real*4 xnoise(NMAX)                    !Generated random noise
  character*60 message
  character*12 arg

  nargs=iargc()
  if(nargs.ne.6) then
     print*,'Usage:   cwsim         "message"           freq  bw wpm fspread snr'
     print*,'Example: cwsim "CQ CQ CQ DE K1JT K1JT K1JT" 700 100  20   100   -10'
     go to 999
  endif

  call getarg(1,message)
  call getarg(2,arg)
  read(arg,*) ifreq                  !Audio frequency (Hz)
  call getarg(3,arg)
  read(arg,*) bw                     !Bandwidth (Hz)
  call getarg(4,arg)
  read(arg,*) wpm                    !CW speed, words per minute
  call getarg(5,arg)
  read(arg,*) fspread                !Doppler spread in Hz
  call getarg(6,arg)
  read(arg,*) snrdb                  !S/N in dB (2500 hz reference BW)

  rms=500.0
  bandwidth_ratio=2500.0/6000.0
  sig=sqrt(2*bandwidth_ratio)*10.0**(0.05*snrdb)
  twopi=8.0*atan(1.0)

  h=default_header(12000,NMAX)
  open(10,file='000000_0000.wav',access='stream',status='unknown')
  do i=1,NMAX                   !Generate gaussian noise
     xnoise(i)=gran()
  enddo

  itone=0
  call morse(message,icw,ncw)
  call cwsig(icw,ncw,ifreq,wpm,sig,cdat)
  nfft=NMAX

  if(fspread.ne.0) then                  !Apply specified Doppler spread
     nh=nfft/2
     df=12000.0/nfft
     cspread(0)=1.0
     cspread(nh)=0.
     b=6.0                       !Use truncated Lorenzian shape for fspread
     do i=1,nh
        f=i*df
        x=b*f/fspread
        z=0.
        a=0.
        if(x.lt.3.0) then                          !Cutoff beyond x=3
           a=sqrt(1.111/(1.0+x*x)-0.1)             !Lorentzian amplitude
           phi1=twopi*rran()                       !Random phase
           z=a*cmplx(cos(phi1),sin(phi1))
        endif
        cspread(i)=z
        z=0.
        if(x.lt.3.0) then                !Same thing for negative freqs
           phi2=twopi*rran()
           z=a*cmplx(cos(phi2),sin(phi2))
        endif
        cspread(nfft-i)=z
     enddo

     call four2a(cspread,nfft,1,1,1)             !Transform to time domain
     sum=0.
     do i=0,nfft-1
        p=real(cspread(i))**2 + aimag(cspread(i))**2
        sum=sum+p
     enddo
     avep=sum/nfft
     fac=sqrt(1.0/avep)
     cspread=fac*cspread                   !Normalize to constant avg power
     cdat=cspread*cdat                     !Apply Rayleigh fading
  endif

  dat=aimag(cdat) + xnoise
  
  cdat=dat
  call four2a(cdat,nfft,1,-1,1)                 !c2c to frequency domain
  ia=max(250/df,(ifreq-0.5*bw)/df)
  ib=ia+bw/df
  cdat(0:ia)=0.
  cdat(ib:)=0.
  call four2a(cdat,nfft,1,+1,1)                 !c2c to time domain
  fac=sqrt(5000/bw)/nfft
  dat=fac*real(cdat)
  
  iwave=nint(rms*dat)
  write(10) h,iwave
  close(10)

999 end program cwsim

subroutine cwsig(icw,ncw,ifreq,wpm,sig,cdat)

  parameter(NMAX=60*12000)
  integer icw(ncw)
  complex cdat(NMAX)
  complex z(NMAX)
  real x(NMAX)
  real y(NMAX)
  real*8 dt,twopi,phi,dphi,fsample,tdit,t

  nspd=nint(1.2*12000.0/wpm)
  fsample=12000.d0                   !Sample rate (Hz)
  dt=1.d0/fsample                    !Sample interval (s)
  tdit=nspd*dt
  twopi=8.d0*atan(1.d0)
  dphi=twopi*ifreq*dt
  phi=0.
  k=12000                            !Start audio at t = 1.0 s
  t=0.
  npts=59*12000
  x=0.
  do i=1,npts
     t=t+dt
     j=nint(t/tdit) + 1
     j=mod(j-1,ncw) + 1
     phi=phi + dphi
     if(phi.gt.twopi) phi=phi-twopi
     xphi=phi
     k=k+1
     x(k)=icw(j)
     z(k)=cmplx(cos(xphi),sin(xphi))
     if(t.ge.59.5) exit
  enddo

  nadd=0.004/dt
  call smo(x,npts,y,nadd)
  y=y/nadd
  cdat=sig*y*z

  return
end subroutine cwsig

subroutine morse(msg,idat,n)

! Convert ascii message to a Morse code bit string.
!    Dash = 3 dots
!    Space between dots, dashes = 1 dot
!    Space between letters = 3 dots
!    Space between words = 7 dots

  character*(*) msg
  integer idat(500)
  integer*1 ic(21,38)
  data ic/                                        &
     1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,20,  &
     1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,0,0,18,  &
     1,0,1,0,1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0,16,  &
     1,0,1,0,1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,14,  &
     1,0,1,0,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,12,  &
     1,0,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,1,1,0,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,12,  &
     1,1,1,0,1,1,1,0,1,0,1,0,1,0,0,0,0,0,0,0,14,  &
     1,1,1,0,1,1,1,0,1,1,1,0,1,0,1,0,0,0,0,0,16,  &
     1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,0,0,0,18,  &
     1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 6,  &
     1,1,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,1,1,0,1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,12,  &
     1,1,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0, 8,  &
     1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 2,  &
     1,0,1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,1,1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0, 8,  &
     1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 4,  &
     1,0,1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,14,  &
     1,1,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,0,1,1,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0, 8,  &
     1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 6,  &
     1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,12,  &
     1,0,1,1,1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,12,  &
     1,1,1,0,1,1,1,0,1,0,1,1,1,0,0,0,0,0,0,0,14,  &
     1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0, 8,  &
     1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 6,  &
     1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 4,  &
     1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0, 8,  &
     1,0,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,1,1,0,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,12,  &
     1,1,1,0,1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,14,  &
     1,1,1,0,1,1,1,0,1,0,1,0,0,0,0,0,0,0,0,0,12,  &
     1,1,1,0,1,0,1,0,1,1,1,0,1,0,0,0,0,0,0,0,14,  &
     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 2/     !Incremental word space
  save

  msglen=len(trim(msg))
  idat=0
  n=6
  do k=1,msglen
     jj=ichar(msg(k:k))
     if(jj.ge.97 .and. jj.le.122) jj=jj-32  !Convert lower to upper case
     if(jj.ge.48 .and. jj.le.57) j=jj-48    !Numbers
     if(jj.ge.65 .and. jj.le.90) j=jj-55    !Letters
     if(jj.eq.47) j=36                      !Slash (/)
     if(jj.eq.32) j=37                      !Word space
     j=j+1

! Insert this character
     nmax=ic(21,j)
     if (n + nmax + 4 .gt. size (idat)) exit
     do i=1,nmax
        n=n+1
        idat(n)=ic(i,j)
     enddo

! Insert character space of 2 dit lengths:
     n=n+1
     idat(n)=0
     n=n+1
     idat(n)=0
  enddo

! Insert word space at end of message
  do j=1,4
     n=n+1
     idat(n)=0
  enddo

  return
end subroutine morse
