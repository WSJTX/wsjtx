module ft8_coarse_sync
  use iso_fortran_env, only: int64
!$ use omp_lib, only: omp_in_parallel,omp_get_num_threads,omp_get_thread_num
  use ft8_decode_ranges, only: max_ft8_decode_ranges
  implicit none
  private
  public :: cooperative_first_pass
  integer, parameter :: bins=1920,spectra=372,max_workers=max_ft8_decode_ranges
  real, save :: s(bins,spectra),shared_red(bins)
  logical(1), save :: shared_cq(bins)
  integer, save :: shared_peak(bins),keys(7,max_workers)
  integer(int64), save :: epochs(max_workers),generations(max_workers)
  logical, save :: accepted,valid(max_workers)
contains
  subroutine cooperative_first_pass(residual,jzb,jzt,epoch,generation,fields,red,peak,cq,eligible,handled, &
       progress_generation,expected_workers)
    use ft8_mod1, only: windowx,facx,icos7
    use decode_completion_module, only: write_decode_progress
    integer :: KK,ND,NS,NN,NSPS,NZ,NMAX,NFFT1,NH1,NSTEP,NHSYM,NDOWN
    real, intent(in) :: residual(180000)
    integer, intent(in) :: jzb,jzt,fields(7),progress_generation,expected_workers
    integer(int64), intent(in) :: epoch,generation
    real, intent(inout) :: red(bins)
    integer, intent(inout) :: peak(bins)
    logical(1), intent(inout) :: cq(bins)
    logical, intent(in) :: eligible
    logical, intent(out) :: handled
    include 'ft8_params.f90'
    integer, parameter :: tile_width=16
    integer :: tile,lo,hi,width,t
    real :: acc
    real :: tile_baseline(tile_width,NHSYM),best(tile_width),score,raw_den
    integer :: best_peak(tile_width)
    logical(1) :: best_cq(tile_width),score_cq
    complex cx(0:NH1)
    real x(NFFT1),signal_a(tile_width),signal_b(tile_width),signal_c(tile_width),signal_cq(tile_width)
    real baseline_a(tile_width),baseline_b(tile_width),baseline_c(tile_width),baseline_cq(tile_width)
    integer :: worker,participants,indx(NH1)
    integer :: i,j,k,n,ia,ib,iaw,ibw,k36,k72,nssy,nssy36,nssy72,nfos,nfos6,jstrt,iz,ibase
    integer(int64) :: sync_plan
    real :: ta,tb,tc,tcq,t0a,t0b,t0c,t0cq,t1,t2,t01,t02,sync01,sync02,syncf,syncs,base
    logical :: in_parallel,plan_ready
    logical(1) :: lcq,lcq2
    logical :: spectra_safe,window_safe,lane_safe
    integer, parameter :: max_sync_stencil=16
    equivalence(x,cx)
    handled=.false.
    in_parallel=.false.
!$  in_parallel=omp_in_parallel()
    if(.not.in_parallel) return
    participants=1
!$  participants=omp_get_num_threads()
    if(participants.ne.expected_workers) return
    if(participants.le.1 .or. participants.gt.max_workers) return
    worker=1
!$  worker=omp_get_thread_num()+1
    keys(:,worker)=fields; epochs(worker)=epoch; generations(worker)=generation
    valid(worker)=eligible
! Every worker joins, including workers with an empty local candidate range.
!$omp barrier
!$omp single
    accepted=all(valid(1:participants))
    do i=2,participants
      accepted=accepted.and.all(keys(:,i).eq.keys(:,1))
      accepted=accepted.and.epochs(i).eq.epochs(1).and.generations(i).eq.generations(1)
    enddo
!$omp end single

    if(.not.accepted) return
    iaw=max(1,nint(fields(4)/3.125))
    ibw=min(NH1-max_sync_stencil,nint(fields(5)/3.125))
    spectra_safe=.true.
    plan_ready=.false.
!$omp do schedule(static)
    do j=1,NHSYM
      ia=(j-1)*NSTEP + 1
      ib=ia+NSPS-1
      x(1:759)=0.
      if(j.ne.1) then; x(760:960)=residual(ia-201:ia-1)*windowx(200:0:-1); else; x(760:960)=0.; endif
      x(961:2880)=facx*residual(ia:ib); x(961)=x(961)*1.9; x(2880)=x(2880)*1.9
      if(j.ne.NHSYM) then; x(2881:3081)=residual(ib+1:ib+201)*windowx; else; x(2881:3081)=0.; endif
      x(3082:)=0.
      if(.not.plan_ready) then
        call four2avar_get_plan(cx,NFFT1,1,-1,0,sync_plan)
        plan_ready=.true.
      endif
      call sfftw_execute_dft_r2c(sync_plan,x,cx)
      window_safe=.true.
!$omp simd private(lane_safe) reduction(.and.:window_safe)
      do i=iaw,ibw+max_sync_stencil
        s(i,j)=SQRT(real(cx(i))**2 + aimag(cx(i))**2)
        lane_safe=s(i,j).lt.sqrt(huge(1.0))
        window_safe=merge(window_safe,.false.,lane_safe)
      enddo
!$omp end simd
      spectra_safe=spectra_safe.and.window_safe
      if(mod(j,16).eq.0) call write_decode_progress(progress_generation)
    enddo
!$omp end do nowait
    valid(worker)=spectra_safe
!$omp barrier

    nssy=4; nssy36=144; nssy72=288; nfos=2; jstrt=12.5
    nfos6=max_sync_stencil
