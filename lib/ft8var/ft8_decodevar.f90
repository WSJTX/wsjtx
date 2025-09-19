module ft8_decodevar

  logical ltry_a8
  type :: ft8_decodervar
     procedure(ft8_decodevar_callback), pointer :: callback
   contains
     procedure :: decodevar
  end type ft8_decodervar

  abstract interface
     subroutine ft8_decodevar_callback (this,snr,dt,freq,decodedvar,nap,qual)
       import ft8_decodervar
       implicit none
       class(ft8_decodervar), intent(inout) :: this
       integer, intent(in) :: snr
       integer, intent(in) :: nap 
       real, intent(in) :: dt,freq,qual
       character(len=37), intent(in) :: decodedvar !ft8md was 26
     end subroutine ft8_decodevar_callback
  end interface

contains

  subroutine decodevar(this,callback,nQSOProgress,nfqso,nft8rxfsens,nftx,nutc,  &
       nfa,nfb,ncandthin,ndtcenter,nsec,napwid,lmycallstd,lhiscallstd,          &
       stophint,nthr,numthreads,nagainfil,lft8lowth,lft8subpass,lhideft8dupes)

    use omp_lib

    use ft8_mod1, only : ndecodes,allmessages,allsnrs,allfreq,odd,even,nmsg,    &
         lastrxmsg,lasthcall,calldteven,calldtodd,incall,oddcopy,evencopy,      &
         avexdt,mycall,hiscall,dd8,nft8cycles,ncandallthr,nincallthr,evencq,    &
         oddcq,numcqsig,numdeccq,evenmyc,oddmyc,nummycsig,numdecmyc,lapmyc,     &
         evenqso,oddqso,lqsomsgdcd,hisgrid4

    include 'ft8_params.f90'

    class(ft8_decodervar), intent(inout) :: this
    procedure(ft8_decodevar_callback) :: callback
    real, DIMENSION(:), ALLOCATABLE :: dd8m
    real candidate(4,460),freqsub(200)
    real qual !ft8md
    integer, intent(in) :: nQSOProgress,nfqso,nft8rxfsens,nftx,nfa,nfb,         &
         ncandthin,ndtcenter,nsec,napwid,nthr,numthreads
    logical, intent(in) :: nagainfil
    logical(1), intent(in) :: stophint,lft8lowth,lft8subpass,lhideft8dupes,     &
         lmycallstd,lhiscallstd
    logical newdat1,lsubtract,ldupe,lFreeText,lspecial
    logical(1) lft8sdec,lft8s,lft8sd,lrepliedother,lhashmsg,lqsothread,         &
         lhidemsg,lhighsens,lcqcand,lsubtracted,levenint,loddint,lnohiscall,    &
         lnomycall,lnohisgrid
    character msg37*37,msg37_2*37,msg26*37,call2*12 !ft8md msg26 was *26
    character*37 msgsrcvd(130)

    type oddtmp_struct
       real freq
       real dt
       logical lstate
       character*37 msg
    end type oddtmp_struct
    type(oddtmp_struct) oddtmp(130)

    type eventmp_struct
       real freq
       real dt
       logical lstate
       character*37 msg
    end type eventmp_struct
    type(eventmp_struct) eventmp(130)

    type tmpcqdec_struct
       real freq
       real xdt
    end type tmpcqdec_struct
    type(tmpcqdec_struct) tmpcqdec(numdeccq) ! 40 sigs

    type tmpcqsig_struct
       real freq
       real xdt
      complex cs(0:7,79)
    end type tmpcqsig_struct
    type(tmpcqsig_struct) tmpcqsig(numcqsig) ! 20 sigs

    type tmpmyc_struct
       real freq
       real xdt
    end type tmpmyc_struct
    type(tmpmyc_struct) tmpmyc(numdecmyc) ! 25 sigs

    type tmpmycsig_struct
       real freq
       real xdt
       complex cs(0:7,79)
    end type tmpmycsig_struct
    type(tmpmycsig_struct) tmpmycsig(nummycsig) ! 5 sigs

    type tmpqsosig_struct
       real freq
       real xdt
       complex cs(0:7,79)
    end type tmpqsosig_struct
    type(tmpqsosig_struct) tmpqsosig(1)

    this%callback => callback

    oddtmp%lstate=.false.
    eventmp%lstate=.false.
    nmsgloc=0
    ncandthr=0
    nmsgcq=0
    tmpcqdec(:)%freq=6000.0
    nmsgmyc=0
    tmpmyc(:)%freq=6000.0
    tmpcqsig(:)%freq=6000.0
    tmpmycsig(:)%freq=6000.0
    tmpqsosig(1)%freq=6000.0
    if(hiscall.eq.'') then
       lastrxmsg(1)%lstate=.false. 
    else if(lastrxmsg(1)%lstate .and. lasthcall.ne.hiscall .and.               &
         index(lastrxmsg(1)%lastmsg,trim(hiscall)).le.0) then
       lastrxmsg(1)%lstate=.false.
    endif

    levenint=.false.
    loddint=.false.
    if(nsec.eq.0 .or. nsec.eq.30) then
       levenint=.true.
    elseif(nsec.eq.15 .or. nsec.eq.45) then
       loddint=.true.
    endif

    lrepliedother=.false.
    lft8sdec=.false.
    lqsothread=.false.
    lsubtracted=.false.
    ncount=0
    mycalllen1=len_trim(mycall)+1
    nincallthr(nthr)=0
    nallocthr=0
    ncqsignal=0
    nmycsignal=0

    lnohiscall=.false.
    if(len_trim(hiscall).lt.3) lnohiscall=.true.
    lnomycall=.false.
    if(len_trim(mycall).lt.3) lnomycall=.true.
    lnohisgrid=.false.
    if(len_trim(hisgrid4).ne.4) lnohisgrid=.true.
    if(nfqso.ge.nfa .and. nfqso.le.nfb) lqsothread=.true.

    if(lqsothread .and. .not.lastrxmsg(1)%lstate .and. .not.stophint .and.     &
         hiscall.ne.'') then
