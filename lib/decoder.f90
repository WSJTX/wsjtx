subroutine multimode_decoder_core(ss,id2,params,nfsample,completion,progress_generation)

!$ use omp_lib
  use prog_args
  use timer_module, only: timer
  use jt4_decode
  use jt65_decode
  use jt9_decode
  use ft8_decode
  use ft8_decode_ranges, only: max_ft8_decode_ranges, partition_ft8_decode_range
  use ft8_mtd_residual, only: mtd_prepare,mtd_finish,mtd_worker_residual, &
       mtd_worker_spectrum
  use ft8_decodevar
  use ft4_decode
  use fst4_decode
  use q65_decode
  use decoder_callbacks, only: decoder_callback_context,                     &
       counting_jt4_decoder, counting_jt65_decoder, counting_jt9_decoder,    &
       counting_ft8_decoder, counting_ft8_decodervar, counting_ft4_decoder,  &
       counting_fst4_decoder, counting_q65_decoder, ft8_decodedvar,           &
       ft8_decoded, ft4_decoded, fst4_decoded, q65_decoded, jt4_decoded,       &
       jt4_average, jt65_decoded, jt9_decoded
  use streaming_emit, only: streaming_emit_enabled,                       &
       streaming_emit_decode
  use decode_completion_module, only: decode_completion_result,          &
       reset_decode_completion, set_decode_completion, write_decode_progress

!ft8md added 3 uses below
  use ft8_mod1, only : ndecodes,allmessages,allsnrs,allfreq,mycall12_0,         &
       mycall12_00,hiscall12_0,nmsg,odd,even,oddcopy,evencopy,nlasttx,          &
       lqsomsgdcd,mycalllen1,msgroot,msgrootlen,lapmyc,sumxdtt,avexdt,          &
       nfawide,nfbwide,mycall,hiscall,lhound,mybcall,hisbcall,lenabledxcsearch, &
       lwidedxcsearch,hisgrid4,lmultinst,dd8,nft8cycles,lskiptx1,ncandallthr,   &
       nincallthr,incall,msgincall,xdtincall,maskincallthr,ltxing,hisgrid,      &
       lastrxmsg,lasthcall

  include 'jt9com.f90'

  interface
     subroutine wsjt_tsan_acquire_decoder_section_primary() bind(C)
     end subroutine wsjt_tsan_acquire_decoder_section_primary
     subroutine wsjt_tsan_release_decoder_section_primary() bind(C)
     end subroutine wsjt_tsan_release_decoder_section_primary
     subroutine wsjt_tsan_acquire_decoder_section_secondary() bind(C)
     end subroutine wsjt_tsan_acquire_decoder_section_secondary
     subroutine wsjt_tsan_release_decoder_section_secondary() bind(C)
     end subroutine wsjt_tsan_release_decoder_section_secondary
  end interface

  logical first,firstsd !ft8md
  logical(1) lhoundprev !ft8md
  integer nutc,ndelay
  integer :: ft8_range_low(max_ft8_decode_ranges)
  integer :: ft8_range_high(max_ft8_decode_ranges)
  integer :: ft8_range_count
  integer :: requested_threads
  integer :: nthr
  integer, intent(in) :: progress_generation
  integer :: active_progress_generation
  type(params_block) :: params
  type(decode_completion_result), intent(out) :: completion
  type(decoder_callback_context) :: callback_context
  data ndelay/0/
  data first/.true./ !ft8md
  data firstsd/.true./ !ft8md
  data lhoundprev/.false./ !ft8md
  real ss(184,NSMAX)
  logical baddata,newdat65,newdat9,single_decode,bVHF,bad0,newdat,ex
  logical lprinthash22
  integer*2 id2(NTMAX*12000)
  integer nqf(20)
  real*4 dd(NTMAX*12000)
  character(len=20) :: datetime
  !character(len=12) :: mycall, hiscall  !ft8md
  character(len=6) :: mygrid!, hisgrid   !ft8md
  character*60 line
  data ndec8/0/,ntr0/-1/
  save
  type(counting_jt4_decoder) :: my_jt4
  type(counting_jt65_decoder) :: my_jt65
  type(counting_jt9_decoder) :: my_jt9
  type(counting_ft8_decoder) :: my_ft8
  type(counting_ft8_decodervar) :: my_ft8var
  type(counting_ft4_decoder) :: my_ft4
  type(counting_fst4_decoder) :: my_fst4
  type(counting_q65_decoder) :: my_q65  

  call reset_decode_completion(completion)
  active_progress_generation=progress_generation

  my_jt4%decoded = 0
  my_jt65%decoded = 0
  my_jt9%decoded = 0
  my_ft8%decoded = 0
  my_ft8var%decodedvar = 0
  my_ft8var%callback => ft8_decodedvar   !Set once in serial init; avoids racing writes in decodevar
  my_ft4%decoded = 0
  my_fst4%decoded = 0
  my_q65%decoded = 0
  my_ft8var%xdtt=0.
  nsynced=0
  navg0=0

  if(.not.params%newdat .and. params%ntr.gt.ntr0) go to 800
  ntr0=params%ntr
  rms=sqrt(dot_product(float(id2(1:180000)),float(id2(1:180000)))/180000.0)
  if(rms.lt.0.5) go to 800

