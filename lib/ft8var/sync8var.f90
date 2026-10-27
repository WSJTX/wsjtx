subroutine sync8var(residual,nfa,nfb,syncmin,nfqso,candidate,ncand,jzb,jzt,ipass, &
     lqsothread,ncandthin,ndtcenter,progress_generation)

  use iso_fortran_env, only: int64
!$ use omp_lib, only: omp_get_thread_num
  use ft8_mtd_residual, only: mtd_coarse_snapshot,mtd_coarse_team_size
  use ft8_coarse_cache, only: coarse_cache_lookup,coarse_cache_store
  use ft8_coarse_sync, only: cooperative_first_pass
  use ft8_mod1, only : windowx,facx,icos7,lagcc,lagccbail,nfawide,nfbwide
  use decode_completion_module, only : write_decode_progress
  real, target, intent(in) :: residual(180000)
  include 'ft8_params.f90'
  complex cx(0:NH1)
  real, save :: s(NH1,NHSYM)
  !$omp threadprivate(s)
  real x(NFFT1),sync2d(NH1,jzb:jzt),red(NH1),candidate0(5,450),candidate(4,460),tall(30),freq,rcandthin,dtcenter
  real candidate_sync(450),baseline_sum
  real signal_a(NH1),signal_b(NH1),signal_c(NH1),signal_cq(NH1)
  real baseline_a(NH1),baseline_b(NH1),baseline_c(NH1),baseline_cq(NH1)
  integer(int64) :: cache_epoch,cache_generation
  integer :: cache_fields(7),worker,cooperative_workers
  logical :: snapshot_valid,cache_fill,cache_hit,cooperative_handled,cooperative_eligible
  real, allocatable :: baseline(:,:)
  integer jpeak(NH1),indx(NH1),ii(1)
  integer*8 sync_plan
  integer, parameter :: max_sync_stencil=16
  integer, intent(in) :: nfa,nfb,nfqso,jzb,jzt,ipass,ncandthin,ndtcenter,progress_generation
  logical(1) syncq(NH1,jzb:jzt),redcq(NH1),lcq,lcq2,lpass1,lpass2
  logical(1), intent(in) :: lqsothread
  equivalence (x,cx)

! Compute symbol spectra, stepping by NSTEP steps.  
  tstep=0.04 ! NSTEP/12000.0                         
  df=3.125 ! 12000.0/NFFT1 , Hz
  syncq=.false.; redcq=.false.; candidate(4,:)=0.
  ncand=0
  rcandthin=ncandthin/100.;
  dtcenter=ndtcenter/100.

  iaw=max(1,nint(nfawide/df))
  ibw=min(NH1-max_sync_stencil,nint(nfbwide/df))
  ia=max(1,nint(nfa/df),iaw)
  ib=min(NH1-max_sync_stencil,nint(nfb/df),ibw)
  worker=1
