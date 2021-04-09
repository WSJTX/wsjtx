subroutine genqra64a(msg0,ichk,ntxfreq,mode64,itype,msgsent,iwave,nwave)

! Encodes a QRA64 message to yield itone(1:84)

  use packjt
  parameter (NMAX=2*60*11025)
  character*22 msg0
  character*22 message          !Message to be generated
  character*22 msgsent          !Message as it will be received
  integer itone(84)
  character*3 cok               !'   ' or 'OOO'
  real*8 t,dt,phi,f,f0,dfgen,dphi,twopi,samfac,tsym
  integer dgen(13)
  integer sent(63)
  integer*2 iwave(NMAX)
  integer icos7(0:6)
  data icos7/2,5,6,0,4,1,3/     !Defines a 7x7 Costas array
  data twopi/6.283185307179586476d0/
  save

  if(msg0(1:1).eq.'@') then
     read(msg0(2:5),*,end=1,err=1) nfreq
     go to 2
1    nfreq=1000
2    itone(1)=nfreq
     write(msgsent,1000) nfreq
1000 format(i5,' Hz')
  else
     message=msg0
     do i=1,22
        if(ichar(message(i:i)).eq.0) then
           message(i:)='                      '
           exit
        endif
     enddo

     do i=1,22                               !Strip leading blanks
        if(message(1:1).ne.' ') exit
        message=message(i+1:)
     enddo

     call chkmsg(message,cok,nspecial,flip)
     call packmsg(message,dgen,itype)    !Pack message into 72 bits
     call unpackmsg(dgen,msgsent)        !Unpack to get message sent
     if(ichk.ne.0) go to 999             !Return if checking only
     call qra64_enc(dgen,sent)           !Encode using QRA64

     nsync=10
     itone(1:7)=nsync*icos7              !Insert 7x7 Costas array in 3 places
     itone(8:39)=sent(1:32)
     itone(40:46)=nsync*icos7
     itone(47:77)=sent(33:63)
     itone(78:84)=nsync*icos7
  endif

! Set up necessary constants
  nsym=84
  tsym=6912.d0/12000.d0
  samfac=1.d0
  dt=1.d0/(samfac*11025.d0)
  f0=ntxfreq
  ndf=2**(mode64-1)
  dfgen=ndf*12000.d0/6912.d0
  phi=0.d0
  dphi=twopi*dt*f0
  i=0
  iz=84*6912*11025.d0/12000.d0
  t=0.d0
  j0=0
  do i=1,iz
     t=t+dt
     j=t/tsym
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

999 return
end subroutine genqra64a