! Cast C character arrays to Fortran character strings
  datetime=transfer(params%datetime, datetime)
  mycall=transfer(params%mycall,mycall)
  mybcall=transfer(params%mybcall,mybcall)
  hiscall=transfer(params%hiscall,hiscall)
  hisbcall=transfer(params%hisbcall,hisbcall)
  mygrid=transfer(params%mygrid,mygrid)
  hisgrid=transfer(params%hisgrid,hisgrid)
  hisgrid4=hisgrid(1:4)

  ncandall=0           !ft8md
  ncandallthr=0        !ft8md
  
! FT8 defaults at start to enable a8 decoding
  if(params%nzhsym.eq.41 .or. params%lmultift8) ltry_a8=.true.

! For testing only: return Rx messages stored in a file as decodes
  inquire(file='rx_messages.txt',exist=ex)
  if(ex) then
     if(params%nzhsym.eq.41) then
        open(39,file='rx_messages.txt',status='old')
        do i=1,9999
           read(39,'(a60)',end=5) line
           if(line(1:1).eq.' ' .or. line(1:1).eq.'-') go to 800
           write(*,'(a)') trim(line)
        enddo
5       close(39)
     endif
     go to 800
  endif

  ncontest=iand(params%nexp_decode,7)
  single_decode=iand(params%nexp_decode,32).ne.0
  bVHF=iand(params%nexp_decode,64).ne.0
  if(mod(params%nranera,2).eq.0) ntrials=10**(params%nranera/2)
  if(mod(params%nranera,2).eq.1) ntrials=3*10**(params%nranera/2)
  if(params%nranera.eq.0) ntrials=0

  nfail=0
10 if (params%nagain) then
     open(13,file=trim(temp_dir)//'/decoded.txt',status='unknown',            &
          position='append',iostat=ios13)
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

  callback_context%nutc = params%nutc
  callback_context%nfqso = params%nfqso
  callback_context%ncontest = ncontest
  callback_context%ios13 = ios13
  callback_context%bVHF = bVHF
  callback_context%b_superfox = params%b_superfox
  callback_context%mycall = mycall
  my_jt4%context = callback_context
  my_jt65%context = callback_context
  my_jt9%context = callback_context
  my_ft8%context = callback_context
  my_ft8var%context = callback_context
  my_ft4%context = callback_context
  my_fst4%context = callback_context
  my_q65%context = callback_context

  if(params%nmode.eq.8) then
    ! We're in FT8 mode
     if(ncontest.eq.6) then            !Fox=6, Hound=7
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
        call sfrx_sub(params%yymmdd,params%nutc,params%nfqso,params%ntol,id2)
     else
        call timer('decft8  ',0)
        if(params%nfa.gt.params%nfb) then
           call timer('decft8  ',1)
           go to 800
        endif
        newdat=params%newdat
        if(params%emedelay.ne.0.0) then
           id2(1:156000)=id2(24001:180000)  ! Drop the first 2 seconds of data
           id2(156001:180000)=0
        endif

! ft8md below
        if(params%lmultift8 .and. params%nmode.eq.8) then
           if(params%lmodechanged) then
              avexdt=0.
              nintcount=3
           endif ! avexdt fast track in FT8 after mode change

           if(.not.params%nagain) ndelay=params%ndelay
           lqsomsgdcd=.false.
           if(ndelay.gt.0) then ! received incomplete interval
              call partintft8(ndelay,params%nutc)
              lqsomsgdcd=.true.
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
              call cwfilter(first) 
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
                 call tone8(params%lmycallstd,params%lhiscallstd)
                 hiscall12_0=hiscall
                 mycall12_0=mycall
              endif
              lhoundprev=lhound
           endif
           if(params%lmycallstd .and. mycall.ne.'            ' .and.      &
                mycall12_00.ne.mycall) then
              call tone8myc()
              mycall12_00=mycall
           endif

           ndecodes=0            !Initialize arrays for multi-threaded decoding
           allmessages=""
           allsnrs=0
           allfreq=0.
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
                 write(*,64) nutc,'nfqso is out of bandwidth','d'
