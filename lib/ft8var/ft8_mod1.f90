module ft8_mod1

  use packjt77_schema, only: PACK77_TYPE1_RR73_TAIL

  save

  parameter (NPS=180000,NFR=151680,NFILT1=4000,NFILT2=3400,numcqsig=20,   &
       numdeccq=40,nummycsig=5,numdecmyc=25,nmaxthreads=24)
  real*4 dd8(nps)
  real endcorr(NFILT1/2+1)
  complex cw(nps),csync(0:6,32),csynce(0:18,32),csyncsd(0:18,32),         &
       csyncsdcq(0:57,32),csynccq(0:7,32),ctwkw(11,32),ctwkn(11,32),      &
       ctwk256(256)
  character*37 allmessages(200),msgsd76(76),msg(56),msgroot,msgincall(174)
  character lasthcall*12,mycall12_0*12,mycall12_00*12,hiscall12_0*12,     &
       hisgrid4*4,hisgrid*6 !ft8md w3sz added hisgrid
  character(len=12) :: mycall,hiscall,mybcall,hisbcall
  real allfreq(200),windowc1(0:54),windowx(0:200),pivalue,facx,twopi,     &
       facc1,dt,sumxdtt(24),avexdt,xdtincall(174)
  integer itone76(76,79),idtone76(76,58),itone56(56,79),idtone56(56,58),  &
       idtone25(25,58),allsnrs(200),apsym(58),idtonemyc(58),mcq(29),      &
       mrrr(19),m73(19),naptypes(0:5,27),icos7(0:6),                    &
       graymap(0:7),nappasses(0:5),nmsg,ndecodes,nlasttx,mycalllen1,      &
       msgrootlen,nfawide,nfbwide,nhaptypes(0:5,27),apsymsp(66),          &
       apsymdxns1(58),apsymdxnsrrr(77),ndxnsaptypes(0:5,27),apcqsym(77),  &
       apsymdxnsrr73(77),apsymdxns73(77),nft8cycles,ncandallthr(24),      &
       maskincallthr(25),nincallthr(24),idtonecqdxcns(58),apsymmyns1(29), &
       apsymmyns2(58),apsymmynsrr73(77),apsymmyns73(77),                  &
       nmycnsaptypes(0:5,27),apsymdxstd(58),apsymdxnsr73(77),             &
       apsymdxns732(77),apsymmynsrrr(77),idtonedxcns73(58),               &
       idtonefox73(58),idtonespec(58),nintcount
  integer, parameter :: mrr73(19)=2*PACK77_TYPE1_RR73_TAIL-1
  integer*1 gen(91,174)
  logical one(0:511,0:8),first_osd
  logical, private :: lqsomsgdcd=.false.
  logical(1) lapmyc,lagcc,lagccbail,lhound,lenabledxcsearch,              &
       lwidedxcsearch,lmultinst,lskiptx1,ltxing
  logical(1) idtone25_valid(25),idtone56_valid(56),idtone76_valid(76)
  logical(1) csyncsd_valid,csyncsdcq_valid
  data maskincallthr/0,30,45,55,65,75,85,90,95,100,105,110,115,120,125,   &
       130,135,140,145,150,155,160,165,170,175/
  data     mcq/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0/
  data    mrrr/0,1,1,1,1,1,1,0,1,0,0,1,0,0,1,0,0,0,1/
  data     m73/0,1,1,1,1,1,1,0,1,0,0,1,0,1,0,0,0,0,1/
  data   icos7/3,1,4,0,6,5,2/
  data graymap/0,1,3,2,5,6,4,7/
  data mycall12_0/'dummy'/
  data mycall12_00/'dummy'/
  data hiscall12_0/'dummy'/