!$ worker=omp_get_thread_num()+1
  call mtd_coarse_snapshot(worker,residual,cache_epoch,cache_generation,snapshot_valid)
  cache_fields=[ipass,jzb,jzt,nfawide,nfbwide,merge(1,0,lagcc),merge(1,0,lagccbail)]
  cooperative_workers=mtd_coarse_team_size()
  ! Every team member must reach this first-pass collective before any worker-local
  ! early return, including empty-range workers; otherwise its barriers can deadlock.
  if(ipass.eq.1.and.cooperative_workers.gt.1) then
    cooperative_eligible=snapshot_valid.and.nfawide.le.nfbwide.and.iaw.le.ibw.and.jzb.le.jzt.and. &
         .not.(lagcc.and..not.lagccbail)
    call cooperative_first_pass(residual,jzb,jzt,cache_epoch,cache_generation,cache_fields, &
         red,jpeak,redcq,cooperative_eligible,cooperative_handled,progress_generation,cooperative_workers)
    if(cooperative_handled) then
      if(nfa.gt.nfb.or.ia.gt.ib) return
      goto 800
    endif
  endif
  if(nfa.gt.nfb.or.nfawide.gt.nfbwide.or.ia.gt.ib.or.iaw.gt.ibw.or.jzb.gt.jzt) return
  cache_fill=snapshot_valid.and..not.(lagcc.and..not.lagccbail)
  if(cache_fill) then
    call coarse_cache_lookup(cache_epoch,cache_generation,cache_fields,red,jpeak,redcq,cache_hit)
    if(cache_hit) goto 800
  endif

  if(ipass.eq.1 .or. ipass.eq.4 .or. ipass.eq.7) then
    do j=1,NHSYM
      ia=(j-1)*NSTEP + 1
      ib=ia+NSPS-1
      x(1:759)=0.
      if(j.ne.1) then; x(760:960)=residual(ia-201:ia-1)*windowx(200:0:-1); else; x(760:960)=0.; endif
      x(961:2880)=facx*residual(ia:ib); x(961)=x(961)*1.9; x(2880)=x(2880)*1.9
      if(j.ne.NHSYM) then; x(2881:3081)=residual(ib+1:ib+201)*windowx; else; x(2881:3081)=0.; endif
      x(3082:)=0.
      if(j.eq.1) call four2avar_get_plan(cx,NFFT1,1,-1,0,sync_plan)
      call sfftw_execute_dft_r2c(sync_plan,x,cx)
      do i=iaw,ibw+max_sync_stencil
        s(i,j)=SQRT(real(cx(i))**2 + aimag(cx(i))**2)
      enddo
      if(mod(j,16).eq.0) call write_decode_progress(progress_generation)
    enddo
  endif
  if(ipass.eq.2 .or. ipass.eq.5 .or. ipass.eq.8) then
    do j=1,NHSYM
      ia=(j-1)*NSTEP + 1
      ib=ia+NSPS-1
      x(1:759)=0.
      if(j.ne.1) then; x(760:960)=residual(ia-201:ia-1)*windowx(200:0:-1); else; x(760:960)=0.; endif
      x(961:2880)=facx*residual(ia:ib); x(961)=x(961)*1.9; x(2880)=x(2880)*1.9
      if(j.ne.NHSYM) then; x(2881:3081)=residual(ib+1:ib+201)*windowx; else; x(2881:3081)=0.; endif
      x(3082:)=0.
      if(j.eq.1) call four2avar_get_plan(cx,NFFT1,1,-1,0,sync_plan)
      call sfftw_execute_dft_r2c(sync_plan,x,cx)
      do i=iaw,ibw+max_sync_stencil
        s(i,j)=real(cx(i))**2 + aimag(cx(i))**2
      enddo
      if(mod(j,16).eq.0) call write_decode_progress(progress_generation)
    enddo
  endif
  if(ipass.eq.3 .or. ipass.eq.6 .or. ipass.eq.9) then
    do j=1,NHSYM
      ia=(j-1)*NSTEP + 1
      ib=ia+NSPS-1
      x(1:759)=0.
      if(j.ne.1) then; x(760:960)=residual(ia-201:ia-1)*windowx(200:0:-1); else; x(760:960)=0.; endif
      x(961:2880)=facx*residual(ia:ib); x(961)=x(961)*1.9; x(2880)=x(2880)*1.9
      if(j.ne.NHSYM) then; x(2881:3081)=residual(ib+1:ib+201)*windowx; else; x(2881:3081)=0.; endif
      x(3082:)=0.
      if(j.eq.1) call four2avar_get_plan(cx,NFFT1,1,-1,0,sync_plan)
      call sfftw_execute_dft_r2c(sync_plan,x,cx)
      do i=iaw,ibw+max_sync_stencil
        s(i,j)=abs(real(cx(i))) + abs(aimag(cx(i)))
      enddo
      if(mod(j,16).eq.0) call write_decode_progress(progress_generation)
    enddo
  endif

  ia=max(1,nint(nfa/df))
  ib=min(NH1-max_sync_stencil,nint(nfb/df))
  ia=max(ia,iaw)
  ib=min(ib,ibw)
  if(ia.gt.ib) return
  nssy=4 ! NSPS/NSTEP   ! # steps per symbol
  nssy36=144 ! nssy*36
  nssy72=288 ! nssy*72
  nfos=2 ! NFFT1/NSPS   ! # frequency bin oversampling factor
  jstrt=12.5 ! 0.5/tstep

  if(lagcc .and. .not.lagccbail) then
    nfos6=12 ! nfos*6
    allocate(baseline(iaw:ibw,NHSYM))
    do k=1,NHSYM
      do i=iaw,ibw
        baseline(i,k)=sum(s(i:i+nfos6:nfos,k))
      enddo
    enddo
    do j=jzb,jzt
      do i=iaw,ibw
        ta=0.; tb=0.; tc=0.; tall=0.
        do n=0,6
          k=j+jstrt+nssy*n
          if(k.gt.0) then
            ta=s(i+nfos*icos7(n),k)
            if(ta.gt.1e-9) then; tall(n+1)=ta*6.0/(baseline(i,k)-ta); else; tall(n+1)=0.; endif
          endif
          k36=k+nssy36
          if(k36.gt.0 .and. k36.le.NHSYM) then
            tb=s(i+nfos*icos7(n),k36)
            if(tb.gt.1e-9) then; tall(n+17)=tb*6.0/(baseline(i,k36)-tb); else; tall(n+17)=0.; endif
          endif
          k72=k+nssy72
          if(k72.le.NHSYM) then
            tc=s(i+nfos*icos7(n),k72)
            if(tc.gt.1e-9) then; tall(n+24)=tc*6.0/(baseline(i,k72)-tc); else; tall(n+24)=0.; endif
          endif
        enddo
        lcq=.false.
        if(ipass.gt.1) then
          do n=7,15
            k=j+jstrt+nssy*n
            if(k.gt.0) then
              if(n.lt.15) then
                tall(n+1)=s(i,k)*6.0/(baseline(i,k)-s(i,k))
              else
                tall(n+1)=s(i+2,k)*6.0/(baseline(i,k)-s(i+2,k))
              endif
            endif
          enddo
          sya=sum(tall(1:7)); sycq=sum(tall(8:16)); sybc=sum(tall(17:30))
          sy1=(sya+sycq+sybc)/30.; sy2=(sya+sybc)/21.; sync_abc=max(sy1,sy2)
          sy1=(sycq+sybc)/23.; sy2=(sybc)/14.; sync_bc=max(sy1,sy2); if(sy1.gt.sy2) lcq=.true.
        else
          sybc=sum(tall(17:30)); sync_abc=sum(tall(1:7))+sybc; sync_bc=sybc/14.; sync_abc=sync_abc/21.
        endif
        sync2d(i,j)=max(sync_abc,sync_bc); if(lcq) syncq(i,j)=.true.
      enddo
      if(mod(j-jzb+1,16).eq.0) call write_decode_progress(progress_generation)
    enddo
  else
