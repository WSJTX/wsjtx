subroutine read_qm(fname,iret)

  include 'njunk.f90'
  parameter(NMAX=60*96000,NFFT=32768)
  character*(*) fname
  character prog_id*24,mycall*12,mygrid*6
  real*8 fcenter
  integer nxtra(15)                        !For possible future additions
  integer*1 id1(2,NMAX)
  common/datcom/dd(2,5760000),ss(400,NFFT),savg(NFFT),                  &
       fcenter,nutc,fselected,mousedf,mousefqso,nagain,                 &
       ndepth,ndiskdat,ntx60,newdat,nn1,nn2,nfcal,nfshift,              &
       ntx30a,ntx30b   !...

  open(28,file=trim(fname),status='old',access='stream',err=900)
  read(28,end=910) prog_id,mycall,mygrid,fcenter,nutc,ntx30a,           &
       ntx30b,ndop00,ndop58,ia,ib,fac0,nxtra
  iret=3
  if(ib.eq.NMAX/2) iret=1
  if(ia.eq.NMAX/2+1) iret=2
  fac=1.0
  if(fac0.gt.0.0) fac=1.0/fac0
  id1=0
  read(28,end=910) id1(1:2,ia:ib)
  dd=0.
  dd(1:2,ia:ib)=fac*id1(1:2,ia:ib)   !Boost back to previous level
  go to 999

900 iret=-1; go to 999
910 iret=-2

999 close(28)

!  sq1=0.
!  sq2=0.
!  NH=NMAX/2
!  do i=1,NMAX
!     if(i.le.NH) sq1=sq1 + dd(1,i)*dd(1,i) + dd(2,i)*dd(2,i)
!     if(i.gt.NH) sq2=sq2 + dd(1,i)*dd(1,i) + dd(2,i)*dd(2,i)
!  enddo
!  print*,'B',sqrt(sq1/NMAX),sqrt(sq2/NMAX)

  return
end subroutine read_qm