64               format(i6.6,2x,a25,16x,a1)
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
           if(nsec.ne.0 .and. nsec.ne.15 .and. nsec.ne.30 .and. nsec.ne.45) then
! Reading simulated wav file
              odd%lstate=.false.
              even%lstate=.false.
              oddcopy%lstate=.false.
              evencopy%lstate=.false.  
           endif

           if(firstsd) then
              odd%lstate=.false.
              even%lstate=.false.
              firstsd=.false.
           endif

           if(nsec.eq.0 .or. nsec.eq.30) then
              evencopy%msg=even%msg
              evencopy%freq=even%freq
              evencopy%dt=even%dt
              evencopy%lstate=even%lstate
              even%lstate=.false.
           endif

           if(nsec.eq.15 .or. nsec.eq.45) then
              oddcopy%msg=odd%msg
              oddcopy%freq=odd%freq
              oddcopy%dt=odd%dt
              oddcopy%lstate=odd%lstate
              odd%lstate=.false.
           endif

           nlasttx=params%nlasttx
           lapmyc=params%lapmyc
           nFT8decd=0
           sumxdt=0.0
           if(params%nmode.eq.4) sumxdtt=0.0

           if(hiscall.eq.'') then
              lastrxmsg(1)%lstate=.false.
           else if(lastrxmsg(1)%lstate .and. lasthcall.ne.hiscall .and.        &
                index(lastrxmsg(1)%lastmsg,trim(hiscall)).le.0) then
              lastrxmsg(1)%lstate=.false.
           endif

!$omp parallel num_threads(requested_threads) private(nthr) shared(numthreads)
!$omp single
           numthreads=1
!$         numthreads=omp_get_num_threads()
! Reduced teams cover wider frequency slices, so fixed per-worker candidate limits can lower crowded-band yield.
           call partition_ft8_decode_range(nfa,nfb,numthreads,ft8_range_low, &
                ft8_range_high,ft8_range_count)
           numthreads=ft8_range_count
           call mtd_prepare(dd8,numthreads)
           call fillhashvar(numthreads,.false.)
           call ft8apsetvar(params%lmycallstd,params%lhiscallstd,numthreads)
!$omp end single

           nthr=1
!$         nthr=omp_get_thread_num()+1
           call my_ft8var%decodevar(ft8_decodedvar,params%nQSOProgress,nfqso, &
                params%nft8rxfsens,params%nftx,nutc,ft8_range_low(nthr),      &
                ft8_range_high(nthr),params%ncandthin,params%ndtcenter,nsec,  &
                params%napwid,params%lmycallstd,params%lhiscallstd,           &
                params%nstophint,nthr,numthreads,logical(params%nagainfil),   &
                params%lft8lowth,params%lft8subpass,params%lhideft8dupes,     &
                params%lft8apon,active_progress_generation,                 &
                mtd_worker_residual(:,nthr),                                  &
                mtd_worker_spectrum(:,nthr))