! Hound OFF, MyCall is standard, DXCall is standard or empty
  data naptypes(0,1:27)/3,3,3,0,0,0,0,0,0,0,0,0,2,2,2,1,1,1,31,31,31,36,  &
       36,36,35,35,35/ ! Tx6 CQ
  data naptypes(1,1:27)/3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,31,31,31,36,  &
       36,36,35,35,35/ ! Tx1 Grid
  data naptypes(2,1:27)/3,3,3,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,31,31,31,36,  &
       36,36,35,35,35/ ! Tx2 Report
  data naptypes(3,1:27)/3,3,3,6,6,6,5,5,5,4,4,4,0,0,0,0,0,0,31,31,31,36,  &
       36,36,35,35,35/ ! Tx3 RRreport
  data naptypes(4,1:27)/3,3,3,6,6,6,5,5,5,4,4,4,2,2,2,0,0,0,31,31,31,36,  &
       36,36,35,35,35/ ! Tx4 RRR,RR73
  data naptypes(5,1:27)/0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,1,1,1,31,31,31,36,  &
       36,36,35,35,35/ ! Tx5 73

! Hound OFF, MyCall is non-standard, DXCall is standard or empty
  data nmycnsaptypes(0,1:27)/0,0,0,0,0,0,0,0,0,0,0,0,40,40,40,1,1,1,31,   &
       31,31,36,36,36,35,35,35/             ! Tx6 CQ
  data nmycnsaptypes(1,1:27)/41,41,41,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,31,   &
       31,31,36,36,36,35,35,35/             ! Tx1 DXcall MyCall
  data nmycnsaptypes(2,1:27)/41,41,41,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,31,   &
       31,31,36,36,36,35,35,35/             ! Tx2 Report
  data nmycnsaptypes(3,1:27)/41,41,41,44,44,44,43,43,43,42,42,42,0,0,0,0, &
       0,0,31,31,31,36,36,36,35,35,35/    ! Tx3 RRreport
  data nmycnsaptypes(4,1:27)/41,41,41,44,44,44,43,43,43,42,42,42,40,40,40,&
       0,0,0,31,31,31,36,36,36,35,35,35/ ! Tx4 RRR,RR73
  data nmycnsaptypes(5,1:27)/0,0,0,0,0,0,0,0,0,0,0,0,40,40,40,1,1,1,31,31,&
       31,36,36,36,35,35,35/             ! Tx5 73

! Hound mode
  data nhaptypes(0,1:27)/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0,0,0,0,0,0, &
       111,111,111/       ! Tx6 CQ, possible in idle mode if DXCall is empty
  data nhaptypes(1,1:27)/21,21,21,22,22,22,0,0,0,0,0,0,0,0,0,31,31,31,0,0,&
       0,36,36,36,0,0,0/ ! Tx1 Grid idle mode or transmitting
  data nhaptypes(2,1:27)/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, &
       0,0,0/             ! Tx2 none
  data nhaptypes(3,1:27)/21,21,21,22,22,22,23,23,23,24,24,24,0,0,0,0,0,0, &
       0,0,0,0,0,0,0,0,0/ ! Tx3 RRreport QSO in progress or QSO is finished
  data nhaptypes(4,1:27)/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, &
       0,0,0/             ! Tx4 none
  data nhaptypes(5,1:27)/0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, &
       0,0,0/             ! Tx5 none

