subroutine gen_q65_wave(msg,ntxfreq,mode65,msgsent,iwave,nwave)

! Encodes a Q65 message to yield complex iwave() at fsample = 11025 Hz

  use packjt
  use q65_encoding
  parameter (NMAX=2*60*11025)
  character*22 msg
  character*22 msgsent          !Message as it will be received
  character*37 msg37
  real*8 t,dt,phi,f,f0,dfgen,dphi,twopi,tsym
  integer codeword(65),itone(85)
  integer*2 iwave(NMAX)
  integer icos7(0:6)
  data icos7/2,5,6,0,4,1,3/     !Defines a 7x7 Costas array
  data twopi/6.283185307179586476d0/
  save

  msgsent=msg
  msg37=''
  msg37(1:22)=msg
  call get_q65_tones(msg37,codeword,itone)

! Set up necessary constants
  nsym=85
  tsym=7200.d0/12000.d0
  dt=1.d0/11025.d0
  f0=ntxfreq
  ndf=2**(mode65-1)
  dfgen=ndf*12000.d0/7200.d0
  phi=0.d0
  dphi=twopi*dt*f0
  i=0
  iz=85*7200*11025.d0/12000.d0
  t=0.d0
  j0=0
  do i=1,iz
     t=t+dt
     j=t/tsym + 1.0
     if(j.ne.j0) then
        f=f0 + itone(j)*dfgen
        dphi=twopi*dt*f
        j0=j
     endif
     phi=phi+dphi
     if(phi.gt.twopi) phi=phi-twopi
     xphi=phi
     iwave(2*i-1)=32767.0*cos(xphi)
     iwave(2*i)=32767.0*sin(xphi)
  enddo
  nwave=2*iz

999  return
end subroutine gen_q65_wave
