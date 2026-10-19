module ft8_decode_state_test_controls
  implicit none

  ! Direct global controls avoid libgomp's captured stack environment in TSan tests.
  integer, save :: started,reader_ready,checkpoint,checkpoint_seen,finished, &
       reader_finished,reads
  logical, save :: torn_pair,torn_history,qso_failure,torn_hint,torn_qso
end module ft8_decode_state_test_controls

program test_ft8_mtd_decode_state_concurrent
  use omp_lib
  use ft8_decode_state_test_controls
  use ft8_mod1, only : allmessages,allfreq,ndecodes,ft8_message_seen, &
       calldteven,calldtodd,ft8_lookup_call_dt,begin_ft8_qso_attempt, &
       ft8_qso_hints_suppressed,suppress_ft8_qso_hints,even,odd,ft8_period_hint, &
       get_ft8_period_hint,get_ft8_bootstrap_hint,retire_ft8_period_hint, &
       prepare_ft8_hint_history,clear_ft8_hint_history,ft8_qso_snapshot, &
       get_ft8_qso_hint_state,publish_ft8_qso_message,seed_ft8_qso_message, &
       reset_ft8_qso_state,invalidate_ft8_qso_partner,prepare_ft8_qso_history,incall
  implicit none

  interface
     subroutine wsjt_tsan_acquire_ft8_find_dupes() bind(C)
     end subroutine wsjt_tsan_acquire_ft8_find_dupes
     subroutine wsjt_tsan_release_ft8_find_dupes() bind(C)
     end subroutine wsjt_tsan_release_ft8_find_dupes
  end interface

  if(omp_get_max_threads().lt.2) error stop 'two OpenMP workers are required'
  call test_duplicate_queries()
  call test_coherent_duplicate_queries()
  call test_history_queries()
  call test_coherent_history_queries()
  call test_qso_suppression()
  call test_concurrent_qso_suppression()
  call test_period_hints()
  call test_bootstrap_hints()
  call test_concurrent_hint_retirement()
  call test_qso_message_state()
  call test_qso_history_preparation()
  call test_concurrent_qso_publication()

contains

  subroutine test_duplicate_queries()
    allmessages=''
    allfreq=0.
    ndecodes=0
    if(ft8_message_seen('CQ K1JT FN20',1000.)) error stop &
         'an empty decode list reported a duplicate'

    ndecodes=2
    allmessages(1)='CQ W9XYZ EN37'
    allfreq(1)=1200.
    allmessages(2)='CQ K1JT FN20'
    allfreq(2)=1000.
    if(.not.ft8_message_seen('CQ K1JT FN20',1000.)) error stop &
         'a matching decode beyond the first entry was missed'
    if(ft8_message_seen('CQ K1ABC FN20',1000.)) error stop &
         'a different message was reported as a duplicate'
    if(.not.ft8_message_seen('CQ K1JT FN20',1044.5)) error stop &
         'a duplicate below the upper frequency boundary was missed'
    if(.not.ft8_message_seen('CQ K1JT FN20',955.5)) error stop &
         'a duplicate above the lower frequency boundary was missed'
    if(ft8_message_seen('CQ K1JT FN20',1045.)) error stop &
         'the upper 45 Hz boundary was included'
    if(ft8_message_seen('CQ K1JT FN20',955.)) error stop &
         'the lower 45 Hz boundary was included'
    if(ft8_message_seen('CQ K1JT FN20',1045.5)) error stop &
         'a decode outside the upper frequency boundary was included'
    if(ft8_message_seen('CQ K1JT FN20',954.5)) error stop &
         'a decode outside the lower frequency boundary was included'
  end subroutine test_duplicate_queries

  subroutine test_coherent_duplicate_queries()
    integer, parameter :: writer_iterations=20000,minimum_reads=40000
    integer :: worker,iteration,status,checkpoint_status

    allmessages=''
    allfreq=0.
    ndecodes=1
!$omp atomic write seq_cst
    started=0
!$omp atomic write seq_cst
    reader_ready=0
!$omp atomic write seq_cst
    checkpoint=0
!$omp atomic write seq_cst
    checkpoint_seen=0
!$omp atomic write seq_cst
    finished=0
!$omp atomic write seq_cst
    reader_finished=0
    reads=0
    torn_pair=.false.

!$omp parallel num_threads(2) private(worker,iteration,status,checkpoint_status)
    if(omp_get_num_threads().ne.2) error stop 'two OpenMP workers are required for duplicates'
    worker=omp_get_thread_num()
    if(worker.eq.0) then
       call publish_pair('CQ K1JT FN20',1000.)
!$omp atomic write seq_cst
       started=1
       do
!$omp atomic read seq_cst
          status=reader_ready
          if(status.eq.1) exit
       enddo
       do iteration=1,writer_iterations
          if(mod(iteration,2).eq.0) then
             call publish_pair('CQ K1JT FN20',1000.)
          else
             call publish_pair('CQ W9XYZ EN37',1100.)
          endif
          if(iteration.eq.writer_iterations/2) then