!    nfos6=15 ! 16i spec bw -1
    nfos6=max_sync_stencil
    allocate(baseline(iaw:ibw,NHSYM))
    do k=1,NHSYM
!$omp simd private(baseline_sum,n)
      do i=iaw,ibw
        baseline_sum=0.0
        do n=0,16
          baseline_sum=baseline_sum+s(i+n,k)
        enddo
        baseline(i,k)=baseline_sum
      enddo
    enddo
    do j=jzb,jzt
      signal_a(iaw:ibw)=0.; signal_b(iaw:ibw)=0.
      signal_c(iaw:ibw)=0.; signal_cq(iaw:ibw)=0.
      baseline_a(iaw:ibw)=0.; baseline_b(iaw:ibw)=0.
      baseline_c(iaw:ibw)=0.; baseline_cq(iaw:ibw)=0.
      do n=0,6
        k=j+jstrt+nssy*n
        if(k.gt.0) then
          do i=iaw,ibw
            signal_a(i)=signal_a(i)+s(i+nfos*icos7(n),k)
            baseline_a(i)=baseline_a(i)+baseline(i,k)-s(i+nfos*icos7(n)+1,k)
          enddo
        endif
        k36=k+nssy36
        if(k36.gt.0 .and. k36.le.NHSYM) then
          do i=iaw,ibw
            signal_b(i)=signal_b(i)+s(i+nfos*icos7(n),k36)
            baseline_b(i)=baseline_b(i)+baseline(i,k36)-s(i+nfos*icos7(n)+1,k36)
          enddo
        endif
        k72=k+nssy72
        if(k72.le.NHSYM) then
          do i=iaw,ibw
            signal_c(i)=signal_c(i)+s(i+nfos*icos7(n),k72)
            baseline_c(i)=baseline_c(i)+baseline(i,k72)-s(i+nfos*icos7(n)+1,k72)
          enddo
        endif
      enddo
      do n=7,15
        k=j+jstrt+nssy*n
        if(k.ge.1) then
          if(n.lt.15) then
            do i=iaw,ibw
              signal_cq(i)=signal_cq(i)+s(i,k)
              baseline_cq(i)=baseline_cq(i)+baseline(i,k)-s(i,k+1)
            enddo
          else
            do i=iaw,ibw
              signal_cq(i)=signal_cq(i)+s(i+2,k)
              baseline_cq(i)=baseline_cq(i)+baseline(i,k)-s(i,k+3)
            enddo
          endif
        endif
      enddo
      do i=iaw,ibw
        ta=signal_a(i); tb=signal_b(i); tc=signal_c(i); tcq=signal_cq(i)
        t0a=baseline_a(i); t0b=baseline_b(i); t0c=baseline_c(i); t0cq=baseline_cq(i)
        t1=ta+tb+tc; t01=t0a+t0b+t0c; t2=t1+tcq; t02=t01+t0cq
        t01=(t01-t1*2)/42.0; if(t01.lt.1e-8) t01=1.0; t02=(t02-t2*2)/60.0; if(t02.lt.1e-8) t02=1.0 ! safe division
        sync01=t1/(7.0*t01); sync02=(t1/7.0 + tcq/9.0)/t02; syncf=max(sync01,sync02)
        lcq=.false.; if(sync02.gt.sync01) lcq=.true.
        t1=tb+tc; t01=t0b+t0c; t2=t1+tcq; t02=t01+t0cq
        t01=(t01-t1*2)/28.0; if(t01.lt.1e-8) t01=1.0; t02=(t02-t2*2)/46.0; if(t02.lt.1e-8) t02=1.0 ! safe division
        sync01=t1/(7.0*t01); sync02=(t1/7.0 + tcq/9.0)/t02; syncs=max(sync01,sync02)
        lcq2=.false.; if(sync02.gt.sync01) lcq2=.true.
        sync2d(i,j)=max(syncf,syncs)
        if(syncf.gt.syncs) then; if(lcq) syncq(i,j)=.true.; else; if(lcq2) syncq(i,j)=.true.; endif
      enddo
      if(mod(j-jzb+1,16).eq.0) call write_decode_progress(progress_generation)
    enddo
  endif
  deallocate(baseline)


  red=0.
  do i=iaw,ibw
    ii=maxloc(sync2d(i,jzb:jzt)) - 1 + jzb
    j0=ii(1)
    jpeak(i)=j0
    red(i)=sync2d(i,j0); if(syncq(i,j0)) redcq(i)=.true.
