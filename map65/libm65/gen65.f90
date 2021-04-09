subroutine gen65(message,mode65,samfac,nsendingsh,msgsent,iwave,nwave)

! Encodes a JT65 message into a wavefile.  
! Executes in 17 ms on opti-745.

  use packjt
  
  parameter (NMAX=2*60*11025)   !Max length of wave file
  character*22 message          !Message to be generated
  character*22 msgsent          !Message as it will be received
  character*3 cok               !'   ' or 'OOO'
  real*8 dt,phi,f,f0,dfgen,dphi,twopi,samfac
  integer*2 iwave(NMAX)         !Generated wave file
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

  call chkmsg(message,cok,nspecial,flip)
  if(nspecial.eq.0) then
     call packmsg(message,dgen,itype)    !Pack message into 72 bits
     nsendingsh=0
     if(iand(dgen(10),8).ne.0) nsendingsh=-1    !Plain text flag

     call rs_encode(dgen,sent)
     call interleave63(sent,1)           !Apply interleaving
     call graycode(sent,63,1)            !Apply Gray code
     nsym=126                            !Symbols per transmission
     nsps=4096
  else
     nsym=32
     nsps=16384
     nsendingsh=1                         !Flag for shorthand message
  endif
  if(mode65.eq.0) go to 900

! Set up necessary constants
  dt=1.d0/(samfac*11025.d0)
  f0=118*11025.d0/1024
  dfgen=mode65*11025.d0/4096.d0
  phi=0.d0
  dphi=twopi*dt*f0
  i=0
  k=0
  do j=1,nsym
     if(message(1:5).ne.'@TUNE') then
        f=f0
        if(nspecial.ne.0 .and. mod(j,2).eq.0) f=f0+10*nspecial*dfgen
        if(nspecial.eq.0 .and. flip*pr(j).lt.0.0) then
           k=k+1
           f=f0+(sent(k)+2)*dfgen
        endif
        dphi=twopi*dt*f
     endif
     do ii=1,nsps
        phi=phi+dphi
        if(phi.gt.twopi) phi=phi-twopi
        xphi=phi
        i=i+1
        iwave(2*i-1)=32767.0*cos(xphi)
        iwave(2*i)=32767.0*sin(xphi)
     enddo
  enddo

  iwave(2*nsym*nsps+1:)=0
  nwave=2*nsym*nsps + 5512
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

900 return
end subroutine gen65
