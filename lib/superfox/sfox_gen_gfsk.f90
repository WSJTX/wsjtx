subroutine sfox_gen_gfsk(idat,f0,isync,itone,cdat)
 
  parameter (NSPS=1024)
  parameter (NDS=151)
  parameter (NN=127)             !NN = number of code symbols
  parameter (NS=24)              !NS = number of sync symbols
  parameter (NMAX=15*12000)
  parameter (NPTS=(NDS+2)*NSPS)  !# of samples in waveform at 12000 samples/sec
  parameter (BT=8)               !GFSK time-bandwidth product

  complex cdat(NMAX)
  complex w, wstep
  integer idat(NN)
  integer isync(NS)
  integer itone(NDS)
  real*8 dt,twopi,phi,dphi_peak
  real*8 dphi(0:NPTS-1)
  real  pulse(3*NSPS)
  logical first/.true./

  save first,twopi,dt,hmod,dphi_peak,pulse

  if(first) then
    twopi=8.d0*atan(1.0)
    fsample=12000.0
    dt=1.0/fsample
    hmod=1.0
    dphi_peak=twopi*hmod/real(NSPS)
    do i=1,3*NSPS 
      tt=(i-1.5*NSPS)/real(NSPS)
      pulse(i)=gfsk_pulse(BT,tt)
    enddo
    first=.false.
  endif
  wave=0.
 
! Create the itone sequence: data symbols and interspersed sync symbols
  j=1
  k=0
  do i=1,NDS
     if(j.le.NS .and. i.eq.isync(j)) then
        if(j.lt.NS) j=j+1       !Index for next sync symbol
        itone(i)=0              !Insert sync symbol at tone 0
     else
        k=k+1
        itone(i)=idat(k) + 1    !Symbol value 0 is transmitted at tone 1, etc.
     endif
  enddo

! Generate the SuperFox waveform.
  
  dphi=0.d0
  do j=1,NDS
    ib=(j-1)*NSPS
    ie=ib+3*NSPS-1
    dphi(ib:ie)=dphi(ib:ie)+dphi_peak*pulse(1:3*NSPS)*itone(j)
  enddo
  dphi(0:2*NSPS-1)=dphi(0:2*NSPS-1)+dphi_peak*itone(1)*pulse(NSPS+1:3*NSPS)
  dphi(NDS*NSPS:(NDS+2)*NSPS-1)=dphi(NDS*NSPS:(NDS+2)*NSPS-1)+dphi_peak*itone(NDS)*pulse(1:2*NSPS)

  phi=0.d0
  dphi=dphi+twopi*f0*dt
  k=0
  do j=1,NSPS*(NDS+2)-1
    k=k+1
    cdat(k)=cmplx(cos(phi),sin(phi))
    phi=phi+dphi(j)
  enddo
  
! Add raised cosine ramps at the beginning and end of the waveform.
! Since the modulator expects an integral number of symbols, dummy
! symbols are added to the beginning and end of the waveform to 
! hold the ramps. All but nramp of the samples in each dummy
! symbol will be zero.
 
  nramp=NSPS/BT
  cdat(1:NSPS-nramp)=cmplx(0.0,0.0)
  cdat(NSPS-nramp+1:NSPS)=cdat(NSPS-nramp+1:NSPS) *                   &
       (1.0-cos(twopi*(/(i,i=0,nramp-1)/)/(2.0*nramp)))/2.0
  k1=(NDS+1)*NSPS+1
  cdat(k1:k1+nramp-1)=cdat(k1:k1+nramp-1) *                           &
       (1.0+cos(twopi*(/(i,i=0,nramp-1)/)/(2.0*nramp)))/2.0
  cdat(k1+nramp:NPTS)=0.0

  return
end subroutine sfox_gen_gfsk