!     write(52,3052) i*df,red(i),db(red(i))
!3052 format(3f12.3)
  enddo

  iz=ibw-iaw+1
  call indexx(red(iaw:ibw),iz,indx)
  ibase=indx(max(1,nint(0.40*iz))) - 1 + iaw ! max is workaround to prevent indx getting out of bounds
  base=red(ibase)
  if(base.lt.1e-8) base=1.0 ! safe division
  red=red/base
  if(cache_fill) call coarse_cache_store(cache_epoch,cache_generation,cache_fields,iaw,ibw,red,jpeak,redcq)
800 continue

  candidate0=0.; k=0; iz=ib-ia+1; lpass1=.false.; lpass2=.false.
  if(rcandthin.lt.0.99) then
    if(ipass.eq.1 .or. ipass.eq.4 .or. ipass.eq.7) then; lpass1=.true.
    else if(ipass.eq.2 .or. ipass.eq.5 .or. ipass.eq.8) then; lpass2=.true.
    endif
  endif
  call indexx(red(ia:ib),iz,indx)
  do i=1,iz
    n=ia + indx(iz+1-i) - 1
    freq=n*df
    if(abs(freq-nfqso).gt.3.0) then
      if (red(n).lt.syncmin) cycle
    else
      if (red(n).lt.1.1) cycle
    endif
    if(jpeak(n).lt.-49 .or. jpeak(n).gt.76) cycle
    if(k.lt.450) then; k=k+1; else; exit; endif