!$omp atomic write seq_cst
             checkpoint=1
             do
!$omp atomic read seq_cst
                status=checkpoint_seen
                if(status.eq.1) exit
             enddo
          endif
       enddo
!$omp atomic write seq_cst
       finished=1
       do
!$omp atomic read seq_cst
          status=reader_finished
          if(status.eq.1) exit
       enddo
    else
       do
!$omp atomic read seq_cst
          status=started
          if(status.eq.1) exit
       enddo
       if(ft8_message_seen('CQ K1JT FN20',1100.)) torn_pair=.true.
       reads=reads+1
!$omp atomic write seq_cst
       reader_ready=1
       do
!$omp atomic read seq_cst
          checkpoint_status=checkpoint
          if(ft8_message_seen('CQ K1JT FN20',1100.)) torn_pair=.true.
          reads=reads+1
          if(checkpoint_status.eq.1) then
!$omp atomic write seq_cst
             checkpoint_seen=1
          endif
!$omp atomic read seq_cst
          status=finished
          if(status.eq.1.and.reads.ge.minimum_reads) exit
       enddo
!$omp atomic write seq_cst
       reader_finished=1
    endif
!$omp end parallel

    if(torn_pair) error stop 'duplicate reader observed a torn message/frequency pair'
    if(checkpoint_seen.ne.1) error stop 'duplicate reader did not overlap publication'
    if(reads.lt.minimum_reads) error stop 'duplicate coherence reader did not finish'
    if(.not.ft8_message_seen('CQ K1JT FN20',1000.)) error stop &
         'the final published pair was not retained'
  end subroutine test_coherent_duplicate_queries

  subroutine publish_pair(message,frequency)
    character(len=*), intent(in) :: message
    real, intent(in) :: frequency

!$omp critical(find_dupes)
    call wsjt_tsan_acquire_ft8_find_dupes()
    ndecodes=1
    allmessages(1)=message
    allfreq(1)=frequency
    call wsjt_tsan_release_ft8_find_dupes()
!$omp end critical(find_dupes)
  end subroutine publish_pair

  subroutine test_history_queries()
    calldteven%call2=''
    calldteven%dt=0.
    calldtodd%call2=''
    calldtodd%dt=0.
    call assert_history_lookup('K1JT',.true.,.false.,.false.,99.)

    calldteven(1)%call2='W9XYZ'
    calldteven(1)%dt=0.25
    calldteven(2)%call2='K1JT'
    calldteven(2)%dt=-0.375
    calldteven(3)%call2='K1JT'
    calldteven(3)%dt=0.75
    calldtodd(1)%call2='K1JT'
    calldtodd(1)%dt=1.25
    calldtodd(2)%call2='W1ABC'
    calldtodd(2)%dt=-0.625
    calldtodd(3)%call2='K1JT'
    calldtodd(3)%dt=-1.25

    call assert_history_lookup('W9XYZ',.true.,.false.,.true.,0.25)
    call assert_history_lookup('K1JT   ',.true.,.false.,.true.,-0.375)
    call assert_history_lookup('K1JT',.false.,.true.,.true.,1.25)
    call assert_history_lookup('W1ABC',.false.,.true.,.true.,-0.625)
    call assert_history_lookup('K1JT',.true.,.true.,.true.,-0.375)
    call assert_history_lookup('W1ABC',.true.,.true.,.false.,99.)
    call assert_history_lookup('K1JT',.false.,.false.,.false.,99.)
    call assert_history_lookup('K9ZZZ',.true.,.false.,.false.,99.)
    call assert_history_lookup('K9ZZZ',.false.,.true.,.false.,99.)
  end subroutine test_history_queries

  subroutine assert_history_lookup(call,even_interval,odd_interval,expected_found,expected_dt)
    character(len=*), intent(in) :: call
    logical, intent(in) :: even_interval,odd_interval,expected_found
    real, intent(in) :: expected_dt
    logical :: found
    real :: dt

    found=.not.expected_found
    dt=99.
    call ft8_lookup_call_dt(call,even_interval,odd_interval,found,dt)
    if(found.neqv.expected_found) error stop 'history lookup returned the wrong match state'
    if(dt.ne.expected_dt) error stop 'history lookup returned the wrong DT or changed a miss'
  end subroutine assert_history_lookup

  subroutine test_coherent_history_queries()
    integer, parameter :: writer_iterations=20000,minimum_reads=40000
    integer :: worker,iteration,status,checkpoint_status

    calldteven%call2=''
    calldteven%dt=0.
    calldtodd%call2=''
    calldtodd%dt=0.
!$omp atomic write seq_cst
    started=0
!$omp atomic write seq_cst
    reader_ready=0