! got incoming call
       do i=1,30
          if(incall(i)%msg(1:1).eq." ") exit
          if(index(incall(i)%msg,(trim(mycall)//' '//trim(hiscall))).eq.1) then
             lastrxmsg(1)%lastmsg=incall(i)%msg
             lastrxmsg(1)%xdt=incall(i)%xdt
             lastrxmsg(1)%lstate=.true.
             exit
          endif
       enddo

       if(.not.lastrxmsg(1)%lstate) then
! calling someone, lastrxmsg still not valid
          if(levenint) then
             do i=1,130
                if(.not.evencopy(i)%lstate) cycle
                if(index(evencopy(i)%msg,' '//trim(hiscall)//' ').gt.1) then
                   lastrxmsg(1)%lastmsg=evencopy(i)%msg
                   lastrxmsg(1)%xdt=evencopy(i)%dt
                   lastrxmsg(1)%lstate=.true.
                   exit
                endif
             enddo

          elseif(loddint) then
             do i=1,130
                if(.not.oddcopy(i)%lstate) cycle
                if(index(oddcopy(i)%msg,' '//trim(hiscall)//' ').gt.1) then
                   lastrxmsg(1)%lastmsg=oddcopy(i)%msg
                   lastrxmsg(1)%xdt=oddcopy(i)%dt
                   lastrxmsg(1)%lstate=.true.
                   exit
                endif
             enddo
          endif
       endif
    endif

! sliding search over +/- 2.5s relative to 0.5s TX start time
    jzb=-62 + avexdt*25.
    jzt=62 + avexdt*25.

    npass=3 ! fallback
    if(nft8cycles.eq.1) then
       npass=3
    else if(nft8cycles.eq.2) then
       npass=6
    else if(nft8cycles.eq.3) then
       npass=9
    else
       npass=3
    endif

    syncmin=1.3
    do ipass=1,npass
       newdat1=.true.
       lsubtract=.true.
       npos=0
       if(ipass.eq.1 .or. ipass.eq.4 .or. ipass.eq.7) then
          if(lft8lowth) syncmin=1.225
       elseif(ipass.eq.2 .or. ipass.eq.5 .or. ipass.eq.8) then
          if(lft8lowth) syncmin=1.3
       elseif(ipass.eq.3 .or. ipass.eq.6 .or. ipass.eq.9) then
          if(lft8lowth) syncmin=1.1
       endif
       if(ipass.gt.5 .or. (ipass.eq.3 .and. npass.eq.3)) lsubtract=.false.

       if(ipass.eq.4) then
!$omp barrier
!$omp single
          if(npass.eq.9) then ! 3 decoding cycles
             nallocthr=nthr
             allocate(dd8m(180000), STAT = nAllocateStatus1)
             if(nAllocateStatus1.ne.0) STOP "Not enough memory"
             dd8m=dd8
          endif
          do i=1,179999
             dd8(i)=(dd8(i)+dd8(i+1))/2
          enddo
!$omp end single
!$omp barrier
       else if(ipass.eq.7) then
!$omp barrier
          if(nthr.eq.nallocthr) then
             dd8(1)=dd8m(1)
             do i=2,180000
                dd8(i)=(dd8m(i-1)+dd8m(i))/2
             enddo
             deallocate (dd8m, STAT = nDeAllocateStatus1)
             if (nDeAllocateStatus1.ne.0) print *, 'failed to release memory'
          endif
!$omp barrier
       endif
       
       call sync8var(nfa,nfb,syncmin,nfqso,candidate,ncand,jzb,jzt,ipass,       &
            lqsothread,ncandthin,ndtcenter)
       do icand=1,ncand
          sync=candidate(3,icand)
          f1=candidate(1,icand)
          xdt=candidate(2,icand)
          lcqcand=.false.
          if(candidate(4,icand).gt.1.0) lcqcand=.true.
          lhighsens=.false.
          if(sync.lt.1.9 .or. ((ipass.eq.2 .or. ipass.eq.4 .or. ipass.eq.6)     &
               .and. sync.lt.3.15)) lhighsens=.true.
          lspecial=.false.
          lFreeText=.false.
          i3bit=0
          lft8s=.false.
          lft8sd=.false.
          lhashmsg=.false.
          iaptype=0
          msg37=''
          i3=16
          n3=16

          call ft8bvar(newdat1,nQSOProgress,nfqso,nftx,napwid,lsubtract,npos,   &
               freqsub,tmpcqdec,tmpmyc,nagainfil,iaptype,f1,xdt,nbadcrc,        &
               lft8sdec,msg37,msg37_2,xsnr,stophint,nthr,lFreeText,ipass,       &
               lft8subpass,lspecial,lcqcand,ncqsignal,nmycsignal,npass,i3bit,   &
               lft8s,lmycallstd,lhiscallstd,levenint,loddint,lft8sd,i3,n3,      &
               nft8rxfsens,ncount,msgsrcvd,lrepliedother,lhashmsg,lqsothread,   &
               lft8lowth,lhighsens,lsubtracted,tmpcqsig,tmpmycsig,tmpqsosig,    &
               lnohiscall,lnomycall,lnohisgrid,qual,iaptype2)
          nsnr=nint(xsnr)
          xdt=xdt-0.5
          if(nbadcrc.eq.0) then
             lhidemsg=.false.
             if(lspecial) then
                nspecial=2
             else
                nspecial=1
             endif

!$omp critical(find_dupes)
             do k=1,nspecial
          !ft8md  if(k.eq.2) msg37=msg37_2  ! this splits DXpedition mode msg into 2 lines 
                ldupe=.false.
                if(msg37(1:6).eq."      ") ldupe=.true. 
                if(.not.ldupe .and. ndecodes.gt.0) then
                   do idec=1,ndecodes
                      if(lhideft8dupes) then
                         if(msg37.eq.allmessages(idec) .and.                    &
                              (nsnr.le.allsnrs(idec) .or.                       &
                              (nsnr.gt.allsnrs(idec) .and.                      &
                              abs(allfreq(idec)-f1).lt.45.0))) then
                            ldupe=.true.
                            exit
                         endif
                      else
                         if(msg37.eq.allmessages(idec) .and.                    &
                              ((nsnr.le.allsnrs(idec) .and.                     &
                              abs(allfreq(idec)-f1).lt.45.0) .or.               &
                              (nsnr.gt.allsnrs(idec) .and.                      &
                              abs(allfreq(idec)-f1).lt.45.0 .and.               &
                              numthreads.ne.1))) then
                            ldupe=.true.
                            exit
                         endif
                      endif
                   enddo
                endif
                
                if(.not.ldupe) then
                   if(.not.lFreeText .and. k.eq.1) call extract_callvar(msg37,call2)
!!!$omp critical(update_arrays)
                   ndecodes=ndecodes+1
                   allmessages(ndecodes)=msg37
                   allsnrs(ndecodes)=nsnr
                   allfreq(ndecodes)=f1

                   if(.not.lhidemsg) then
 ! simulated wav tests affected, structure contains data for at least previous and current even|odd intervals
                      if(levenint) then
                         calldteven(150:2:-1)=calldteven(150-1:1:-1)
                         calldteven(1)%call2=call2
                         calldteven(1)%dt=xdt
                      else if(loddint) then
                         calldtodd(150:2:-1)=calldtodd(150-1:1:-1)
                         calldtodd(1)%call2=call2
                         calldtodd(1)%dt=xdt
                      endif
                   endif
!!!$omp end critical(update_arrays)

                   if(.not.lhidemsg) then
                      msg26=msg37(1:37) !ft8md was 1:26
                      if(associated(this%callback)) then
                         call this%callback(nsnr,xdt,f1,msg26,iaptype2,qual)
                      endif
                   endif

                   if(msg37(1:3).eq.'CQ ' .and. nmsgcq.lt.numdeccq) then
                      nmsgcq=nmsgcq+1
                      xdtr=xdt+0.5
                      tmpcqdec(nmsgcq)%freq=f1
                      tmpcqdec(nmsgcq)%xdt=xdtr
                   endif
                   
                   if(lapmyc .and. lmycallstd) then
                      ispc1=index(msg37,' ')
                      if(msg37(1:ispc1-1).eq.trim(mycall) .and.                 &
                           nmsgmyc.lt.numdecmyc) then
                         nmsgmyc=nmsgmyc+1
                         xdtr=xdt+0.5
                         tmpmyc(nmsgmyc)%freq=f1
                         tmpmyc(nmsgmyc)%xdt=xdtr
                      endif
                   endif

                   if(i3.eq.4 .and. msg37(1:3).eq.'CQ ' .and.                   &
                        mod(nsec,15).eq.0 .and. nmsgloc.lt.130) then
                      nmsgloc=nmsgloc+1

                      if(levenint) then
                         eventmp(nmsgloc)%msg=msg37
                         eventmp(nmsgloc)%freq=f1
                         eventmp(nmsgloc)%dt=xdt
                         eventmp(nmsgloc)%lstate=.true.
                      endif

                      if(loddint) then
                         oddtmp(nmsgloc)%msg=msg37
                         oddtmp(nmsgloc)%freq=f1
                         oddtmp(nmsgloc)%dt=xdt
                         oddtmp(nmsgloc)%lstate=.true.
                      endif
                      go to 4 ! tmp filled in
                   endif

                   if(.not.lFreeText) then
! protection against any possible free txtmsg bit corruption
                      ispc1=index(msg37,' ')
                      if(.not.lhashmsg .and. mod(nsec,15).eq.0 .and.            &
                           ((i3.eq.1 .and. .not.lft8sd) .or. lft8sd) .and.      &
                           msg37(1:ispc1-1).ne.trim(mycall) .and.               &
                           nmsgloc.lt.130 .and. index(msg37,'<').le.0) then


                         ! compound callsigns not supported
                         if(index(msg37,'/').gt.0 .and. msg37(1:3).ne.'CQ ') go to 4
                         
                         nmsgloc=nmsgloc+1
                         if(levenint) then
                            eventmp(nmsgloc)%msg=msg37
                            eventmp(nmsgloc)%freq=f1
                            eventmp(nmsgloc)%dt=xdt
                            eventmp(nmsgloc)%lstate=.true.
                         endif
                         if(loddint) then
                            oddtmp(nmsgloc)%msg=msg37
                            oddtmp(nmsgloc)%freq=f1
                            oddtmp(nmsgloc)%dt=xdt
                            oddtmp(nmsgloc)%lstate=.true.
                         endif
                      endif
                   endif
                endif
4               continue
             enddo !do k
!$omp end critical(find_dupes)

          endif
       enddo !icand
       ncandthr=ncandthr+ncand
    enddo !ipass

    if(levenint) then
       evencq(1:ncqsignal,nthr)%freq=tmpcqsig(1:ncqsignal)%freq
       evencq(1:ncqsignal,nthr)%xdt=tmpcqsig(1:ncqsignal)%xdt
       do ik=1,ncqsignal
          evencq(ik,nthr)%cs=tmpcqsig(ik)%cs
       enddo
       if(lapmyc) then
          evenmyc(1:nmycsignal,nthr)%freq=tmpmycsig(1:nmycsignal)%freq
          evenmyc(1:nmycsignal,nthr)%xdt=tmpmycsig(1:nmycsignal)%xdt
          do ik=1,nmycsignal
             evenmyc(ik,nthr)%cs=tmpmycsig(ik)%cs
          enddo
          if(.not.lqsomsgdcd .and. tmpqsosig(1)%freq.lt.5001.) then
             evenqso(1,nthr)%freq=tmpqsosig(1)%freq
             evenqso(1,nthr)%xdt=tmpqsosig(1)%xdt
             evenqso(1,nthr)%cs=tmpqsosig(1)%cs
          endif
       endif
    else if(loddint) then
       oddcq(1:ncqsignal,nthr)%freq=tmpcqsig(1:ncqsignal)%freq
       oddcq(1:ncqsignal,nthr)%xdt=tmpcqsig(1:ncqsignal)%xdt
       do ik=1,ncqsignal
          oddcq(ik,nthr)%cs=tmpcqsig(ik)%cs
       enddo
       if(lapmyc) then
          oddmyc(1:nmycsignal,nthr)%freq=tmpmycsig(1:nmycsignal)%freq
          oddmyc(1:nmycsignal,nthr)%xdt=tmpmycsig(1:nmycsignal)%xdt
          do ik=1,nmycsignal
             oddmyc(ik,nthr)%cs=tmpmycsig(ik)%cs
          enddo
          if(.not.lqsomsgdcd .and. tmpqsosig(1)%freq.lt.5001.) then
             oddqso(1,nthr)%freq=tmpqsosig(1)%freq
             oddqso(1,nthr)%xdt=tmpqsosig(1)%xdt
             oddqso(1,nthr)%cs=tmpqsosig(1)%cs
          endif
       endif
    endif

    ncandthr=nint(float(ncandthr)/npass)
    ncandallthr(nthr)=ncandallthr(nthr)+ncandthr

    if(nmsgloc.gt.0) then
!$omp critical(update_structures)
       if(levenint) then
          even(nmsg+1:nmsg+nmsgloc)%msg=eventmp(1:nmsgloc)%msg
          even(nmsg+1:nmsg+nmsgloc)%freq=eventmp(1:nmsgloc)%freq
          even(nmsg+1:nmsg+nmsgloc)%dt=eventmp(1:nmsgloc)%dt
          even(nmsg+1:nmsg+nmsgloc)%lstate=eventmp(1:nmsgloc)%lstate
          nmsg=nmsg+nmsgloc
       else if(loddint) then
          odd(nmsg+1:nmsg+nmsgloc)%msg=oddtmp(1:nmsgloc)%msg
          odd(nmsg+1:nmsg+nmsgloc)%freq=oddtmp(1:nmsgloc)%freq
          odd(nmsg+1:nmsg+nmsgloc)%dt=oddtmp(1:nmsgloc)%dt
          odd(nmsg+1:nmsg+nmsgloc)%lstate=oddtmp(1:nmsgloc)%lstate
          nmsg=nmsg+nmsgloc
       endif
!$omp end critical(update_structures)
    endif
    
    return
  end subroutine decodevar
end module ft8_decodevar
