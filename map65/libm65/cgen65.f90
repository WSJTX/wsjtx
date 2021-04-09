subroutine cgen65(message,mode65,samfac,nsendingsh,msgsent,cwave,nwave)

! Encodes a JT65 message into a wavefile.  
! Executes in 17 ms on opti-745.

  use packjt

  parameter (NMAX=60*96000)     !Max length of wave file
  character*22 message          !Message to be generated
  character*22 msgsent          !Message as it will be received
  character*3 cok               !'   ' or 'OOO'
  real*8 t,dt,phi,f,f0,dfgen,dphi,twopi,samfac,tsymbol
  complex cwave(NMAX)           !Generated complex wave file
  integer dgen(12)
  integer sent(63)
  logical first
  integer nprc(126)
  real pr(126)
  data nprc/1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0,  &
            0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1,  &
            0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1,  &
            0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,  &
            1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1,  &
            0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1,  &
            1,1,1,1,1,1/
  data twopi/6.283185307179586476d0/,first/.true./
  save

  if(first) then
     do i=1,126
        pr(i)=2*nprc(i)-1
     enddo
     first=.false.
  endif

  call chkmsg(message,cok,nspecial,flip) !See if it's a shorthand
  if(nspecial.eq.0) then
     call packmsg(message,dgen,itype)    !Pack message into 72 bits
     nsendingsh=0
     if(iand(dgen(10),8).ne.0) nsendingsh=-1    !Plain text flag

     call rs_encode(dgen,sent)
     call interleave63(sent,1)           !Apply interleaving
     call graycode(sent,63,1)            !Apply Gray code
     nsym=126                            !Symbols per transmission
     tsymbol=4096.d0/11025.d0            !Time per symbol
  else
     nsendingsh=1                        !Flag for shorthand message
     nsym=32
     tsymbol=16384.d0/11025.d0
  endif

! Set up necessary constants
  dt=1.d0/(samfac*96000.d0)
  f0=118*11025.d0/1024
  dfgen=mode65*11025.d0/4096.d0
  t=0.d0
  phi=0.d0
  k=0
  j0=0
  ndata=nsym*96000.d0*samfac*tsymbol

  do i=1,ndata
     t=t+dt
     j=int(t/tsymbol) + 1                    !Symbol number, 1-126
     if(j.ne.j0) then
        f=f0
        if(nspecial.ne.0 .and. mod(j,2).eq.0) f=f0+10*nspecial*dfgen
        if(nspecial.eq.0 .and. flip*pr(j).lt.0.0) then
           k=k+1
           f=f0+(sent(k)+2)*dfgen
        endif
        dphi=twopi*dt*f
        j0=j
     endif
     phi=phi+dphi
     if(phi.gt.twopi) phi=phi-twopi
     xphi=phi
     cwave(i)=cmplx(cos(xphi),-sin(xphi))
  enddo

  cwave(ndata+1:)=0
  nwave=ndata + 48000
  call unpackmsg(dgen,msgsent)
  if(flip.lt.0.0) then
     do i=22,1,-1
        if(msgsent(i:i).ne.' ') goto 10
     enddo
10   msgsent=msgsent(1:i)//' OOO'
  endif

  if(nsendingsh.eq.1) then
     if(nspecial.eq.2) msgsent='RO'
     if(nspecial.eq.3) msgsent='RRR'
     if(nspecial.eq.4) msgsent='73'
  endif

  return
end subroutine cgen65