! being sorted by sync
    candidate0(1,k)=freq
    candidate0(2,k)=(jpeak(n)-1)*tstep
    candidate0(3,k)=red(n)
    if(rcandthin.lt.0.99) then ! candidate thinning option
      if(lpass2) then
        candidate0(5,k)=candidate0(3,k)/(abs(candidate0(2,k)-dtcenter)+1.0)**2
      else
        candidate0(5,k)=candidate0(3,k)/(abs(candidate0(2,k)-dtcenter)+1.0)
      endif
    endif
    if(redcq(n)) candidate0(4,k)=2.
  enddo
  ncand=k

  fdif0=4.0;
  xdtdelta=0.0
! save sync only to the best of near-dupe freqs 
  do i=1,ncand
    if(i.ge.2) then
      do j=1,i-1
        fdiff=abs(candidate0(1,i)-candidate0(1,j))
        xdtdelta=abs(candidate0(2,i)-candidate0(2,j))
        if(fdiff.lt.fdif0 .and. abs(candidate0(1,i)-nfqso).gt.3.0) then
          if(xdtdelta.lt.0.1) then
            if(candidate0(3,i).ge.candidate0(3,j)) candidate0(3,j)=0.
            if(candidate0(3,i).lt.candidate0(3,j)) candidate0(3,i)=0.
          endif
        endif
      enddo
!        write(*,3001) i,candidate0(1,i-1),candidate0(1,i),candidate0(3,i-1),  &
!             candidate0(3,i)
!3001    format(i2,4f8.1)
    endif
  enddo

! Sort by sync
!  call indexx(candidate0(3,1:ncand),ncand,indx)
  if(rcandthin.gt.0.99) then; candidate_sync(1:ncand)=candidate0(3,1:ncand)
! sort by sync value with DT weight
  else; candidate_sync(1:ncand)=candidate0(5,1:ncand)
  endif
  call indexx(candidate_sync,ncand,indx)
! Sort by frequency 
!  call indexx(candidate0(1,1:ncand),ncand,indx)

  k=1; ncandfqso=0
!Put nfqso at top of list and apply lowest sync threshold for nfqso
  fprev=5004.
  do i=ncand,1,-1
    j=indx(i)
    if(abs(candidate0(1,j)-nfqso).le.3.0 .and. candidate0(3,j).ge.1.1 .and. abs(candidate0(1,j)-fprev).gt.3.0) then
      candidate(1,k)=candidate0(1,j); candidate(2,k)=candidate0(2,j)
      candidate(3,k)=candidate0(3,j); candidate(4,k)=candidate0(4,j)
      fprev=candidate0(1,j)
      k=k+1; ncandfqso=ncandfqso+1
    endif
  enddo

!put virtual candidates for FT8Svar decoder
  if(lqsothread) then
    candidate(1,k)=float(nfqso)
    candidate(2,k)=5.0 ! xdt
    candidate(3,k)=0.0 ! sync
    k=k+1; ncandfqso=ncandfqso+1
    candidate(1,k)=float(nfqso)
    candidate(2,k)=-5.0
    candidate(3,k)=0.0
    k=k+1; ncandfqso=ncandfqso+1
  endif

  do i=ncand,1,-1
    j=indx(i)
    if(abs(candidate0(1,j)-nfqso).gt.3.0) then; syncmin1=syncmin; else; syncmin1=1.1; endif
    if(candidate0(3,j) .ge. syncmin1) then
      candidate(1,k)=candidate0(1,j); candidate(2,k)=candidate0(2,j)
      candidate(3,k)=candidate0(3,j); candidate(4,k)=candidate0(4,j)
      k=k+1
      if(k.gt.460) exit 
    endif
  enddo
  ncand=k-1
  if(ncand-ncandfqso.gt.1 .and. rcandthin.lt.0.99) then
! applying decoding pass weight factor, derived from number of candidates in each pass
    if(lpass1) then; rcandthin=min(rcandthin*1.27,1.0)
    else if(lpass2) then
      if(rcandthin.gt.0.79) then; rcandthin=rcandthin**2
      else; rcandthin=rcandthin*0.79
      endif
    else; rcandthin=min(rcandthin*5.0,1.0) ! ipass 3,6,9
    endif
    ncand=ncandfqso+nint((ncand-ncandfqso)*rcandthin)
  endif

  return
end subroutine sync8var
