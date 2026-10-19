module ft8_decode_state_test_controls
  implicit none

  ! Direct global controls avoid libgomp's captured stack environment in TSan tests.
  integer, save :: started,reader_ready,checkpoint,checkpoint_seen,finished, &
       reader_finished,reads
  logical, save :: torn_pair,torn_history,qso_failure
end module ft8_decode_state_test_controls

program test_ft8_mtd_decode_state_concurrent
  use omp_lib
  use ft8_decode_state_test_controls
  use ft8_mod1, only : allmessages,allfreq,ndecodes,ft8_message_seen, &
       calldteven,calldtodd,ft8_lookup_call_dt,begin_ft8_qso_attempt, &
       ft8_qso_hints_suppressed,suppress_ft8_qso_hints
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

end program test_ft8_mtd_decode_state_concurrent
