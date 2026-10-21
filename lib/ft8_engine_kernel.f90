module ft8_engine_kernel
!$ use omp_lib
  use prog_args, only: temp_dir
  use streaming_emit, only: streaming_emit_enabled
  use, intrinsic :: iso_fortran_env, only: error_unit
  use timer_module, only: timer
  use packjt77, only: pack77_state
  use decoder_engine_types, only: ft8_signal_evidence
  use ft8_decode, only: c2fox,g2fox,nsnrfox,nfreqfox,n30fox,n30z,nfox,ft8_a8d
  use ft8_decodevar, only: ltry_a8
  use ft8_decode_ranges, only: max_ft8_decode_ranges, partition_ft8_decode_range
  use ft8_mtd_residual, only: mtd_prepare,mtd_finish,mtd_worker_residual,mtd_worker_spectrum
  use decoder_callbacks, only: decoder_callback_context,counting_ft8_decoder, &
       counting_ft8_decodervar,ft8_decoded,ft8_decodedvar
  use decode_completion_module, only: decode_completion_result,reset_decode_completion, &
       set_decode_completion,write_decode_progress
  use sfrx_engine, only: sfrx_decode
  use ft8_mod1, only : mycall12_0,         &
       mycall12_00,hiscall12_0,nmsg,odd,even,prepare_ft8_hint_history,nlasttx,          &
       begin_ft8_qso_attempt,suppress_ft8_qso_hints,mycalllen1,msgroot,msgrootlen,lapmyc,sumxdtt,avexdt,          &
       nfawide,nfbwide,mycall,hiscall,lhound,mybcall,hisbcall,lenabledxcsearch, &
       lwidedxcsearch,hisgrid4,lmultinst,dd8,nft8cycles,lskiptx1,ncandallthr,   &
       nincallthr,incall,msgincall,xdtincall,maskincallthr,ltxing,hisgrid,      &
       prepare_ft8_qso_history,begin_ft8_result_attempt,report_ft8_result_overflow
  include 'jt9com.f90'

  private
  public :: ft8_kernel_state,run_ft8_kernel,reset_ft8_kernel,release_ft8_input,params_block

  type :: ft8_kernel_state
     type(counting_ft8_decoder) :: my_ft8
     type(counting_ft8_decodervar) :: my_ft8var
     logical :: first=.true.,firstsd=.true.
     logical :: mtd_residual_ready=.false.
     logical(1) :: lhoundprev=.false.
     integer :: nutc=0,ndelay=0,ntr0=-1,nintcount=0,nwrap=0
     integer :: ndec41=0,ndec46=0,ndec47=0,ndec48=0,ndec49=0
  end type

  interface
     subroutine tone8(lmycallstd,lhiscallstd,knowledge)
       import pack77_state
       logical(1), intent(in) :: lmycallstd,lhiscallstd
       type(pack77_state), target, optional, intent(inout) :: knowledge
     end subroutine
     subroutine tone8myc(knowledge)
       import pack77_state
       type(pack77_state), target, optional, intent(inout) :: knowledge
     end subroutine
     subroutine cwfilter(first,knowledge,render_legacy)
       import pack77_state
       logical, intent(in) :: first
       type(pack77_state), target, optional, intent(inout) :: knowledge
       logical, optional, intent(in) :: render_legacy
     end subroutine
     subroutine partintft8(ndelay,nutc,render_legacy)
       integer :: ndelay,nutc
       logical, optional, intent(in) :: render_legacy
     end subroutine
     subroutine ft8apsetvar(lmycallstd,lhiscallstd,numthreads,knowledge)
       import pack77_state
       logical(1), intent(in) :: lmycallstd,lhiscallstd
       integer :: numthreads
       type(pack77_state), target, optional, intent(inout) :: knowledge
     end subroutine
     subroutine fillhashvar(numthreads,lfill,knowledge)
       import pack77_state
       integer, intent(in) :: numthreads
       logical, intent(in) :: lfill
       type(pack77_state), target, optional, intent(inout) :: knowledge
     end subroutine
  end interface