!$omp atomic write seq_cst
    checkpoint=0
!$omp atomic write seq_cst
    checkpoint_seen=0
!$omp atomic write seq_cst
    finished=0
!$omp atomic write seq_cst
    reader_finished=0
    reads=0
    torn_history=.false.

!$omp parallel num_threads(2) private(worker,iteration,status,checkpoint_status)
    if(omp_get_num_threads().ne.2) error stop 'two OpenMP workers are required for history'
    worker=omp_get_thread_num()
    if(worker.eq.0) then
       call publish_history('K1JT',0.25,-0.75)
!$omp atomic write seq_cst
       started=1
       do
!$omp atomic read seq_cst
          status=reader_ready
          if(status.eq.1) exit
       enddo
       do iteration=1,writer_iterations
          if(mod(iteration,2).eq.0) then
             call publish_history('K1JT',0.25,-0.75)
          else
             call publish_history('W9XYZ',-0.5,1.25)
          endif
          if(iteration.eq.writer_iterations/2) then
!$omp atomic write seq_cst
             checkpoint=1
             do
!$omp atomic read seq_cst
                status=checkpoint_seen
                if(status.eq.1) exit
             enddo
          endif
       enddo
!$omp atomic write seq_cst
       finished=1
       do
!$omp atomic read seq_cst
          status=reader_finished
          if(status.eq.1) exit
       enddo
    else
       do
!$omp atomic read seq_cst
          status=started
          if(status.eq.1) exit
       enddo
       call check_history_pairs(torn_history)
       reads=reads+1
!$omp atomic write seq_cst
       reader_ready=1
       do
!$omp atomic read seq_cst
          checkpoint_status=checkpoint
          call check_history_pairs(torn_history)
          reads=reads+1
          if(checkpoint_status.eq.1) then
!$omp atomic write seq_cst
             checkpoint_seen=1
          endif
!$omp atomic read seq_cst
          status=finished
          if(status.eq.1.and.reads.ge.minimum_reads) exit
       enddo
!$omp atomic write seq_cst
       reader_finished=1
    endif
!$omp end parallel

    if(torn_history) error stop 'history reader lost a call or observed a torn call/DT pair'
    if(checkpoint_seen.ne.1) error stop 'history reader did not overlap publication'
    if(reads.lt.minimum_reads) error stop 'history coherence reader did not finish'
    call assert_history_lookup('K1JT',.true.,.false.,.true.,0.25)
    call assert_history_lookup('K1JT',.false.,.true.,.true.,-0.75)
    call assert_history_lookup('W9XYZ',.true.,.false.,.true.,-0.5)
    call assert_history_lookup('W9XYZ',.false.,.true.,.true.,1.25)
  end subroutine test_coherent_history_queries

  subroutine check_history_pairs(torn_history)
    logical, intent(inout) :: torn_history
    logical :: found
    real :: dt

    dt=99.
    call ft8_lookup_call_dt('K1JT',.true.,.false.,found,dt)
    if(.not.found.or.dt.ne.0.25) torn_history=.true.
    dt=99.
    call ft8_lookup_call_dt('K1JT',.false.,.true.,found,dt)
    if(.not.found.or.dt.ne.-0.75) torn_history=.true.
  end subroutine check_history_pairs

  subroutine publish_history(call,even_dt,odd_dt)
    character(len=*), intent(in) :: call
    real, intent(in) :: even_dt,odd_dt

!$omp critical(find_dupes)
    call wsjt_tsan_acquire_ft8_find_dupes()
    calldteven(150:2:-1)=calldteven(149:1:-1)
    calldteven(1)%call2=call
    calldteven(1)%dt=even_dt
    calldtodd(150:2:-1)=calldtodd(149:1:-1)
    calldtodd(1)%call2=call
    calldtodd(1)%dt=odd_dt
    call wsjt_tsan_release_ft8_find_dupes()
!$omp end critical(find_dupes)
  end subroutine publish_history

  subroutine test_qso_suppression()
    call begin_ft8_qso_attempt(.false.)
    if(ft8_qso_hints_suppressed()) error stop 'a new QSO attempt was suppressed'
    call suppress_ft8_qso_hints()
    if(.not.ft8_qso_hints_suppressed()) error stop 'QSO suppression was not retained'
    call suppress_ft8_qso_hints()
    if(.not.ft8_qso_hints_suppressed()) error stop 'repeated QSO suppression cleared the flag'
    call begin_ft8_qso_attempt(.false.)
    if(ft8_qso_hints_suppressed()) error stop 'a new QSO attempt retained old suppression'
    call begin_ft8_qso_attempt(.true.)
    if(.not.ft8_qso_hints_suppressed()) error stop 'initial QSO suppression was lost'
  end subroutine test_qso_suppression

  subroutine test_concurrent_qso_suppression()
    integer, parameter :: writer_iterations=20000,minimum_reads=40000
    integer :: worker,iteration,status,checkpoint_status

    call begin_ft8_qso_attempt(.false.)
