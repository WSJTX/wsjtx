subroutine save_qm(fname,revision,mycall,mygrid,dd,ntx30a,ntx30b,fcenter,  &
     nutc,ndop00,ndop58)

  parameter(NMAX=60*96000)
  character*120 fname
  character*22 revision
  character*12 mycall
  character*6 mygrid
  real*4 dd(2,NMAX)
  real*8 fcenter
  integer nxtra(15)                        !For possible future additions
  integer*1,allocatable :: id1(:,:)

  ia=1
  ib=NMAX
  if(ntx30a.gt.5) ia=NMAX/2+1
  if(ntx30b.gt.5) ib=NMAX/2
  
  sq=0.
  do i=ia,ib
     x=dd(1,i)
     y=dd(2,i)
     sq=sq + x*x + y*y
  enddo
  nsum=2*(ib-ia+1)
  rms=sqrt(sq/nsum)

  nbad=0
  dmax=0.
  fac0=10.0/rms
  allocate(id1(1:2,1:NMAX))
  
  do i=ia,ib
     x=fac0*dd(1,i)
     y=fac0*dd(2,i)
     ax=abs(x)
     ay=abs(y)
     dmax=max(dmax,ax,ay)
     if(ax.gt.127.0) then
        x=0.
        nbad=nbad+1
     endif
     if(ay.gt.127.0) then
        y=0.
        nbad=nbad+1
     endif
     id1(1,i)=nint(x)
     id1(2,i)=nint(y)
  enddo
  if(ia.gt.30*96000) id1(1:2,1:ia-1)=0
  if(ib.eq.30*96000) id1(1:2,ib+1:60*96000)=0

  open(29,file=trim(fname),status='unknown',access='stream')
  nxtra=0
  write(29) revision//'  ',mycall,mygrid,fcenter,nutc,ntx30a,ntx30b,  &
       ndop00,ndop58,ia,ib,fac0,nxtra  !Write header to disk
  write(29) id1(1:2,ia:ib)             !Write 8-bit data to disk
  close(29)
  deallocate(id1)

  return
end subroutine save_qm

