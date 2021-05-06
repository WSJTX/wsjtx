subroutine gen_q65_cwave(msg,ntxfreq,ntone_spacing,msgsent,cwave,nwave)

! Encodes a Q65 message to yield complex cwave() at fsample = 96000 Hz

  use packjt
  use q65_encoding
  parameter (NMAX=60*96000)
  character*22 msg
  character*22 msgsent          !Message as it will be received
  character*37 msg37
  real*8 t,dt,phi,f,f0,dfgen,dphi,twopi,tsym
  complex cwave(NMAX)
  integer codeword(65),itone(85)
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
  dt=1.d0/96000.d0
  f0=ntxfreq
  dfgen=ntone_spacing*12000.d0/7200.d0
  phi=0.d0
  dphi=twopi*dt*f0
  i=0
  nwave=85*7200*96000.d0/12000.d0
  t=0.d0
  j0=0
  do i=1,nwave
     t=t+dt
     j=t/tsym + 1
     if(j.gt.85) exit
     if(j.ne.j0) then
        f=f0 + itone(j)*dfgen
        dphi=twopi*dt*f
        j0=j
     endif
     phi=phi+dphi
     if(phi.gt.twopi) phi=phi-twopi
     xphi=phi
     cwave(i)=cmplx(cos(xphi),-sin(xphi))
  enddo

999  return
end subroutine gen_q65_cwave