contains

  subroutine reset_ft8_kernel(state)
    use ft8_mod1, only: reset_ft8_mod1
    use ft8_a7, only: reset_ft8_a7
    type(ft8_kernel_state), intent(inout) :: state
    call state%my_ft8%reset()
    state%my_ft8%decoded=0
    state%my_ft8%first_result=.true.
    state%my_ft8%day_wrap=0
    state%my_ft8var%decodedvar=0
    state%my_ft8var%first_result=.true.
    state%my_ft8var%day_wrap=0
    state%my_ft8var%xdtt=0.
    state%first=.true.
    state%mtd_residual_ready=.false.
    state%firstsd=.true.
    state%lhoundprev=.false.
    state%nutc=0
    state%ndelay=0
    state%ntr0=-1
    state%nintcount=0
    state%nwrap=0
    state%ndec41=0
    state%ndec46=0
    state%ndec47=0
    state%ndec48=0
    state%ndec49=0
    c2fox=''
    g2fox=''
    nsnrfox=-99
    nfreqfox=-99
    n30fox=0
    n30z=0
    nfox=0
    ltry_a8=.true.
    call reset_ft8_mod1()
    call reset_ft8_a7()
    call reset_ft8apsetvar()
  end subroutine

  subroutine release_ft8_input(state)
    type(ft8_kernel_state), intent(inout) :: state
    call state%my_ft8%release_input()
    state%mtd_residual_ready=.false.
    state%ndec41=0
    state%ndec46=0
    state%ndec47=0
    state%ndec48=0
    state%ndec49=0
  end subroutine release_ft8_input

  subroutine run_ft8_kernel(state,knowledge,id2,params,completion,progress_generation,callback_context)
    type(ft8_kernel_state), intent(inout) :: state
    type(pack77_state), target, intent(inout) :: knowledge
    integer(c_short), intent(inout) :: id2(180000)
    type(params_block), intent(inout) :: params
    type(decode_completion_result), intent(out) :: completion
    integer, intent(in) :: progress_generation
    type(decoder_callback_context), intent(in) :: callback_context
    type(decoder_callback_context) :: context
    integer :: ft8_range_low(max_ft8_decode_ranges),ft8_range_high(max_ft8_decode_ranges)
    integer :: ft8_range_count,requested_threads,nthr,active_progress_generation
    integer :: ncontest,nfqso
    logical :: newdat,ex
    character(len=6) :: mygrid
    character(len=60) :: line

    associate(my_ft8=>state%my_ft8,my_ft8var=>state%my_ft8var,first=>state%first, &
         firstsd=>state%firstsd,lhoundprev=>state%lhoundprev,nutc=>state%nutc, &
         ndelay=>state%ndelay,ntr0=>state%ntr0,nintcount=>state%nintcount, &
         nwrap=>state%nwrap,ndec41=>state%ndec41,ndec46=>state%ndec46, &
         ndec47=>state%ndec47,ndec48=>state%ndec48,ndec49=>state%ndec49)
    call reset_decode_completion(completion)
    active_progress_generation=progress_generation
    context=callback_context
    ncontest=iand(params%nexp_decode,7)
    nsynced=0
    navg0=0
    my_ft8%decoded=0
    my_ft8var%decodedvar=0
    my_ft8var%xdtt=0.
    my_ft8%knowledge=>knowledge
    my_ft8var%knowledge=>knowledge
    my_ft8var%callback=>ft8_decodedvar

    if(.not.params%newdat .and. params%ntr.gt.ntr0) go to 800
    ntr0=params%ntr
    rms=sqrt(dot_product(real(id2),real(id2))/180000.0)
    if(rms.lt.0.5) go to 800

    mycall=transfer(params%mycall,mycall)
    mybcall=transfer(params%mybcall,mybcall)
    hiscall=transfer(params%hiscall,hiscall)
    hisbcall=transfer(params%hisbcall,hisbcall)
    mygrid=transfer(params%mygrid,mygrid)
    hisgrid=transfer(params%hisgrid,hisgrid)
    hisgrid4=hisgrid(1:4)
    ncandallthr=0
    if(params%nzhsym.eq.41 .or. params%lmultift8) ltry_a8=.true.

    if(context%render_legacy) then
       inquire(file='rx_messages.txt',exist=ex)
       if(ex) then
          if(params%nzhsym.eq.41) then
             open(39,file='rx_messages.txt',status='old')
             do i=1,9999
                read(39,'(a60)',end=5) line
                if(line(1:1).eq.' ' .or. line(1:1).eq.'-') exit
                write(*,'(a)') trim(line)
             enddo