!$omp atomic write seq_cst
    started=0
!$omp atomic write seq_cst
    reader_ready=0
!$omp atomic write seq_cst
    checkpoint=0
!$omp atomic write seq_cst
    checkpoint_seen=0
!$omp atomic write seq_cst
    finished=0
!$omp atomic write seq_cst
    reader_finished=0
    reads=0
    qso_failure=.false.

!$omp parallel num_threads(2) private(worker,iteration,status,checkpoint_status)
    if(omp_get_num_threads().ne.2) error stop 'two OpenMP workers are required for QSO state'
    worker=omp_get_thread_num()
    if(worker.eq.0) then
       do
!$omp atomic read seq_cst
          status=reader_ready
          if(status.eq.1) exit
       enddo
       call suppress_ft8_qso_hints()
!$omp atomic write seq_cst
       started=1
       do iteration=1,writer_iterations
          call suppress_ft8_qso_hints()
          if(iteration.eq.writer_iterations/2) then
!$omp atomic write seq_cst
             checkpoint=1
             do
!$omp atomic read seq_cst
                status=checkpoint_seen
                if(status.eq.1) exit
             enddo
          endif
       enddo
!$omp atomic write seq_cst
       finished=1
       do
!$omp atomic read seq_cst
          status=reader_finished
          if(status.eq.1) exit
       enddo
    else
       if(ft8_qso_hints_suppressed()) qso_failure=.true.
!$omp atomic write seq_cst
       reader_ready=1
       do
!$omp atomic read seq_cst
          status=started
          if(status.eq.1) exit
       enddo
       do
!$omp atomic read seq_cst
          checkpoint_status=checkpoint
          if(.not.ft8_qso_hints_suppressed()) qso_failure=.true.
          reads=reads+1
          if(checkpoint_status.eq.1) then
!$omp atomic write seq_cst
             checkpoint_seen=1
          endif
!$omp atomic read seq_cst
          status=finished
          if(status.eq.1.and.reads.ge.minimum_reads) exit
       enddo
!$omp atomic write seq_cst
       reader_finished=1
    endif