!$omp single
    ! Non-finite or extreme spectra require MAXLOC's existing selection semantics.
    accepted=all(valid(1:participants))
    shared_red=0.; shared_cq=.false.; shared_peak=0
!$omp end single
    if(.not.accepted) return
!$omp do schedule(static)
    do tile=0,(ibw-iaw)/tile_width
      lo=iaw+tile*tile_width; hi=min(ibw,lo+tile_width-1); width=hi-lo+1
      do k=1,NHSYM
! Each SIMD lane retains the ascending seventeen-input addition order.
!$omp simd private(acc,n)
        do t=1,width
          acc=0.0
          do n=0,16
            acc=acc+s(lo+t-1+n,k)
          enddo
          tile_baseline(t,k)=acc
        enddo
      enddo
      best(1:width)=-huge(1.0)
      do j=jzb,jzt
        signal_a(1:width)=0.; signal_b(1:width)=0.
        signal_c(1:width)=0.; signal_cq(1:width)=0.
        baseline_a(1:width)=0.; baseline_b(1:width)=0.
        baseline_c(1:width)=0.; baseline_cq(1:width)=0.
      do n=0,6
        k=j+jstrt+nssy*n
        if(k.gt.0) then
          do t=1,width
            signal_a(t)=signal_a(t)+s(lo+t-1+nfos*icos7(n),k)
            baseline_a(t)=baseline_a(t)+tile_baseline(t,k)-s(lo+t-1+nfos*icos7(n)+1,k)
          enddo
        endif
        k36=k+nssy36
        if(k36.gt.0 .and. k36.le.NHSYM) then
          do t=1,width
            signal_b(t)=signal_b(t)+s(lo+t-1+nfos*icos7(n),k36)
            baseline_b(t)=baseline_b(t)+tile_baseline(t,k36)-s(lo+t-1+nfos*icos7(n)+1,k36)
          enddo
        endif
        k72=k+nssy72
        if(k72.le.NHSYM) then
          do t=1,width
            signal_c(t)=signal_c(t)+s(lo+t-1+nfos*icos7(n),k72)
            baseline_c(t)=baseline_c(t)+tile_baseline(t,k72)-s(lo+t-1+nfos*icos7(n)+1,k72)
          enddo
        endif
      enddo
      do n=7,15
        k=j+jstrt+nssy*n
        if(k.ge.1) then
          if(n.lt.15) then
            do t=1,width
              signal_cq(t)=signal_cq(t)+s(lo+t-1,k)
              baseline_cq(t)=baseline_cq(t)+tile_baseline(t,k)-s(lo+t-1,k+1)
            enddo
          else
            do t=1,width
              signal_cq(t)=signal_cq(t)+s(lo+t-1+2,k)
              baseline_cq(t)=baseline_cq(t)+tile_baseline(t,k)-s(lo+t-1,k+3)
            enddo
          endif
        endif
      enddo
      do t=1,width
        ta=signal_a(t); tb=signal_b(t); tc=signal_c(t); tcq=signal_cq(t)
        t0a=baseline_a(t); t0b=baseline_b(t); t0c=baseline_c(t); t0cq=baseline_cq(t)
        t1=ta+tb+tc; t01=t0a+t0b+t0c; t2=t1+tcq; t02=t01+t0cq
        raw_den=t01-t1*2
        ! Equivalent to t1/(7*(raw_den/42)) for valid background power.
        if(raw_den.lt.42.0*1e-8) then
          t01=1.0
          sync01=t1/7.0
        else
          sync01=(6.0*t1)/raw_den
        endif
        t02=(t02-t2*2)/60.0; if(t02.lt.1e-8) t02=1.0 ! safe division
        sync02=(t1/7.0 + tcq/9.0)/t02; syncf=max(sync01,sync02)
        lcq=.false.; if(sync02.gt.sync01) lcq=.true.
        t1=tb+tc; t01=t0b+t0c; t2=t1+tcq; t02=t01+t0cq
        raw_den=t01-t1*2
        ! Equivalent to t1/(7*(raw_den/28)) for valid background power.
        if(raw_den.lt.28.0*1e-8) then
          t01=1.0
          sync01=t1/7.0
        else
          sync01=(4.0*t1)/raw_den
        endif
        t02=(t02-t2*2)/46.0; if(t02.lt.1e-8) t02=1.0 ! safe division
        sync02=(t1/7.0 + tcq/9.0)/t02; syncs=max(sync01,sync02)
        lcq2=.false.; if(sync02.gt.sync01) lcq2=.true.
        score=max(syncf,syncs)
        score_cq=.false.
        if(syncf.gt.syncs) then; if(lcq) score_cq=.true.; else; if(lcq2) score_cq=.true.; endif
        if(j.eq.jzb.or.score.gt.best(t)) then
          best(t)=score; best_peak(t)=j; best_cq(t)=score_cq
        endif
      enddo

      enddo
      shared_red(lo:hi)=best(1:width)
      shared_peak(lo:hi)=best_peak(1:width)
      shared_cq(lo:hi)=best_cq(1:width)
    enddo
!$omp end do

!$omp single
  iz=ibw-iaw+1
  call indexx(shared_red(iaw:ibw),iz,indx)
  ibase=indx(max(1,nint(0.40*iz))) - 1 + iaw
  base=shared_red(ibase)
  if(base.lt.1e-8) base=1.0
  shared_red=shared_red/base
!$omp end single

    red=shared_red; peak=shared_peak; cq=shared_cq
!$omp barrier

    handled=.true.
  end subroutine
end module ft8_coarse_sync