5            close(39)
          endif
          go to 800
       endif
       nfail=0
10     if(params%nagain) then
          open(13,file=trim(temp_dir)//'/decoded.txt',status='unknown',position='append',iostat=ios13)
       else
          open(13,file=trim(temp_dir)//'/decoded.txt',status='unknown',iostat=ios13)
       endif
       if(ios13.ne.0) then
          nfail=nfail+1
          if(nfail.le.3) then
             call sleep_msec(10)
             go to 10
          endif
       endif
       context%ios13=ios13
    endif
    context%nutc=params%nutc
    context%nfqso=params%nfqso
    context%ncontest=ncontest
    context%bVHF=iand(params%nexp_decode,64).ne.0
    context%b_superfox=params%b_superfox
    context%mycall=mycall
    my_ft8%context=context
    my_ft8var%context=context

     if(ncontest.eq.6 .and. context%render_legacy) then
        ! Fox mode: initialize and open houndcallers.txt
        inquire(file=trim(temp_dir)//'/houndcallers.txt',exist=ex)
        if(.not.ex) then
           c2fox='            '
           g2fox='    '
           nsnrfox=-99
           nfreqfox=-99
           n30z=0
           nwrap=0
           nfox=0
        endif
        open(19,file=trim(temp_dir)//'/houndcallers.txt',status='unknown')
     endif

     if(ncontest.eq.7 .and. params%b_superfox .and. params%b_even_seq) then
        if(params%nzhsym.lt.50) go to 800
        ! Call the superFox decoder
        call sfrx_decode(knowledge,context%superfox_sink,context%sink_user, &
             params%yymmdd,params%nutc,params%nfqso,params%ntol,id2)
     else
        call timer('decft8  ',0)
        if(params%nfa.gt.params%nfb) then
           call timer('decft8  ',1)
           go to 800
        endif
        newdat=params%newdat
        if(params%emedelay.ne.0.0.and..not.params%lmultift8) then
           id2(1:156000)=id2(24001:180000)  ! Drop the first 2 seconds of data
           id2(156001:180000)=0
           my_ft8%context%waveform_input_offset_seconds=2.0
        endif

! ft8md below
        if(params%lmultift8 .and. params%nmode.eq.8) then
           ! Disk retries continue subtraction on this input; live attempts start with fresh audio.
           if(.not.(params%ndiskdat.and.params%nagain.and.state%mtd_residual_ready)) dd8=real(id2)
           if(params%lmodechanged) then
              avexdt=0.
              nintcount=3
           endif ! avexdt fast track in FT8 after mode change

           if(.not.params%nagain) ndelay=params%ndelay
           call begin_ft8_qso_attempt(.false.)
           if(ndelay.gt.0) then ! received incomplete interval
              call partintft8(ndelay,params%nutc,context%render_legacy)
              call suppress_ft8_qso_hints()
           endif
           ntrials=params%nranera
           if(params%nsecbandchanged.gt.0) then
              nsamplesdel=params%nsecbandchanged*12000
              if(params%nsecbandchanged.gt.14) then
                 dd8=0. ! protection
              else
                 dd8(1:nsamplesdel)=0.
              endif
           endif

           if(.not.params%nagain) nutc=params%nutc

           if(first) then
              call cwfilter(first,knowledge,context%render_legacy)
              first=.false.
           endif ! + ALLCALL to memory
           lenabledxcsearch=params%lenabledxcsearch
           lwidedxcsearch=params%lwidedxcsearch

           lmultinst=params%lmultinst
           lskiptx1=params%lskiptx1
           ltxing=params%ltxing

           mycalllen1=len_trim(mycall)+1
           msgroot=''
           msgroot=trim(mycall)//' '//trim(hiscall)//' '
           msgrootlen=len_trim(msgroot)
           lhound=params%lhound
           nft8cycles=params%nft8cycles
           forcedt=0.

           if((hiscall.ne.hiscall12_0 .and. hiscall.ne.'            ')          &
                .or. (mycall.ne.mycall12_0 .and. mycall.ne.'            ') .or. &
                (lhound.neqv.lhoundprev)) then
              if(hiscall.ne.'            ') then
                 call tone8(params%lmycallstd,params%lhiscallstd,knowledge)
                 hiscall12_0=hiscall
                 mycall12_0=mycall
              endif
              lhoundprev=lhound
           endif
           if(params%lmycallstd .and. mycall.ne.'            ' .and.      &
                mycall12_00.ne.mycall) then
              call tone8myc(knowledge)
              mycall12_00=mycall
           endif

           call begin_ft8_result_attempt()
           numcores=1
!$         numcores=omp_get_num_procs()
           nuserthr=params%nmt

           numthreads=1                                         ! fallback
           if(nuserthr.eq.0) then                               ! auto
              if(numcores.eq.1) then
                 numthreads=1
              else if(numcores.gt.1 .and. numcores.lt.5) then
                 numthreads=numcores-1
              else if(numcores.gt.4 .and. numcores.lt.9) then
                 numthreads=numcores-2
              else if(numcores.gt.8 .and. numcores.lt.16) then
                 numthreads=numcores-3
              else
                 numthreads=12
              endif
           else if(nuserthr.gt.0 .and. nuserthr.lt.13) then
              ! number of threads shall not exceed number of logical cores
              if(numcores.ge.nuserthr) then
                 numthreads=nuserthr
              else
                 numthreads=numcores
              endif
           endif

!$         call omp_set_dynamic(.false.)
!$         call omp_set_max_active_levels(omp_get_supported_active_levels())

           nfa=params%nfa
           nfb=params%nfb
           nfqso=params%nfqso
           nfawide=params%nfa
           nfbwide=params%nfb

           if(params%nagainfil) then
              if(nfqso.lt.nfa .or. nfqso.gt.nfb) then
                 ! The stream's stdout carries only JSON events.
                 if(context%render_legacy .and. streaming_emit_enabled()) then
                    write(error_unit,'(a)') 'nfqso is out of bandwidth'
                 else if(context%render_legacy) then
                    write(*,64) nutc,'nfqso is out of bandwidth','d'
64                  format(i6.6,2x,a25,16x,a1)
                 endif
                 go to 800
              endif
              nfa=max(nfa,nfqso-25) ! 50Hz bandwidth for decode via double click
              nfb=min(nfb,nfqso+25)
              numthreads=min(4,numthreads)
! To do: withdraw limitation when threads are in sync at main passes?
           endif

           call partition_ft8_decode_range(nfa,nfb,numthreads,ft8_range_low, &
                ft8_range_high,ft8_range_count)
           if(ft8_range_count.eq.0) then
              call timer('decft8  ',1)
              go to 800
           endif
           numthreads=ft8_range_count
           requested_threads=numthreads

           nsec=mod(nutc,100)
           nmsg=0
           if(firstsd) then
              odd%lstate=.false.
              even%lstate=.false.
              firstsd=.false.
           endif

           call prepare_ft8_hint_history(nsec)

           nlasttx=params%nlasttx
           lapmyc=params%lapmyc
           nFT8decd=0
           sumxdt=0.0
           if(params%nmode.eq.4) sumxdtt=0.0

           call prepare_ft8_qso_history(hiscall,mycall,.not.logical(params%nstophint), &
                nfqso.ge.nfa .and. nfqso.le.nfb,nsec.eq.0 .or. nsec.eq.30, &
                nsec.eq.15 .or. nsec.eq.45)

!$omp parallel num_threads(requested_threads) private(nthr) shared(numthreads)
!$omp single
           numthreads=1
!$         numthreads=omp_get_num_threads()
! Reduced teams cover wider frequency slices, so fixed per-worker candidate limits can lower crowded-band yield.
           call partition_ft8_decode_range(nfa,nfb,numthreads,ft8_range_low, &
                ft8_range_high,ft8_range_count)
           numthreads=ft8_range_count
           call mtd_prepare(dd8,numthreads)
           call fillhashvar(numthreads,.false.,knowledge)
           call ft8apsetvar(params%lmycallstd,params%lhiscallstd,numthreads,knowledge)
!$omp end single

           nthr=1
!$         nthr=omp_get_thread_num()+1
           call my_ft8var%decodevar(params%nQSOProgress,nfqso,                &
                params%nft8rxfsens,params%nftx,ft8_range_low(nthr),           &
                ft8_range_high(nthr),params%ncandthin,params%ndtcenter,nsec,  &
                params%napwid,params%lmycallstd,params%lhiscallstd,           &
                params%nstophint,nthr,numthreads,logical(params%nagainfil),   &
                params%lft8lowth,params%lft8subpass,params%lhideft8dupes,     &
                active_progress_generation,                                  &
                mtd_worker_residual(:,nthr),                                  &
                mtd_worker_spectrum(:,nthr))
!$omp end parallel

           call report_ft8_result_overflow()
           call write_decode_progress(active_progress_generation)
           call mtd_finish(dd8)
           state%mtd_residual_ready=.true.

           call write_decode_progress(active_progress_generation)
           call run_ft8_mtd_a8_decode()
           call write_decode_progress(active_progress_generation)

           do i=1,numthreads
              do m=1,nincallthr(i)
                 nindex=maskincallthr(i)+m
                 incall(30:2:-1)=incall(30-1:1:-1)
                 incall(1)%msg=msgincall(nindex)
                 incall(1)%xdt=xdtincall(nindex)
              enddo
           enddo

           if(nsec.eq.0 .or. nsec.eq.30) even(nmsg+1:130)%lstate=.false.
           if(nsec.eq.15 .or. nsec.eq.45) odd(nmsg+1:130)%lstate=.false.

           if(params%ndelay.eq.0) then
              call my_ft8var%timing_summary(nFT8decd,sumxdt)
!            if(params%lforcesync) then; nintcount=3 ! fast track after Sync
!            elseif(nintcount.gt.0) then; nintcount=nintcount-1
              if(nintcount.gt.0) then
                 nintcount=nintcount-1
              endif
!              if(params%lforcesync .and. nFT8decd.eq.0) then
              if(nFT8decd.eq.0) then
                 avexdt=forcedt
              else
                 if(nFT8decd.gt.2) then
                    if(nFT8decd.gt.5) then
                       avexdt=(avexdt+sumxdt/nFT8decd)/2
                    else if(nFT8decd.eq.5) then
                       avexdt=(1.1*avexdt+0.9*sumxdt/nFT8decd)/2
                    else if(nFT8decd.eq.4) then
                       avexdt=(1.25*avexdt+0.75*sumxdt/nFT8decd)/2
                    else if(nFT8decd.eq.3) then
                       avexdt=(1.35*avexdt+0.65*sumxdt/nFT8decd)/2
                    endif
                 else if(nFT8decd.gt.0) then
                    if(nFT8decd.eq.2) then
                       avexdt=(1.5*avexdt+0.5*sumxdt/nFT8decd)/2
                    else if(nFT8decd.eq.1) then
                       avexdt=(1.75*avexdt+0.25*sumxdt)/2
                    endif
                 endif
              endif
           endif

           if(nFT8decd.gt.10 .and. nintcount.eq.1) avexdt=sumxdt/nFT8decd ! fast track after Sync or mode change on crowded bands
           call fillhashvar(numthreads,.true.,knowledge)
           call write_decode_progress(active_progress_generation)
           ncandall=sum(ncandallthr(1:numthreads))
           if(nFT8decd.eq.0) avexdt=0. ! reset to let correct sliding in decoder
           call timer('decft8  ',1)
        else        ! still FT8 but not ft8md
           call my_ft8%decode(ft8_decoded,id2,params%nQSOProgress,params%nfqso,  &
                params%nftx,newdat,params%nutc,params%nfa,params%nfb,            &
                params%nzhsym,params%ndepth,params%emedelay,ncontest,            &
                logical(params%nagain),logical(params%lft8apon),ltry_a8,         &
                logical(params%lapcqonly),params%napwid,mycall,hiscall,hisgrid,  &
                params%ndiskdat)
           call timer('decft8  ',1)
        endif       ! end of 'still FT8 but not ft8md'
     endif         ! end of 'if not in SuperFox mode'

     j=0
     if(ncontest.eq.6) then
        ! Fox mode: save decoded Hound calls for possible selection by FoxOp
        n=params%nutc
        n30=(3600*(n/10000) + 60*mod((n/100),100) + mod(n,100))/30
        if(n30.lt.n30z) nwrap=nwrap+2880    !New UTC day, handle the wrap
        n30z=n30
        n30=n30+nwrap

        if(context%render_legacy) rewind 19
        if(nfox.eq.0) then
           if(context%render_legacy) endfile 19
           if(context%render_legacy) rewind 19
        else
           do i=1,nfox
              n=n30fox(i)
              nage=min(99,mod(n30-n+288000,2880))
              if(nage.le.4) then
                 j=j+1
                 c2fox(j)=c2fox(i)
                 g2fox(j)=g2fox(i)
                 nsnrfox(j)=nsnrfox(i)
                 nfreqfox(j)=nfreqfox(i)
                 n30fox(j)=n
                 ! nage=min(99,mod(n30-n+288000,2880))
                 if(len(trim(g2fox(j))).eq.4) then
                    call azdist(mygrid,g2fox(j)//'  ',0.d0,nAz,nEl,nDmiles, &
                         nDkm,nHotAz,nHotABetter)
                 else
                    nDkm=9999
                 endif
                 if(context%render_legacy) write(19,1004) c2fox(j),g2fox(j),nsnrfox(j),nfreqfox(j), &
                      nDkm,nage
1004             format(a12,1x,a4,i5,i6,i7,i3)
              endif
           enddo
           nfox=j
           if(context%render_legacy) flush(19)
        endif
     endif


800 ndecoded=my_ft8%decoded+my_ft8var%decodedvar
  if(params%lmultift8 .and. params%nmode.eq.8) then
     if(params%nzhsym.eq.41) ndec41=0
     if(params%nzhsym.eq.46) ndec46=ndecoded
     if(params%nzhsym.eq.47) ndec47=ndecoded
     if(params%nzhsym.eq.48) ndec48=ndecoded
     if(params%nzhsym.eq.49) ndec49=ndecoded
     if(params%nzhsym.eq.50) then
        ndecoded=ndec41+ndec46+ndec47+ndec48+ndec49+ndecoded
     endif
  elseif(params%nmode.eq.8 .and. params%nzhsym.eq.41) then
     ndec41=ndecoded
  elseif(params%nmode.eq.8 .and. params%nzhsym.eq.47) then
     ndec47=ndecoded
  elseif(params%nmode.eq.8 .and. params%nzhsym.eq.50) then
     ndecoded=ndec41+ndec47+ndecoded
  endif
  if(params%nmode.ne.8 .or. params%nzhsym.eq.50 .or. &
       (params%lmultift8 .and. params%nmode.eq.8 .and. params%nzhsym.gt.45) .or. &
       .not.params%ndiskdat) then !ft8md
     call set_decode_completion(completion,nsynced,ndecoded,navg0)
     call write_decode_progress(active_progress_generation)
  endif
  if(context%render_legacy) close(13)
  if(ncontest.eq.6 .and. context%render_legacy) close(19)
  end associate


  contains
  subroutine run_ft8_mtd_a8_decode()
    implicit none

    real f1,xdt,fbest,xsnr,plog,qual
    integer nsnr,iaptype
    character(len=6) dxgrid
    character(len=37) msg37
    type(ft8_signal_evidence) :: evidence

    if(.not.params%lft8apon) return
    if(ncontest.eq.6 .or. ncontest.eq.7) return
    if(len(trim(hiscall)).lt.3 .or. len(trim(hisgrid4)).lt.4) return
    if(.not.ltry_a8) return

    f1=nfqso
    dxgrid=hisgrid4
    call timer('ft8_a8d ',0)
    call ft8_a8d(dd8,mycall,hiscall,dxgrid,f1,xdt,fbest,xsnr,plog,msg37, &
         active_progress_generation,knowledge,evidence)
    call timer('ft8_a8d ',1)

    if(msg37(1:1).ne.' ') then
       if(associated(state%my_ft8var%callback)) then
          nsnr=nint(xsnr)
          iaptype=8
          qual=1.0
          if(plog.lt.-147.0) qual=0.16
          call state%my_ft8var%callback(nsnr,xdt,fbest,msg37,iaptype,qual,evidence)
       endif
    endif
  end subroutine run_ft8_mtd_a8_decode

  end subroutine run_ft8_kernel
end module ft8_engine_kernel