!$omp end parallel

    if(qso_failure) error stop 'QSO reader observed an incorrect suppression state'
    if(checkpoint_seen.ne.1) error stop 'QSO reader did not overlap suppression'
    if(reads.lt.minimum_reads) error stop 'QSO suppression reader did not finish'
    if(.not.ft8_qso_hints_suppressed()) error stop 'QSO suppression was lost after workers joined'
  end subroutine test_concurrent_qso_suppression

  subroutine seed_period_hint(parity,entry,message,frequency,dt)
    integer, intent(in) :: parity,entry
    character(len=*), intent(in) :: message
    real, intent(in) :: frequency,dt

    if(parity.eq.1) then
       even(entry)%msg=message
       even(entry)%freq=frequency
       even(entry)%dt=dt
       even(entry)%lstate=.true.
    else
       odd(entry)%msg=message
       odd(entry)%freq=frequency
       odd(entry)%dt=dt
       odd(entry)%lstate=.true.
    endif
  end subroutine seed_period_hint

  subroutine test_period_hints()
    type(ft8_period_hint) :: hint,captured
    integer :: parity

    call clear_ft8_hint_history()
    call get_ft8_period_hint(.true.,.false.,1000.,0.,hint)
    if(hint%valid) error stop 'empty period history returned a hint'
    do parity=1,2
       even%lstate=.false.
       odd%lstate=.false.
       call seed_period_hint(parity,1,'CQ K1JT FN20',1000.,0.)
       call seed_period_hint(parity,2,'DE W1ABC FN42',1000.,0.)
       call seed_period_hint(parity,3,'QRZ W9XYZ EN37',1000.,0.)
       call seed_period_hint(parity,4,'K1ABC W9XYZ -10',1002.,0.125)
       call prepare_ft8_hint_history(15*(parity-1))
       call get_ft8_period_hint(parity.eq.1,parity.eq.2,1000.,0.,hint)
       if(.not.hint%valid.or.hint%index.ne.4.or.hint%parity.ne.parity) error stop &
            'period lookup did not select the last eligible match'
       if(hint%message.ne.'K1ABC W9XYZ -10'.or.hint%frequency.ne.1002..or. &
            hint%dt.ne.0.125.or..not.hint%cq) error stop &
            'period lookup lost the selected tuple or accumulated CQ classification'
       captured=hint
       call retire_ft8_period_hint(hint)
       call retire_ft8_period_hint(hint)
       if(.not.captured%valid.or.captured%message.ne.'K1ABC W9XYZ -10') error stop &
            'retirement changed a previously captured hint'
       call get_ft8_period_hint(parity.eq.1,parity.eq.2,1000.,0.,hint)
       if(.not.hint%valid.or.hint%index.ne.3.or..not.hint%cq) error stop &
            'retirement did not expose the preceding eligible hint'
       call retire_ft8_period_hint(hint)
       call get_ft8_period_hint(parity.eq.1,parity.eq.2,1000.,0.,hint)
       if(.not.hint%valid.or.hint%index.ne.2.or..not.hint%cq) error stop 'DE hint was missed'
       call retire_ft8_period_hint(hint)
       call get_ft8_period_hint(parity.eq.1,parity.eq.2,1000.,0.,hint)
       if(.not.hint%valid.or.hint%index.ne.1.or..not.hint%cq) error stop 'CQ hint was missed'
       call retire_ft8_period_hint(hint)
       call get_ft8_period_hint(parity.eq.1,parity.eq.2,1000.,0.,hint)
       if(hint%valid) error stop 'retired history still returned a hint'

       call seed_period_hint(parity,1,'K1ABC W9XYZ -10',1000.,0.)
       call prepare_ft8_hint_history(15*(parity-1))
       call get_ft8_period_hint(parity.eq.1,parity.eq.2,1002.875,0.1875,hint)
       if(.not.hint%valid.or.hint%cq) error stop 'an eligible non-CQ hint was missed'
       call get_ft8_period_hint(parity.eq.1,parity.eq.2,1003.,0.,hint)
       if(hint%valid) error stop 'the upper 3 Hz period boundary was included'
       call get_ft8_period_hint(parity.eq.1,parity.eq.2,997.,0.,hint)
       if(hint%valid) error stop 'the lower 3 Hz period boundary was included'
       call get_ft8_period_hint(parity.eq.1,parity.eq.2,1000.,0.19,hint)
       if(hint%valid) error stop 'the upper period DT boundary was included'
       call get_ft8_period_hint(parity.eq.1,parity.eq.2,1000.,-0.19,hint)
       if(hint%valid) error stop 'the lower period DT boundary was included'
    enddo

    call clear_ft8_hint_history()
    call seed_period_hint(1,1,'CQ K1JT FN20',1000.,0.)
    call seed_period_hint(2,1,'CQ W9XYZ EN37',1000.,0.)
    call prepare_ft8_hint_history(0)
    call prepare_ft8_hint_history(15)
    call get_ft8_period_hint(.true.,.true.,1000.,0.,hint)
    if(.not.hint%valid.or.hint%parity.ne.1) error stop 'even period precedence was lost'
    call retire_ft8_period_hint(hint)
    call get_ft8_period_hint(.true.,.true.,1000.,0.,hint)
    if(hint%valid) error stop 'an even-period miss incorrectly fell back to odd history'
    call get_ft8_period_hint(.false.,.false.,1000.,0.,hint)
    if(hint%valid) error stop 'a hint was returned without an active period'
    call prepare_ft8_hint_history(-1)
    call get_ft8_period_hint(.false.,.true.,1000.,0.,hint)
    if(hint%valid) error stop 'an invalid period retained copied hints'
    if(any(even%lstate).or.any(odd%lstate)) error stop 'an invalid period retained source hints'
  end subroutine test_period_hints

  subroutine test_bootstrap_hints()
    type(ft8_period_hint) :: hint
    integer :: parity

    call clear_ft8_hint_history()
    do parity=1,2
       even%lstate=.false.
       odd%lstate=.false.
       call seed_period_hint(parity,1,' W9XYZ -10',900.,-0.25)
       call seed_period_hint(parity,2,'K1ABC W9XYZ -10',1000.,0.25)
       call seed_period_hint(parity,3,'K1ABC W9XYZ R-10',1100.,0.5)
       call prepare_ft8_hint_history(15*(parity-1))
       call get_ft8_bootstrap_hint('W9XYZ',parity.eq.1,parity.eq.2,hint)
       if(.not.hint%valid.or.hint%index.ne.2.or.hint%frequency.ne.1000..or. &
            hint%dt.ne.0.25.or.hint%message.ne.'K1ABC W9XYZ -10') error stop &
            'bootstrap did not select the first eligible interior callsign match'
       call retire_ft8_period_hint(hint)
       call get_ft8_bootstrap_hint('W9XYZ',parity.eq.1,parity.eq.2,hint)
       if(.not.hint%valid.or.hint%index.ne.3) error stop 'bootstrap reused a retired hint'
       call get_ft8_bootstrap_hint('W9XY',parity.eq.1,parity.eq.2,hint)
       if(hint%valid) error stop 'bootstrap accepted a partial callsign'
       call seed_period_hint(parity,1,'CQ W9XYZ EN37',900.,-0.25)
       call prepare_ft8_hint_history(15*(parity-1))
       call get_ft8_bootstrap_hint('W9XYZ',parity.eq.1,parity.eq.2,hint)
       if(.not.hint%valid.or.hint%index.ne.1) error stop 'bootstrap excluded the first history entry'
    enddo
    call clear_ft8_hint_history()
    call get_ft8_bootstrap_hint('W9XYZ',.true.,.false.,hint)
    if(hint%valid) error stop 'cleared history retained a bootstrap hint'
  end subroutine test_bootstrap_hints

  subroutine test_concurrent_hint_retirement()
    integer, parameter :: writer_iterations=20000,minimum_reads=40000
    integer :: worker,iteration,status,checkpoint_status,entry
    character(len=37) :: message
    type(ft8_period_hint) :: hint

    call clear_ft8_hint_history()
    even%lstate=.false.
    odd%lstate=.false.
    call seed_period_hint(1,1,'CQ K1JT FN20',1000.+1./1024.,1./1024.)
    do entry=2,130
       write(message,'(a,i3)') 'K1ABC W9XYZ ',entry
       call seed_period_hint(1,entry,message,1000.+real(entry)/1024.,real(entry)/1024.)
    enddo
    call prepare_ft8_hint_history(0)
