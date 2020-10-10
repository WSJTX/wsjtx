subroutine msk144sync(cdat,nframes,ntol,delf,navmask,npeaks,fc,fest,   &
     npklocs,nsuccess,xmax,c)

!$ use omp_lib

  parameter (NSPM=864)
  complex cdat(NSPM*nframes)
  complex cdat2(NSPM*nframes,8)
  complex c(NSPM)                    !Coherently averaged complex data
  complex cs(NSPM,8)
  complex cb(42)                     !Complex waveform for sync word 

  integer s8(8)
  integer iloc(1)
  integer npklocs(npeaks)
  integer navmask(nframes)                 ! defines which frames to average

  real cbi(42),cbq(42)
  real pkamps(npeaks)
  real xcc(0:NSPM-1)
  real xccs(0:NSPM-1,8)
  real xm(8)
  real bf(8)
  real pp(12)                        !Half-sine pulse shape
  logical first
  data first/.true./
  data s8/0,1,1,1,0,0,1,0/
  save first,cb,fs,pi,twopi,dt,s8,pp

  if(first) then
     pi=4.0*atan(1.0)
     twopi=8.0*atan(1.0)
     fs=12000.0
     dt=1.0/fs

     do i=1,12                       !Define half-sine pulse
       angle=(i-1)*pi/12.0
       pp(i)=sin(angle)
     enddo

! Define the sync word waveforms
     s8=2*s8-1  
     cbq(1:6)=pp(7:12)*s8(1)
     cbq(7:18)=pp*s8(3)
     cbq(19:30)=pp*s8(5)
     cbq(31:42)=pp*s8(7)
     cbi(1:12)=pp*s8(2)
     cbi(13:24)=pp*s8(4)
     cbi(25:36)=pp*s8(6)
     cbi(37:42)=pp(1:6)*s8(8)
     cb=cmplx(cbi,cbq)

     first=.false.
  endif

  nfreqs=2*nint(ntol/delf) + 1
  xm=0.0
  bf=0.0
  nthreads=1
!$ nthreads=min(4,int(OMP_GET_MAX_THREADS(),4))
  nstep=nfreqs/nthreads

!$OMP PARALLEL NUM_THREADS(nthreads) PRIVATE(id,if1,if2)
  id=1
!$ id=OMP_GET_THREAD_NUM() + 1            !Thread id = 1,2,...
  if1=-nint(ntol/delf) + (id-1)*nstep
  if2=if1+nstep-1
  if(id.eq.nthreads) if2=nint(ntol/delf)
  call msk144_freq_search(cdat,fc,if1,if2,delf,nframes,navmask,cb,    &
       cdat2(1,id),xm(id),bf(id),cs(1,id),xccs(1,id))
!$OMP END PARALLEL

  xmax=xm(1)
  fest=fc+bf(1)
  c=cs(1:NSPM,1)
  xcc=xccs(0:NSPM-1,1)
  if(nthreads.gt.1) then
     do i=2,nthreads
        if(xm(i).gt.xmax) then
           xmax=xm(i)
           fest=fc+bf(i)
           c=cs(1:NSPM,i)
           xcc=xccs(0:NSPM-1,i)
        endif
     enddo
  endif

! Find npeaks largest peaks
  do ipk=1,npeaks
     iloc=maxloc(xcc)
     ic2=iloc(1)
     npklocs(ipk)=ic2
     pkamps(ipk)=xcc(ic2-1)
     xcc(max(0,ic2-7):min(NSPM-1,ic2+7))=0.0
  enddo

  nsuccess=0
  if(xmax.ge.1.3) nsuccess=1

  return
end subroutine msk144sync