!Non-standard DXCall
  data ndxnsaptypes(0,1:27)/0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,1,1,1,31,31,31, &
       36,36,36,35,35,35/             ! Tx6 CQ
  data ndxnsaptypes(1,1:27)/11,11,11,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,31,31, &
       31,36,36,36,35,35,35/          ! Tx1 Grid
  data ndxnsaptypes(2,1:27)/11,11,11,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,31,31, &
       31,36,36,36,35,35,35/          ! Tx2 Report
  data ndxnsaptypes(3,1:27)/11,11,11,14,14,14,13,13,13,12,12,12,0,0,0,0,  &
       0,0,31,31,31,36,36,36,35,35,35/ ! Tx3 RRreport
  data ndxnsaptypes(4,1:27)/11,11,11,14,14,14,13,13,13,12,12,12,2,2,2,0,  &
       0,0,31,31,31,36,36,36,35,35,35/ ! Tx4 RRR,RR73
  data ndxnsaptypes(5,1:27)/0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,1,1,1,31,31,31, &
       36,36,36,35,35,35/             ! Tx5 73
  data nintcount/0/
  data avexdt/0.0/
  data first_osd/.true./
  data idtone25_valid/25*.false./
  data idtone56_valid/56*.false./
  data idtone76_valid/76*.false./
  data csyncsd_valid/.false./
  data csyncsdcq_valid/.false./

  type odd_struct
     real freq
     real dt
     logical lstate
     character*37 msg
  end type odd_struct
  type(odd_struct) odd(130)

  type even_struct
     real freq
     real dt
     logical lstate
     character*37 msg
  end type even_struct
  type(even_struct) even(130)

  type, private :: ft8_hint_record
     real :: freq=0.,dt=0.
     logical :: lstate=.false.
     character(len=37) :: msg=''
  end type ft8_hint_record
  type(ft8_hint_record), private, target :: oddcopy(130),evencopy(130)

  type ft8_period_hint
     logical :: valid=.false.,cq=.false.
     integer :: parity=0,index=0
     real :: frequency=0.,dt=0.
     character(len=37) :: message=''
  end type ft8_period_hint

  type lastrxmsg_struct
     real xdt
     logical lstate
     character*37 lastmsg
  end type lastrxmsg_struct
  type (lastrxmsg_struct) lastrxmsg(1)
  data lastrxmsg%lstate/.false./

  type callsigndtodd_struct
     real dt
     character*12 call2
  end type callsigndtodd_struct
  type (callsigndtodd_struct) calldtodd(150)

  type callsigndteven_struct
     real dt
     character*12 call2
  end type callsigndteven_struct
  type (callsigndteven_struct) calldteven(150)

  type incall_struct
     real xdt
     character*37 msg
  end type incall_struct
  type (incall_struct) incall(30)

  type evencq_struct
     real freq
     real xdt
     complex cs(0:7,79)
  end type evencq_struct
  type(evencq_struct) evencq(numcqsig,nmaxthreads) ! 24 threads

  type oddcq_struct
     real freq
     real xdt
     complex cs(0:7,79)
  end type oddcq_struct
  type(oddcq_struct) oddcq(numcqsig,nmaxthreads)

  type evenmyc_struct
     real freq
     real xdt
     complex cs(0:7,79)
  end type evenmyc_struct
  type(evenmyc_struct) evenmyc(nummycsig,nmaxthreads)

  type oddmyc_struct
     real freq
     real xdt
     complex cs(0:7,79)
  end type oddmyc_struct
  type(oddmyc_struct) oddmyc(nummycsig,nmaxthreads)

  type evenqso_struct
     real freq
     real xdt
     complex cs(0:7,79)
  end type evenqso_struct
  type(evenqso_struct) evenqso(1,nmaxthreads)

  type oddqso_struct
     real freq
     real xdt
     complex cs(0:7,79)
  end type oddqso_struct
  type(oddqso_struct) oddqso(1,nmaxthreads)

  interface
     subroutine wsjt_tsan_acquire_ft8_hint_state() bind(C)
     end subroutine wsjt_tsan_acquire_ft8_hint_state
     subroutine wsjt_tsan_release_ft8_hint_state() bind(C)
     end subroutine wsjt_tsan_release_ft8_hint_state
     subroutine wsjt_tsan_acquire_ft8_qso_state() bind(C)
     end subroutine wsjt_tsan_acquire_ft8_qso_state
     subroutine wsjt_tsan_release_ft8_qso_state() bind(C)
     end subroutine wsjt_tsan_release_ft8_qso_state
     subroutine wsjt_tsan_acquire_ft8_find_dupes() bind(C)
     end subroutine wsjt_tsan_acquire_ft8_find_dupes
     subroutine wsjt_tsan_release_ft8_find_dupes() bind(C)
     end subroutine wsjt_tsan_release_ft8_find_dupes
  end interface

contains

  ! Attempt initialization and reset run only while workers are stopped.
  subroutine begin_ft8_qso_attempt(suppressed)
    logical, intent(in) :: suppressed

!$omp critical(ft8_qso_state)
    call wsjt_tsan_acquire_ft8_qso_state()
    lqsomsgdcd=suppressed
    call wsjt_tsan_release_ft8_qso_state()
!$omp end critical(ft8_qso_state)
  end subroutine begin_ft8_qso_attempt

  logical function ft8_qso_hints_suppressed() result(suppressed)