!$omp atomic write seq_cst
    started=0
!$omp atomic write seq_cst
    reader_ready=0
!$omp atomic write seq_cst
    checkpoint=0
!$omp atomic write seq_cst
    checkpoint_seen=0
!$omp atomic write seq_cst
    finished=0
!$omp atomic write seq_cst
    reader_finished=0
    reads=0
    torn_hint=.false.

!$omp parallel num_threads(2) private(worker,iteration,status,checkpoint_status,hint)
    if(omp_get_num_threads().ne.2) error stop 'two OpenMP workers are required for period hints'
    worker=omp_get_thread_num()
    if(worker.eq.0) then
!$omp atomic write seq_cst
       started=1
       do
!$omp atomic read seq_cst
          status=reader_ready
          if(status.eq.1) exit
       enddo
       call get_ft8_period_hint(.true.,.false.,1000.,0.,hint)
       do iteration=1,writer_iterations
          hint%index=130-mod(iteration-1,129)
          call retire_ft8_period_hint(hint)
          if(iteration.eq.writer_iterations/2) then
!$omp atomic write seq_cst
             checkpoint=1
             do
!$omp atomic read seq_cst
                status=checkpoint_seen
                if(status.eq.1) exit
             enddo
          endif
       enddo
!$omp atomic write seq_cst
       finished=1
       do
!$omp atomic read seq_cst
          status=reader_finished
          if(status.eq.1) exit
       enddo
    else
       do
!$omp atomic read seq_cst
          status=started
          if(status.eq.1) exit
       enddo
       call check_period_hint(torn_hint)
       reads=reads+1
!$omp atomic write seq_cst
       reader_ready=1
       do
!$omp atomic read seq_cst
          checkpoint_status=checkpoint
          call check_period_hint(torn_hint)
          reads=reads+1
          if(checkpoint_status.eq.1) then
!$omp atomic write seq_cst
             checkpoint_seen=1
          endif
!$omp atomic read seq_cst
          status=finished
          if(status.eq.1.and.reads.ge.minimum_reads) exit
       enddo
!$omp atomic write seq_cst
       reader_finished=1
    endif
