! hspec is production code. Substitute only its downstream decoder with a
! recording probe: verify the actual slice delivered, not decoder success.
module hspec_input_probe
  implicit none
  integer :: calls=0
  integer*2 :: received(7168)
end module

program test_hspec_committed_boundary
  use hspec_input_probe
  implicit none
  integer*2 :: samples(0:120*12000-1)
  real :: green(0:702), spectrum(0:63,0:702), pxmax, dbnogain
  real*8 :: coefficients(5)
  integer :: i, jh, k
  character*80 :: line

  samples=0
  coefficients=0
  green=0
  spectrum=0
  jh=0
  k=7168 ! Detector emits an end-exclusive count, not a last-sample index.
  call run_hspec(.true.)
  if(calls.ne.0) stop 1 ! Silence must not invoke the downstream decoder.

  ! Reset hspec's saved sequence position, then provide two nonzero blocks.
  k=512
  call run_hspec(.false.)
  do i=0,7167
     samples(i)=int(1+mod(i,30000),kind=2)
  enddo
  samples(7168)=-12345 ! The first uncommitted sample must never be consumed.
  k=7168
  call run_hspec(.true.)
  if(calls.ne.1) stop 2
  if(any(received.ne.samples(0:7167))) then
     print *, 'FAIL: MSK input must be the completed range [k-7168,k).'
     print *, 'Expected first/last:',samples(0),samples(7167)
     print *, 'Observed first/last:',received(1),received(7168)
     stop 3
  endif
  print *, 'MSK committed-boundary test passed'

contains
  subroutine run_hspec(msk)
    logical, intent(in) :: msk
    logical*1 :: enabled
    enabled=msk
    call hspec(samples,k,0,15,1500,100,enabled,.false._1,coefficients, &
         0,'K1ABC       ','W9XYZ       ',.false._1,.false._1,'.',     &
         green,spectrum,jh,pxmax,dbnogain,line)
  end subroutine
end program

subroutine mskrtd(id2,nutc0,tsec,ntol,nrxfreq,ndepth,mycall,hiscall, &
     bshmsg,btrain,pcoeffs,bswl,datadir,line)
  use hspec_input_probe
  implicit none
  integer*2 :: id2(7168)
  integer :: nutc0, ntol, nrxfreq, ndepth
  real :: tsec
  real*8 :: pcoeffs(5)
  logical*1 :: bshmsg,btrain,bswl
  character*(*) :: mycall,hiscall,datadir,line
  received=id2
  calls=calls+1
  line=char(0)
end subroutine