!$omp critical(ft8_qso_state)
    call wsjt_tsan_acquire_ft8_qso_state()
    suppressed=lqsomsgdcd
    call wsjt_tsan_release_ft8_qso_state()
!$omp end critical(ft8_qso_state)
  end function ft8_qso_hints_suppressed

  subroutine suppress_ft8_qso_hints()
!$omp critical(ft8_qso_state)
    call wsjt_tsan_acquire_ft8_qso_state()
    lqsomsgdcd=.true.
    call wsjt_tsan_release_ft8_qso_state()
!$omp end critical(ft8_qso_state)
  end subroutine suppress_ft8_qso_hints

  subroutine clear_ft8_hint_history()
!$omp critical(ft8_hint_state)
    call wsjt_tsan_acquire_ft8_hint_state()
    evencopy=ft8_hint_record()
    oddcopy=ft8_hint_record()
    call wsjt_tsan_release_ft8_hint_state()
!$omp end critical(ft8_hint_state)
  end subroutine clear_ft8_hint_history

  ! Period rollover runs before workers start; copied records then stay immutable.
  subroutine prepare_ft8_hint_history(nsec)
    integer, intent(in) :: nsec

!$omp critical(ft8_hint_state)
    call wsjt_tsan_acquire_ft8_hint_state()
    select case(nsec)
    case(0,30)
       evencopy%msg=even%msg
       evencopy%freq=even%freq
       evencopy%dt=even%dt
       evencopy%lstate=even%lstate
       even%lstate=.false.
    case(15,45)
       oddcopy%msg=odd%msg
       oddcopy%freq=odd%freq
       oddcopy%dt=odd%dt
       oddcopy%lstate=odd%lstate
       odd%lstate=.false.
    case default
       even%lstate=.false.
       odd%lstate=.false.
       evencopy%lstate=.false.
       oddcopy%lstate=.false.
    end select
    call wsjt_tsan_release_ft8_hint_state()
!$omp end critical(ft8_hint_state)
  end subroutine prepare_ft8_hint_history

  subroutine get_ft8_period_hint(even_interval,odd_interval,frequency,dt,hint)
    logical, intent(in) :: even_interval,odd_interval
    real, intent(in) :: frequency,dt
    type(ft8_period_hint), intent(out) :: hint
    type(ft8_hint_record), pointer :: history(:)
    integer :: i,parity

    hint=ft8_period_hint()
    if(even_interval) then
       history=>evencopy
       parity=1
    else if(odd_interval) then
       history=>oddcopy
       parity=2
    else
       return
    endif
!$omp critical(ft8_hint_state)
    call wsjt_tsan_acquire_ft8_hint_state()
    do i=1,size(history)
       if(.not.history(i)%lstate) cycle
       if(.not.(abs(history(i)%freq-frequency).lt.3.0 .and. abs(history(i)%dt-dt).lt.0.19)) cycle
       hint%valid=.true.
       hint%parity=parity
       hint%index=i
       hint%message=history(i)%msg
       hint%frequency=history(i)%freq
       hint%dt=history(i)%dt
       if(hint%message(1:3).eq.'CQ ' .or. hint%message(1:3).eq.'DE ' .or. &
            hint%message(1:4).eq.'QRZ ') hint%cq=.true.
    enddo
    call wsjt_tsan_release_ft8_hint_state()
!$omp end critical(ft8_hint_state)
  end subroutine get_ft8_period_hint

  subroutine get_ft8_bootstrap_hint(partner,even_interval,odd_interval,hint)
    character(len=*), intent(in) :: partner
    logical, intent(in) :: even_interval,odd_interval
    type(ft8_period_hint), intent(out) :: hint
    type(ft8_hint_record), pointer :: history(:)
    integer :: i,parity

    hint=ft8_period_hint()
    if(even_interval) then
       history=>evencopy
       parity=1
    else if(odd_interval) then
       history=>oddcopy
       parity=2
    else
       return
    endif