!$omp end parallel

           call write_decode_progress(active_progress_generation)
           call mtd_finish(dd8)

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
              nFT8decd=my_ft8var%decodedvar
              dtmed=0.            
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
                    do i=1,nFT8decd
                       if(i.lt.nFT8decd-1) then
                          if((my_ft8var%xdtt(i).gt.my_ft8var%xdtt(i+1) .and.   &
                               my_ft8var%xdtt(i).lt.my_ft8var%xdtt(i+2)) .or.  &
                               (my_ft8var%xdtt(i).lt.my_ft8var%xdtt(i+1) .and. &
                               my_ft8var%xdtt(i).gt.my_ft8var%xdtt(i+2))) then
                             dtmed=my_ft8var%xdtt(i)
                          else if((my_ft8var%xdtt(i+1).gt.my_ft8var%xdtt(i) .and. &
                               my_ft8var%xdtt(i+1).lt.my_ft8var%xdtt(i+2)) .or.   &
                               (my_ft8var%xdtt(i+1).lt.my_ft8var%xdtt(i) .and.    &
                               my_ft8var%xdtt(i+1).gt.my_ft8var%xdtt(i+2))) then
                             dtmed=my_ft8var%xdtt(i+1)
                          else if((my_ft8var%xdtt(i+2).gt.my_ft8var%xdtt(i) .and. &
                               my_ft8var%xdtt(i+2).lt.my_ft8var%xdtt(i+1)) .or.   &
                               (my_ft8var%xdtt(i+2).lt.my_ft8var%xdtt(i) .and.    &
                               my_ft8var%xdtt(i+2).gt.my_ft8var%xdtt(i+1))) then
                             dtmed=my_ft8var%xdtt(i+2)
                          else
                             dtmed=my_ft8var%xdtt(i)
                          endif
                          sumxdt=sumxdt+dtmed
                       else
                          sumxdt=sumxdt+dtmed ! use last median value
                       endif
                    enddo
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
                    sumxdt=sum(my_ft8var%xdtt(1:nFT8decd))
                    if(nFT8decd.eq.2) then
                       avexdt=(1.5*avexdt+0.5*sumxdt/nFT8decd)/2
                    else if(nFT8decd.eq.1) then
                       avexdt=(1.75*avexdt+0.25*sumxdt)/2
                    endif
                 endif
              endif
           endif
           
           if(nFT8decd.gt.10 .and. nintcount.eq.1) avexdt=sumxdt/nFT8decd ! fast track after Sync or mode change on crowded bands
           call fillhashvar(numthreads,.true.)
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

        rewind 19
        if(nfox.eq.0) then
           endfile 19
           rewind 19
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
                 write(19,1004) c2fox(j),g2fox(j),nsnrfox(j),nfreqfox(j), &
                      nDkm,nage