!$omp end parallel

    if(torn_hint) error stop 'period reader observed an inconsistent captured hint'
    if(checkpoint_seen.ne.1) error stop 'period reader did not overlap retirement'
    if(reads.lt.minimum_reads) error stop 'period hint reader did not finish'
    call get_ft8_period_hint(.true.,.false.,1000.,0.,hint)
    if(.not.hint%valid.or.hint%index.ne.1) error stop 'retirement lost the remaining hint'
    call retire_ft8_period_hint(hint)
    call get_ft8_period_hint(.true.,.false.,1000.,0.,hint)
    if(hint%valid) error stop 'the final retired hint remained eligible'
  end subroutine test_concurrent_hint_retirement

  subroutine check_period_hint(torn_hint)
    logical, intent(inout) :: torn_hint
    type(ft8_period_hint) :: hint
    character(len=37) :: expected_message

    call get_ft8_period_hint(.true.,.false.,1000.,0.,hint)
    if(.not.hint%valid.or.hint%parity.ne.1.or.hint%index.lt.1.or.hint%index.gt.130) then
       torn_hint=.true.
       return
    endif
    if(hint%index.eq.1) then
       expected_message='CQ K1JT FN20'
    else
       write(expected_message,'(a,i3)') 'K1ABC W9XYZ ',hint%index
    endif
    if(hint%message.ne.expected_message.or..not.hint%cq.or. &
         hint%frequency.ne.1000.+real(hint%index)/1024..or.hint%dt.ne.real(hint%index)/1024.) &
         torn_hint=.true.
  end subroutine check_period_hint

  subroutine assert_qso_state(suppressed,valid,message,dt,partner)
    logical, intent(in) :: suppressed,valid
    character(len=*), intent(in) :: message,partner
    real, intent(in) :: dt
    type(ft8_qso_snapshot) :: state

    call get_ft8_qso_hint_state(state)
    if((state%suppressed.neqv.suppressed).or.(state%valid.neqv.valid)) error stop &
         'QSO snapshot returned incorrect suppression or validity'
    if(state%message.ne.message.or.state%dt.ne.dt.or.state%partner.ne.partner) error stop &
         'QSO snapshot returned an incorrect message/DT/partner tuple'
  end subroutine assert_qso_state

  subroutine test_qso_message_state()
    call reset_ft8_qso_state()
    call assert_qso_state(.false.,.false.,'',0.,'')
    call suppress_ft8_qso_hints()
    call assert_qso_state(.true.,.false.,'',0.,'')
    call seed_ft8_qso_message('K1ABC W9XYZ -10',0.25)
    call assert_qso_state(.true.,.true.,'K1ABC W9XYZ -10',0.25,'')
    call seed_ft8_qso_message('K1ABC W1ABC -15',-0.5)
    call assert_qso_state(.true.,.true.,'K1ABC W9XYZ -10',0.25,'')
    call reset_ft8_qso_state()
    call seed_ft8_qso_message('K1ABC W9XYZ -10',0.25)
    call assert_qso_state(.false.,.true.,'K1ABC W9XYZ -10',0.25,'')
    call publish_ft8_qso_message('K1ABC W9XYZ R-10',0.5,'W9XYZ')
    call assert_qso_state(.true.,.true.,'K1ABC W9XYZ R-10',0.5,'W9XYZ')
    call invalidate_ft8_qso_partner('W9XYZ')
    call assert_qso_state(.true.,.true.,'K1ABC W9XYZ R-10',0.5,'W9XYZ')
    call invalidate_ft8_qso_partner('W1ABC')
    call assert_qso_state(.true.,.false.,'',0.,'')
    call seed_ft8_qso_message('K1ABC W1ABC -15',-0.5)
    call assert_qso_state(.true.,.true.,'K1ABC W1ABC -15',-0.5,'W9XYZ')
    call publish_ft8_qso_message('K1ABC W9XYZ W1ABC',0.75,'W9XYZ')
    call invalidate_ft8_qso_partner('W1ABC')
    call assert_qso_state(.true.,.true.,'K1ABC W9XYZ W1ABC',0.75,'W9XYZ')
    call invalidate_ft8_qso_partner('')
    call assert_qso_state(.true.,.false.,'',0.,'')
    call reset_ft8_qso_state()
    call assert_qso_state(.false.,.false.,'',0.,'')
  end subroutine test_qso_message_state

  subroutine test_qso_history_preparation()
    call clear_ft8_hint_history()
    even%lstate=.false.
    odd%lstate=.false.
    incall%msg=''
    incall%xdt=0.
    call seed_period_hint(1,1,'K1ABC W9XYZ RR73',1000.,1.25)
    call seed_period_hint(1,2,'K1ABC W9XYZ 73',1100.,1.5)
    call prepare_ft8_hint_history(0)
    incall(1)%msg='K1ABC W9XYZ -10'
    incall(1)%xdt=0.25
    incall(2)%msg='K1ABC W9XYZ R-10'
    incall(2)%xdt=0.5

    call reset_ft8_qso_state()
    call prepare_ft8_qso_history('W9XYZ','K1ABC',.true.,.true.,.true.,.false.)
    call assert_qso_state(.false.,.true.,'K1ABC W9XYZ -10',0.25,'')
    incall(1)%msg='CQ W1ABC FN42'
    call prepare_ft8_qso_history('W9XYZ','K1ABC',.true.,.true.,.true.,.false.)
    call assert_qso_state(.false.,.true.,'K1ABC W9XYZ -10',0.25,'')

    call reset_ft8_qso_state()
    call prepare_ft8_qso_history('W9XYZ','K1ABC',.true.,.true.,.true.,.false.)
    call assert_qso_state(.false.,.true.,'K1ABC W9XYZ R-10',0.5,'')
    incall(1)%msg=''
    call reset_ft8_qso_state()
    call prepare_ft8_qso_history('W9XYZ','K1ABC',.true.,.true.,.true.,.false.)
    call assert_qso_state(.false.,.true.,'K1ABC W9XYZ RR73',1.25,'')

    call reset_ft8_qso_state()
    call prepare_ft8_qso_history('W9XYZ','K1ABC',.false.,.true.,.true.,.false.)
    call assert_qso_state(.false.,.false.,'',0.,'')
    call prepare_ft8_qso_history('W9XYZ','K1ABC',.true.,.false.,.true.,.false.)
    call assert_qso_state(.false.,.false.,'',0.,'')
    call prepare_ft8_qso_history('','K1ABC',.true.,.true.,.true.,.false.)
    call assert_qso_state(.false.,.false.,'',0.,'')

    call publish_ft8_qso_message('K1ABC W1ABC -15',-0.5,'W1ABC')
    call prepare_ft8_qso_history('W9XYZ','K1ABC',.false.,.true.,.true.,.false.)
    call assert_qso_state(.true.,.false.,'',0.,'')
    call prepare_ft8_qso_history('W9XYZ','K1ABC',.true.,.true.,.true.,.false.)
    call assert_qso_state(.true.,.true.,'K1ABC W9XYZ RR73',1.25,'W1ABC')
    call publish_ft8_qso_message('K1ABC W9XYZ -20',-0.75,'W9XYZ')
    call prepare_ft8_qso_history('W9XYZ','K1ABC',.true.,.true.,.true.,.false.)
    call assert_qso_state(.true.,.true.,'K1ABC W9XYZ -20',-0.75,'W9XYZ')
    call prepare_ft8_qso_history('','K1ABC',.true.,.true.,.true.,.false.)
    call assert_qso_state(.true.,.false.,'',0.,'')
  end subroutine test_qso_history_preparation

  subroutine test_concurrent_qso_publication()
    integer, parameter :: writer_iterations=20000,minimum_reads=40000
    integer :: worker,iteration,status,checkpoint_status

    call reset_ft8_qso_state()