!$omp critical(ft8_hint_state)
    call wsjt_tsan_acquire_ft8_hint_state()
    do i=1,size(history)
       if(.not.history(i)%lstate) cycle
       if(index(history(i)%msg,' '//trim(partner)//' ').le.1) cycle
       hint%valid=.true.
       hint%parity=parity
       hint%index=i
       hint%message=history(i)%msg
       hint%frequency=history(i)%freq
       hint%dt=history(i)%dt
       exit
    enddo
    call wsjt_tsan_release_ft8_hint_state()
!$omp end critical(ft8_hint_state)
  end subroutine get_ft8_bootstrap_hint

  ! Retiring eligibility does not cancel trials that already captured this hint.
  subroutine retire_ft8_period_hint(hint)
    type(ft8_period_hint), intent(in) :: hint

    if(.not.hint%valid .or. hint%index.lt.1 .or. hint%index.gt.size(evencopy)) return
!$omp critical(ft8_hint_state)
    call wsjt_tsan_acquire_ft8_hint_state()
    if(hint%parity.eq.1) evencopy(hint%index)%lstate=.false.
    if(hint%parity.eq.2) oddcopy(hint%index)%lstate=.false.
    call wsjt_tsan_release_ft8_hint_state()
!$omp end critical(ft8_hint_state)
  end subroutine retire_ft8_period_hint

  logical function ft8_message_seen(message,frequency) result(seen)
    implicit none
    character(len=*), intent(in) :: message
    real, intent(in) :: frequency
    integer :: i

    seen=.false.
!$omp critical(find_dupes)
    call wsjt_tsan_acquire_ft8_find_dupes()
    do i=1,ndecodes
       if(allmessages(i).eq.message .and. abs(allfreq(i)-frequency).lt.45.0) then
          seen=.true.
          exit
       endif
    enddo
    call wsjt_tsan_release_ft8_find_dupes()
!$omp end critical(find_dupes)
  end function ft8_message_seen

  subroutine ft8_lookup_call_dt(call,even_interval,odd_interval,found,dt)
    implicit none
    character(len=*), intent(in) :: call
    logical, intent(in) :: even_interval,odd_interval
    logical, intent(out) :: found
    real, intent(inout) :: dt
    integer :: i

    found=.false.
!$omp critical(find_dupes)
    call wsjt_tsan_acquire_ft8_find_dupes()
    if(even_interval) then
       do i=1,size(calldteven)
          if(trim(calldteven(i)%call2).eq.trim(call)) then
             dt=calldteven(i)%dt
             found=.true.
             exit
          endif
       enddo
    else if(odd_interval) then
       do i=1,size(calldtodd)
          if(trim(calldtodd(i)%call2).eq.trim(call)) then
             dt=calldtodd(i)%dt
             found=.true.
             exit
          endif
       enddo
    endif
    call wsjt_tsan_release_ft8_find_dupes()
!$omp end critical(find_dupes)
  end subroutine ft8_lookup_call_dt

  subroutine reset_ft8_mod1()
    dd8=0.
    allmessages=''
    allfreq=0.
    allsnrs=0
    ndecodes=0
    nmsg=0
    nintcount=0
    nft8cycles=0
    nlasttx=0
    sumxdtt=0.
    avexdt=0.
    lastrxmsg%lstate=.false.
    lastrxmsg%lastmsg=''
    lastrxmsg%xdt=0.
    odd%lstate=.false.
    even%lstate=.false.
    call clear_ft8_hint_history()
    odd%msg=''
    even%msg=''
    calldteven%call2=''
    calldtodd%call2=''
    calldteven%dt=0.
    calldtodd%dt=0.
    incall%msg=''
    incall%xdt=0.
    msgroot=''
    msgincall=''
    xdtincall=0.
    nincallthr=0
    ncandallthr=0
    idtone25_valid=.false.
    idtone56_valid=.false.
    idtone76_valid=.false.
    csyncsd_valid=.false.
    csyncsdcq_valid=.false.
    first_osd=.true.
    call begin_ft8_qso_attempt(.false.)
    lapmyc=.false.
    lasthcall=''
    mycall12_0='dummy'
    mycall12_00='dummy'
    hiscall12_0='dummy'
  end subroutine reset_ft8_mod1

end module ft8_mod1