1004             format(a12,1x,a4,i5,i6,i7,i3)
              endif
           enddo
           nfox=j
           flush(19)
        endif
     endif
     go to 800
  endif    ! end of code for FT8 mode

  if(params%nmode.eq.5) then
     call timer('decft4  ',0)
     call my_ft4%decode(ft4_decoded,id2,params%nQSOProgress,params%nfqso,    &
          params%nfa,params%nfb,params%ndepth,                               &
          logical(params%lapcqonly),ncontest,mycall,hiscall)
     call timer('decft4  ',1)
     go to 800
  endif

  if(params%nmode.eq.66) then        !NB: JT65 = 65, Q65 = 66.
     ! We're in Q65 mode
     open(17,file=trim(temp_dir)//'/red.dat',status='unknown')
     open(14,file=trim(temp_dir)//'/avemsg.txt',status='unknown')
     call timer('dec_q65 ',0)
     nqd=1
     call my_q65%decode(q65_decoded,id2,nqd,params%nutc,params%ntr,      &
          params%nsubmode,params%nfqso,params%ntol,params%ndepth,        &
          params%nfa,params%nfb,logical(params%nclearave),               &
          single_decode,logical(params%nagain),params%max_drift,         &
          logical(params%newdat),params%emedelay,mycall,hiscall,hisgrid, &
          params%nQSOProgress,ncontest,logical(params%lapcqonly),navg0,nqf)
     params%nclearave=.false.

     if(.not.params%nagain) then
                ! Go through identified candidates again, treating each as if it had been
                ! double-clicked on the waterfall.
        do k=1,20
           if(nqf(k).eq.0) exit
           if(params%nagain .and. abs(nqf(k)-params%nfqso).gt.params%ntol) cycle
           nqd=1
           navg0=0
           ntol=5
           call my_q65%decode(q65_decoded,id2,nqd,params%nutc,params%ntr,    &
                params%nsubmode,nqf(k),ntol,params%ndepth,                   &
                params%nfa,params%nfb,logical(params%nclearave),             &
                .true.,.true.,params%max_drift,                              &
                .false.,params%emedelay,mycall,hiscall,hisgrid,              &
                params%nQSOProgress,ncontest,logical(params%lapcqonly),      &
                navg0,nqf)
        enddo
     endif

     call timer('dec_q65 ',1)
     close(17)
     go to 800
  endif

  if(params%nmode.eq.240) then
     ! We're in FST4 mode
     ndepth=iand(params%ndepth,3)
     iwspr=0
     lprinthash22=.false.
     params%nsubmode=0
     call timer('dec_fst4',0)
     call my_fst4%decode(fst4_decoded,id2,params%nutc,                &
          params%nQSOProgress,params%nfa,params%nfb,                  &
          params%nfqso,ndepth,params%ntr,params%nexp_decode,          &
          params%ntol,params%emedelay,logical(params%nagain),         &
          logical(params%lapcqonly),mycall,hiscall,iwspr,lprinthash22)
     call timer('dec_fst4',1)
     go to 800
  endif

  if(params%nmode.eq.241 .or. params%nmode.eq.242) then
     ! We're in FST4W mode
     ndepth=iand(params%ndepth,3)
     iwspr=1
     lprinthash22=.false.
     if(params%nmode.eq.242) lprinthash22=.true. 
     call timer('dec_fst4',0)
     call my_fst4%decode(fst4_decoded,id2,params%nutc,                &
          params%nQSOProgress,params%nfa,params%nfb,                  &
          params%nfqso,ndepth,params%ntr,params%nexp_decode,          &
          params%ntol,params%emedelay,logical(params%nagain),         &
          logical(params%lapcqonly),mycall,hiscall,iwspr,lprinthash22)
     call timer('dec_fst4',1)
     go to 800
  endif

  ! Zap data at start that might come from T/R switching transient?
  nadd=100
  k=0
  bad0=.false.
  do i=1,240
     sq=0.
     do n=1,nadd
        k=k+1
        sq=sq + float(id2(k))**2
     enddo
     rms=sqrt(sq/nadd)
     if(rms.gt.10000.0) then
        bad0=.true.
        kbad=k
        rmsbad=rms
     endif
  enddo
  if(bad0) then
     nz=min(NTMAX*12000,kbad+100)
              !     id2(1:nz)=0                ! temporarily disabled as it can breaak the JT9 decoder, maybe others
  endif

  if(params%nmode.eq.4 .or. params%nmode.eq.65) open(14,file=trim(temp_dir)// &
       '/avemsg.txt',status='unknown')

  if(params%nmode.eq.4) then
     jz=52*nfsample
     if(params%newdat) then
        if(nfsample.eq.12000) call wav11(id2,jz,dd)
        if(nfsample.eq.11025) dd(1:jz)=id2(1:jz)
     else
        jz=52*11025
     endif
     call my_jt4%decode(jt4_decoded,dd,jz,params%nutc,params%nfqso,         &
          params%ntol,params%emedelay,params%dttol,logical(params%nagain),  &
          params%ndepth,logical(params%nclearave),params%minsync,           &
          params%minw,params%nsubmode,mycall,hiscall,         &
          hisgrid,params%nlist,params%listutc,jt4_average)
     go to 800
  endif

  npts65=52*12000
  if(baddata(id2,npts65)) then
     nsynced=0
     ndecoded=0
     go to 800
  endif
  
  ntol65=params%ntol              !### is this OK? ###
  newdat65=params%newdat
  newdat9=params%newdat

!$ call omp_set_dynamic(.true.)

  call wsjt_tsan_release_decoder_section_primary()
  call wsjt_tsan_release_decoder_section_secondary()
!$omp parallel sections num_threads(2) shared(ndecoded) if(.true.) !iif() needed on Mac

!$omp section
  call wsjt_tsan_acquire_decoder_section_primary()
  if(params%nmode.eq.65) then                       ! We're in JT65 mode     
     if(newdat65) dd(1:npts65)=id2(1:npts65)
     nf1=params%nfa
     nf2=params%nfb
     call timer('jt65a   ',0)
     call my_jt65%decode(jt65_decoded,dd,npts65,newdat65,params%nutc,      &
          nf1,nf2,params%nfqso,ntol65,params%nsubmode,params%minsync,      &
          logical(params%nagain),params%n2pass,logical(params%nrobust),    &
          ntrials,params%naggressive,params%ndepth,params%emedelay,        &
          logical(params%nclearave),mycall,hiscall,                        &
          hisgrid,params%nexp_decode,params%nQSOProgress,                  &
          logical(params%ljt65apon))
     call timer('jt65a   ',1)

  else if(params%nmode.eq.9 .or. (params%nmode.eq.(65+9) .and.             &
       params%ntxmode.eq.9)) then
              ! We're in JT9 mode, or should do JT9 first
     call timer('decjt9  ',0)
     call my_jt9%decode(jt9_decoded,ss,id2,params%nfqso,                   &
          newdat9,params%npts8,params%nfa,params%nfsplit,params%nfb,       &
          params%ntol,params%nzhsym,logical(params%nagain),params%ndepth,  &
          params%nmode,params%nsubmode,params%nexp_decode)
     call timer('decjt9  ',1)
  endif
  call wsjt_tsan_release_decoder_section_primary()

!$omp section
  call wsjt_tsan_acquire_decoder_section_secondary()
  if(params%nmode.eq.(65+9)) then       !Do the other mode (we're in dual mode)
     if (params%ntxmode.eq.9) then
        if(newdat65) dd(1:npts65)=id2(1:npts65)
        nf1=params%nfa
        nf2=params%nfb
        call timer('jt65a   ',0)
        call my_jt65%decode(jt65_decoded,dd,npts65,newdat65,params%nutc,   &
             nf1,nf2,params%nfqso,ntol65,params%nsubmode,params%minsync,   &
             logical(params%nagain),params%n2pass,logical(params%nrobust), &
             ntrials,params%naggressive,params%ndepth,params%emedelay,     &
             logical(params%nclearave),mycall,hiscall,       &
             hisgrid,params%nexp_decode,params%nQSOProgress,        &
             logical(params%ljt65apon))
        call timer('jt65a   ',1)
     else
        call timer('decjt9  ',0)
        call my_jt9%decode(jt9_decoded,ss,id2,params%nfqso,                &
             newdat9,params%npts8,params%nfa,params%nfsplit,params%nfb,    &
             params%ntol,params%nzhsym,logical(params%nagain),             &
             params%ndepth,params%nmode,params%nsubmode,params%nexp_decode)
        call timer('decjt9  ',1)
     end if
  endif
  call wsjt_tsan_release_decoder_section_secondary()

!$omp end parallel sections
  call wsjt_tsan_acquire_decoder_section_primary()
  call wsjt_tsan_acquire_decoder_section_secondary()


! JT65 is not yet producing info for nsynced, ndecoded.
800 ndecoded = my_jt4%decoded + my_jt65%decoded + my_jt9%decoded +       &
         my_ft8%decoded + my_ft8var%decodedvar + my_ft4%decoded +        &
         my_fst4%decoded + my_q65%decoded
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
  close(13)
  if(ncontest.eq.6) close(19)
  if(params%nmode.eq.4 .or. params%nmode.eq.65 .or. params%nmode.eq.66) close(14)
  return

contains

  subroutine run_ft8_mtd_a8_decode()
    implicit none

    external ft8_a8d
    real f1,xdt,fbest,xsnr,plog,qual
    integer nsnr,iaptype
    character(len=6) dxgrid
    character(len=37) msg37

    if(.not.params%lft8apon) return
    if(ncontest.eq.6 .or. ncontest.eq.7) return
    if(len(trim(hiscall)).lt.3 .or. len(trim(hisgrid4)).lt.4) return
    if(.not.ltry_a8) return

    f1=nfqso
    dxgrid=hisgrid4
    call timer('ft8_a8d ',0)
    call ft8_a8d(dd8,mycall,hiscall,dxgrid,f1,xdt,fbest,xsnr,plog,msg37, &
         active_progress_generation)
    call timer('ft8_a8d ',1)

    if(msg37(1:1).ne.' ') then
       if(associated(my_ft8var%callback)) then
          nsnr=nint(xsnr)
          iaptype=8
          qual=1.0
          if(plog.lt.-147.0) qual=0.16
          call my_ft8var%callback(nsnr,xdt,fbest,msg37,iaptype,qual)
       endif
    endif
  end subroutine run_ft8_mtd_a8_decode


end subroutine multimode_decoder_core

subroutine multimode_decoder(ss,id2,params,nfsample)
  use prog_args, only: lquiet
  use streaming_emit, only: streaming_emit_enabled,                       &
       streaming_emit_decode_finished
  use decode_completion_module, only: decode_completion_result,          &
       write_decode_completion

  include 'jt9com.f90'

  real ss(184,NSMAX)
  integer*2 id2(NTMAX*12000)
  type(params_block) :: params
  type(decode_completion_result) :: completion

  call multimode_decoder_core(ss,id2,params,nfsample,completion,0)
  if (.not. completion%available) return

  if (streaming_emit_enabled()) then
     call streaming_emit_decode_finished(params%nutc)
  else if (.not. lquiet) then
     call write_decode_completion(completion)
  end if
  call flush(6)
end subroutine multimode_decoder