!$omp atomic write seq_cst
    started=0
!$omp atomic write seq_cst
    reader_ready=0
!$omp atomic write seq_cst
    checkpoint=0
!$omp atomic write seq_cst
    checkpoint_seen=0
!$omp atomic write seq_cst
    finished=0
!$omp atomic write seq_cst
    reader_finished=0
    reads=0
    torn_qso=.false.

!$omp parallel num_threads(2) private(worker,iteration,status,checkpoint_status)
    if(omp_get_num_threads().ne.2) error stop 'two OpenMP workers are required for QSO publication'
    worker=omp_get_thread_num()
    if(worker.eq.0) then
       call publish_ft8_qso_message('K1ABC W9XYZ -10',0.25,'W9XYZ')
!$omp atomic write seq_cst
       started=1
       do
!$omp atomic read seq_cst
          status=reader_ready
          if(status.eq.1) exit
       enddo
       do iteration=1,writer_iterations
          if(mod(iteration,2).eq.0) then
             call publish_ft8_qso_message('K1ABC W9XYZ -10',0.25,'W9XYZ')
          else
             call publish_ft8_qso_message('K1ABC W1ABC R-15',-0.5,'W1ABC')
          endif
          if(iteration.eq.writer_iterations/2) then
!$omp atomic write seq_cst
             checkpoint=1
             do
!$omp atomic read seq_cst
                status=checkpoint_seen
                if(status.eq.1) exit
             enddo
          endif
       enddo
!$omp atomic write seq_cst
       finished=1
       do
!$omp atomic read seq_cst
          status=reader_finished
          if(status.eq.1) exit
       enddo
    else
       do
!$omp atomic read seq_cst
          status=started
          if(status.eq.1) exit
       enddo
       call check_qso_tuple(torn_qso)
       reads=reads+1
!$omp atomic write seq_cst
       reader_ready=1
       do
!$omp atomic read seq_cst
          checkpoint_status=checkpoint
          call check_qso_tuple(torn_qso)
          reads=reads+1
          if(checkpoint_status.eq.1) then
!$omp atomic write seq_cst
             checkpoint_seen=1
          endif
!$omp atomic read seq_cst
          status=finished
          if(status.eq.1.and.reads.ge.minimum_reads) exit
       enddo
!$omp atomic write seq_cst
       reader_finished=1
    endif
!$omp end parallel

    if(torn_qso) error stop 'QSO reader observed a torn message/DT/partner/suppression tuple'
    if(checkpoint_seen.ne.1) error stop 'QSO reader did not overlap publication'
    if(reads.lt.minimum_reads) error stop 'QSO tuple reader did not finish'
    call assert_qso_state(.true.,.true.,'K1ABC W9XYZ -10',0.25,'W9XYZ')
  end subroutine test_concurrent_qso_publication

  subroutine check_qso_tuple(torn_qso)
    logical, intent(inout) :: torn_qso
    type(ft8_qso_snapshot) :: state

    call get_ft8_qso_hint_state(state)
    if(.not.state%valid.or..not.state%suppressed) torn_qso=.true.
    if(state%message.eq.'K1ABC W9XYZ -10') then
       if(state%dt.ne.0.25.or.state%partner.ne.'W9XYZ') torn_qso=.true.
    else if(state%message.eq.'K1ABC W1ABC R-15') then
       if(state%dt.ne.-0.5.or.state%partner.ne.'W1ABC') torn_qso=.true.
    else
       torn_qso=.true.
    endif
  end subroutine check_qso_tuple

end program test_ft8_mtd_decode_state_concurrent
