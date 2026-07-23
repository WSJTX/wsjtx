program test_packjt77_hash_state

  use packjt77
  use packjt77_test_helpers
  use ft8_mod1, only : mycall, hiscall, lhound, mybcall, hisbcall, hisgrid4
  implicit none

  integer :: ntests
  external :: fillhashvar
  external :: ft8apsetvar
  external :: genft8

  ntests=0

  call expect_standard_hash_save_contract()
  call expect_var_hash_save_contract()
  call expect_var_hash_save_thread_bounds()
  call expect_var_thread_queue_capacity()
  call expect_var_thread_rx_accumulation()
  call expect_type4_literal_call_staged_until_fill()
  call expect_configured_type12_addressee_staged_until_fill()
  call expect_legacy_type12_addressee_not_hashed()
  call expect_service_unpack_does_not_consume_dx_edge()
  call expect_configured_call_clear_blanks_stale_state()
  call expect_dxpedition_dxcall_reentry_substitution()
  call expect_shared_hash_tables_survive_mode_sync()
  call expect_default_pack77_tx_hash_recording()
  call expect_pack77_record_tx_hashes_option_suppresses_commit()
  call expect_pack77_encode_rejects_dx_macro_by_default()
  call expect_pack77_free_text_fallback_rejects_unexpanded_dx_macro()
  call expect_pack_unpack_hash_state_matches_pack_only()
  call expect_genft8_failure_state()
  call expect_dx_macro_option_uses_explicit_base()
  call expect_failed_pack77_candidates_do_not_record_hashes()
  call expect_ft8apsetvar_does_not_record_template_hashes()
  call expect_var_tx_rx_separation()
  call expect_mycall_dxcall_substitutions()
  call expect_invalid_standard_call_decode_failure()

  call expect_hash_resolution_case( &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, &
       'K1ABC RR73; W9XYZ <...> -12', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
       10, 'KH1/KH7Z', 0, '', 0, '')
  call expect_hash_resolution_case( &
       '<PJ4/K1ABC> W9XYZ RR73', 1, 0, &
       '<...> W9XYZ RR73', &
       '<PJ4/K1ABC> W9XYZ RR73', &
       22, 'PJ4/K1ABC', 0, '', 0, '')
  call expect_hash_resolution_case( &
       'PJ2/W1AW <W7ABC> RR73', 4, 0, &
       'PJ2/W1AW <...> RR73', &
       'PJ2/W1AW <W7ABC> RR73', &
       12, 'W7ABC', 0, '', 0, '')
  call expect_hash_resolution_case( &
       '<W3CCX> <K1JT/P> 590001 FN20QI', 5, 0, &
       '<...> <...> 590001 FN20QI', &
       '<W3CCX> <K1JT/P> 590001 FN20QI', &
       12, 'W3CCX', 22, 'K1JT/P', 0, '')

  write(*,1000) ntests
1000 format('packjt77 hash-state tests passed: ',i0)

contains

  subroutine expect_standard_hash_save_contract()
    character(len=13) :: c13
    integer :: n10, n12, n22

    call clear_all_state('N0AAA','N0BBB')

    c13='             '
    c13='<PJ4/K1ABC>'
    call save_hash_call(c13,n10,n12,n22)
    call assert_int('standard n10',346,n10)
    call assert_int('standard n12',1387,n12)
    call assert_int('standard n22',1420834,n22)
    call assert_call('standard calls10',calls10(n10),'PJ4/K1ABC')
    call assert_call('standard calls12',calls12(n12),'PJ4/K1ABC')
    call assert_int('standard nzhash',1,nzhash)
    call assert_int('standard ihash22(1)',n22,ihash22(1))
    call assert_call('standard calls22(1)',calls22(1),'PJ4/K1ABC')

    call save_hash_call(c13,n10,n12,n22)
    call assert_int('standard duplicate nzhash',1,nzhash)
    call assert_call('standard duplicate calls22(1)',calls22(1),'PJ4/K1ABC')

    call clear_all_state('K1ABC','N0BBB')
    c13='             '
    c13='K1ABC'
    call save_hash_call(c13,n10,n12,n22)
    call assert_call('standard mycall calls10 excluded',calls10(n10),'')
    call assert_call('standard mycall calls12 excluded',calls12(n12),'')
    call assert_call('standard mycall calls22 retained',calls22(1),'K1ABC')

    call clear_all_state('N0AAA','N0BBB')
    n10=-99
    n12=-99
    n22=-99
    c13='             '
    c13='<...>'
    call save_hash_call(c13,n10,n12,n22)
    call assert_int('standard placeholder ignored nzhash',0,nzhash)
    call assert_int('standard placeholder ignored n10',-99,n10)

    c13='             '
    c13='K1'
    call save_hash_call(c13,n10,n12,n22)
    call assert_int('standard short call ignored nzhash',0,nzhash)

    ntests=ntests+1
  end subroutine expect_standard_hash_save_contract

  subroutine expect_var_hash_save_contract()
    character(len=13) :: c13
    integer :: n10, n12, n22
    integer :: slot

    call clear_all_state('N0AAA','N0BBB')

    c13='             '
    c13='<PJ4/K1ABC>'
    call save_hash_call(c13,n10,n12,n22)
    call assert_int('var rx n10',346,n10)
    call assert_int('var rx n12',1387,n12)
    call assert_int('var rx n22',1420834,n22)
    call assert_call('var rx calls10',calls10(n10),'PJ4/K1ABC')
    call assert_call('var rx calls12',calls12(n12),'PJ4/K1ABC')
    call assert_int('var rx nzhash',1,nzhash)
    call assert_int('var rx ihash22(1)',n22,ihash22(1))
    call assert_call('var rx calls22(1)',calls22(1),'PJ4/K1ABC')

    call clear_all_state('N0AAA','N0BBB')
    c13='             '
    c13='<PJ4/K1ABC>'
    n10=ihashcall('PJ4/K1ABC    ',10)
    n12=ihashcall('PJ4/K1ABC    ',12)
    n22=ihashcall('PJ4/K1ABC    ',22)
    call assert_int('var tx disabled count',0,nzhash)
    call assert_call('var tx disabled calls12',calls12(1387),'')

    call save_hash_call(c13,n10,n12,n22)
    call assert_int('var tx n10',346,n10)
    call assert_int('var tx n12',1387,n12)
    call assert_int('var tx n22',1420834,n22)
    call assert_call('var tx calls10',calls10(n10),'PJ4/K1ABC')
    call assert_call('var tx calls12',calls12(n12),'PJ4/K1ABC')
    call assert_int('var tx nzhash',1,nzhash)
    call assert_int('var tx ihash22(1)',n22,ihash22(1))
    call assert_call('var tx calls22(1)',calls22(1),'PJ4/K1ABC')

    call clear_all_state('N0AAA','N0BBB')
    c13='             '
    c13='<W7ABC>'
    call queue_hash_call_for_thread(c13,3)
    slot=thread_call_index(3)+1
    call assert_int('var thread 3 count',1,nqueued_calls_by_thread(3))
    call assert_int('var thread 2 count',0,nqueued_calls_by_thread(2))
    call assert_int('var thread 4 count',0,nqueued_calls_by_thread(4))
    call assert_call('var thread 3 call',queued_calls_by_thread(slot),'W7ABC')
    call assert_call('var thread call not folded',calls12(ihashcall('W7ABC        ',12)),'')

    ntests=ntests+1
  end subroutine expect_var_hash_save_contract

  subroutine expect_var_hash_save_thread_bounds()
    character(len=13) :: c13

    call clear_all_state('N0AAA','N0BBB')
    call normalize_call('W7ABC',c13)

    call queue_hash_call_for_thread(c13,0)
    call queue_hash_call_for_thread(c13,-1)
    call queue_hash_call_for_thread(c13,25)

    call assert_true('var invalid thread indices ignored', &
         all(nqueued_calls_by_thread(1:24).eq.0))
    call assert_call('var first queued call untouched',queued_calls_by_thread(1),'')

    ntests=ntests+1
  end subroutine expect_var_hash_save_thread_bounds

  subroutine expect_var_thread_queue_capacity()
    character(len=13) :: c13, last_call, overflow_call
    integer :: capacity, i, nthr

    call clear_all_state('N0AAA','N0BBB')

    nthr=24
    capacity=thread_call_index(nthr+1)-thread_call_index(nthr)
    do i=1,capacity
       c13='             '
       write(c13,'("K0",i2.2)') i
       call queue_hash_call_for_thread(c13,nthr)
    enddo

    overflow_call='             '
    write(overflow_call,'("K0",i2.2)') 99
    call queue_hash_call_for_thread(overflow_call,nthr)

    last_call='             '
    write(last_call,'("K0",i2.2)') capacity
    call assert_int('var thread capacity count',capacity, &
         nqueued_calls_by_thread(nthr))
    call assert_call('var thread capacity last call', &
         queued_calls_by_thread(thread_call_index(nthr)+capacity),last_call)
    call assert_true('var thread capacity overflow dropped', &
         .not.any(queued_calls_by_thread.eq.overflow_call))
    call assert_int('var thread capacity nzhash before fold',0,nzhash)

    call fillhashvar(nthr,.true.)

    call assert_int('var thread capacity nzhash after fold',capacity,nzhash)
    call assert_true('var thread capacity last hash after fold', &
         shared_hash22_contains(last_call))
    call assert_true('var thread capacity overflow absent after fold', &
         .not.shared_hash22_contains(overflow_call))

    ntests=ntests+1
  end subroutine expect_var_thread_queue_capacity

  subroutine expect_var_thread_rx_accumulation()
    character(len=13) :: rx_call, later_thread_call
    integer :: n10, n12, n22
    integer :: later_n10, later_n12

    call clear_all_state('N0AAA','N0BBB')

    call normalize_call('W7ABC',rx_call)
    call normalize_call('K9XYZ',later_thread_call)

    ! queue_hash_call_for_thread queues RX hashes per thread until fillhashvar folds them.
    call queue_hash_call_for_thread(rx_call,3)
    call queue_hash_call_for_thread(later_thread_call,7)

    n10=ihashcall(rx_call,10)
    n12=ihashcall(rx_call,12)
    n22=ihashcall(rx_call,22)
    later_n10=ihashcall(later_thread_call,10)
    later_n12=ihashcall(later_thread_call,12)

    call assert_int('var thread 3 count before fold',1,nqueued_calls_by_thread(3))
    call assert_int('var thread 7 count before fold',1,nqueued_calls_by_thread(7))
    call assert_int('var thread 2 count before fold',0,nqueued_calls_by_thread(2))
    call assert_int('var thread 4 count before fold',0,nqueued_calls_by_thread(4))
    call assert_call('var thread 3 first call',queued_calls_by_thread(thread_call_index(3)+1), &
         'W7ABC')
    call assert_call('var thread 7 first call',queued_calls_by_thread(thread_call_index(7)+1), &
         'K9XYZ')
    call assert_call('var thread 2 first slot untouched', &
         queued_calls_by_thread(thread_call_index(2)+1),'')
    call assert_call('var thread 4 first slot untouched', &
         queued_calls_by_thread(thread_call_index(4)+1),'')
    call assert_call('var rx calls10 before fold',calls10(n10),'')
    call assert_call('var rx calls12 before fold',calls12(n12),'')
    call assert_int('var rx nzhash before fold',0,nzhash)

    call fillhashvar(3,.true.)

    call assert_call('var rx calls10 after fold',calls10(n10),'W7ABC')
    call assert_call('var rx calls12 after fold',calls12(n12),'W7ABC')
    call assert_int('var rx nzhash after fold',1,nzhash)
    call assert_int('var rx ihash22 after fold',n22,ihash22(1))
    call assert_call('var rx calls22 after fold',calls22(1),'W7ABC')
    call assert_call('var later thread not folded calls10',calls10(later_n10),'')
    call assert_call('var later thread not folded calls12',calls12(later_n12),'')
    call assert_int('var thread 3 count after fold',1,nqueued_calls_by_thread(3))
    call assert_int('var thread 7 count after fold',1,nqueued_calls_by_thread(7))
    call assert_call('var thread 7 call retained after fold', &
         queued_calls_by_thread(thread_call_index(7)+1),'K9XYZ')

    ntests=ntests+1
  end subroutine expect_var_thread_rx_accumulation

  subroutine expect_type4_literal_call_staged_until_fill()
    character(len=77) :: c77
    character(len=37) :: decoded
    character(len=13) :: literal_call
    integer :: n10, n12
    logical :: ok

    call pack_and_assert_type('PJ2/W1AW <W7ABC> RR73',4,0,c77)
    call clear_all_state('N0AAA','N0BBB')
    call normalize_call('PJ2/W1AW',literal_call)
    n10=ihashcall(literal_call,10)
    n12=ihashcall(literal_call,12)

    call unpack_var_current_state_thread(c77,1,3,decoded,ok)

    call assert_decode('type 4 staged literal decode', &
         'PJ2/W1AW <W7ABC> RR73','PJ2/W1AW <...> RR73',decoded,ok)
    call assert_int('type 4 thread 3 queued count',1,nqueued_calls_by_thread(3))
    call assert_call('type 4 thread 3 queued call', &
         queued_calls_by_thread(thread_call_index(3)+1),'PJ2/W1AW')
    call assert_int('type 4 thread 3 queued recent count',1, &
         nqueued_recent_calls_by_thread(3))
    call assert_call('type 4 thread 3 queued recent call', &
         queued_recent_calls_by_thread(thread_call_index(3)+1),'PJ2/W1AW')
    call assert_call('type 4 calls10 before fill',calls10(n10),'')
    call assert_call('type 4 calls12 before fill',calls12(n12),'')
    call assert_call('type 4 recent before fill',recent_calls(1),'')

    call fillhashvar(3,.true.)

    call assert_call('type 4 calls10 after fill',calls10(n10),'PJ2/W1AW')
    call assert_call('type 4 calls12 after fill',calls12(n12),'PJ2/W1AW')
    call assert_true('type 4 calls22 after fill',shared_hash22_contains(literal_call))
    call assert_call('type 4 recent after fill',recent_calls(1),'PJ2/W1AW')

    ntests=ntests+1
  end subroutine expect_type4_literal_call_staged_until_fill

  subroutine expect_configured_type12_addressee_staged_until_fill()
    character(len=77) :: c77
    character(len=37) :: decoded
    character(len=13) :: call1, call2
    logical :: ok

    call pack_and_assert_type('W9XYZ K1ABC FN42',1,0,c77)
    call clear_all_state('N0AAA','N0BBB')
    call normalize_call('W9XYZ',call1)
    call normalize_call('K1ABC',call2)

    call unpack_var_current_state_thread(c77,1,3,decoded,ok)

    call assert_decode('type 1 staged addressee decode', &
         'W9XYZ K1ABC FN42','W9XYZ K1ABC FN42',decoded,ok)
    call assert_int('type 1 thread 3 queued hash count',2, &
         nqueued_calls_by_thread(3))
    call assert_call('type 1 thread 3 queued first call', &
         queued_calls_by_thread(thread_call_index(3)+1),'W9XYZ')
    call assert_call('type 1 thread 3 queued second call', &
         queued_calls_by_thread(thread_call_index(3)+2),'K1ABC')
    call assert_true('type 1 addressee hash absent before fill', &
         .not.shared_hash22_contains(call1))

    call fillhashvar(3,.true.)

    call assert_true('type 1 addressee hash after fill', &
         shared_hash22_contains(call1))
    call assert_true('type 1 second call hash after fill', &
         shared_hash22_contains(call2))

    ntests=ntests+1
  end subroutine expect_configured_type12_addressee_staged_until_fill

  subroutine expect_legacy_type12_addressee_not_hashed()
    character(len=77) :: c77
    character(len=37) :: decoded
    character(len=13) :: call1, call2
    integer :: n10a, n12a, n10b, n12b
    logical :: ok

    call pack_and_assert_type('W9XYZ K1ABC FN42',1,0,c77)
    call clear_all_state('N0AAA','N0BBB')
    call normalize_call('W9XYZ',call1)
    call normalize_call('K1ABC',call2)
    n10a=ihashcall(call1,10)
    n12a=ihashcall(call1,12)
    n10b=ihashcall(call2,10)
    n12b=ihashcall(call2,12)

    call unpack_standard_current_state(c77,1,decoded,ok)

    call assert_decode('legacy type 1 addressee decode', &
         'W9XYZ K1ABC FN42','W9XYZ K1ABC FN42',decoded,ok)
    call assert_call('legacy type 1 recent sender',recent_calls(1),'K1ABC')
    call assert_call('legacy type 1 recent addressee',recent_calls(2),'W9XYZ')
    call assert_call('legacy type 1 addressee calls10',calls10(n10a),'')
    call assert_call('legacy type 1 addressee calls12',calls12(n12a),'')
    call assert_true('legacy type 1 addressee hash22 absent', &
         .not.shared_hash22_contains(call1))
    call assert_call('legacy type 1 sender calls10',calls10(n10b),'K1ABC')
    call assert_call('legacy type 1 sender calls12',calls12(n12b),'K1ABC')
    call assert_true('legacy type 1 sender hash22 present', &
         shared_hash22_contains(call2))

    ntests=ntests+1
  end subroutine expect_legacy_type12_addressee_not_hashed

  subroutine expect_service_unpack_does_not_consume_dx_edge()
    character(len=77) :: c77
    character(len=37) :: decoded
    logical :: ok

    call pack_and_assert_type('K1ABC W9XYZ RR73',1,0,c77)
    call clear_all_state('N0AAA','')

    mycall='N0AAA'
    hiscall='KH1/KH7Z'
    dxcall13='KH1/KH7Z     '
    dxcall13_configured_prev='             '
    dxcall13_configured='             '
    dxcall13_configured_set=.false.
    hashdx10_configured=-1

    decoded='                                     '
    ok=.false.
    call unpack77_configured(c77,1,decoded,ok, &
         unpack77_options(record_hashes=.false.,record_recent_calls=.false.))

    call assert_true('service unpack leaves recent queue empty', &
         all(nqueued_recent_calls_by_thread(1:24).eq.0))
    call assert_call('service unpack leaves recent calls empty',recent_calls(1),'')

    call fillhashvar(1,.false.)

    call assert_int('service unpack leaves dx edge queued count',1, &
         nqueued_calls_by_thread(1))
    call assert_call('service unpack leaves dx edge queued call', &
         queued_calls_by_thread(thread_call_index(1)+1),'KH1/KH7Z')
    call assert_true('service unpack dx configured set',dxcall13_configured_set)
    call assert_call('service unpack dx configured',dxcall13_configured,'KH1/KH7Z')
    call assert_call('service unpack dx configured prev',dxcall13_configured_prev, &
         'KH1/KH7Z')
    call assert_int('service unpack dx configured hash', &
         ihashcall(dxcall13_configured,10),hashdx10_configured)

    ntests=ntests+1
  end subroutine expect_service_unpack_does_not_consume_dx_edge

  subroutine expect_configured_call_clear_blanks_stale_state()
    integer :: old_hash

    call clear_all_state('N0AAA','KH1/KH7Z')
    mycall='N0AAA'
    hiscall='KH1/KH7Z'
    call fillhashvar(1,.false.)

    call assert_true('configured clear seeded dx',dxcall13_configured_set)
    call assert_call('configured clear seeded dx call',dxcall13_configured, &
         'KH1/KH7Z')
    old_hash=hashdx10_configured

    hiscall=''
    call fillhashvar(1,.false.)

    call assert_true('configured clear dx flag',.not.dxcall13_configured_set)
    call assert_call('configured clear dx configured blank',dxcall13_configured,'')
    call assert_call('configured clear dx previous blank',dxcall13_configured_prev,'')
    call assert_int('configured clear dx hash reset',-1,hashdx10_configured)
    call assert_int('configured clear dx queue count',0,nqueued_calls_by_thread(1))
    call assert_true('configured clear changed dx hash',hashdx10_configured.ne.old_hash)

    call clear_all_state('KH1/KH7Z','N0BBB')
    mycall='KH1/KH7Z'
    hiscall='N0BBB'
    call fillhashvar(1,.false.)

    call assert_true('configured clear seeded mycall',mycall13_configured_set)
    call assert_call('configured clear seeded mycall value',mycall13_configured, &
         'KH1/KH7Z')

    mycall=''
    call fillhashvar(1,.false.)

    call assert_true('configured clear mycall flag',.not.mycall13_configured_set)
    call assert_call('configured clear mycall configured blank', &
         mycall13_configured,'')
    call assert_call('configured clear mycall previous blank', &
         mycall13_configured_prev,'')
    call assert_int('configured clear mycall hash10 reset',-1,hashmy10_configured)
    call assert_int('configured clear mycall hash12 reset',-1,hashmy12_configured)
    call assert_int('configured clear mycall hash22 reset',-1,hashmy22_configured)

    ntests=ntests+1
  end subroutine expect_configured_call_clear_blanks_stale_state

  subroutine expect_dxpedition_dxcall_reentry_substitution()
    character(len=77) :: c77
    character(len=37) :: decoded
    logical :: ok

    call pack_and_assert_type('K1ABC RR73; W9XYZ <KH1/KH7Z> -12',0,1,c77)
    call clear_all_state('N0AAA','')

    mycall='N0AAA'
    hiscall='KH1/KH7Z'
    call fillhashvar(1,.false.)
    call unpack_var_current_state(c77,1,decoded,ok)
    call assert_decode('DXpedition dx configured first entry', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12',decoded,ok)

    hiscall=''
    call fillhashvar(1,.false.)
    call assert_true('DXpedition dx cleared',.not.dxcall13_configured_set)

    hiscall='KH1/KH7Z'
    call fillhashvar(1,.false.)
    call unpack_var_current_state(c77,1,decoded,ok)
    call assert_decode('DXpedition dx configured re-entry', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12',decoded,ok)

    ntests=ntests+1
  end subroutine expect_dxpedition_dxcall_reentry_substitution

  subroutine expect_shared_hash_tables_survive_mode_sync()
    character(len=13) :: c13
    integer :: n10, n12, n22

    call clear_all_state('N0AAA','KH1/KH7Z')

    call normalize_call('PJ4/K1ABC',c13)
    call save_hash_call(c13,n10,n12,n22)
    call assert_call('persistent hash calls10 seeded',calls10(n10),'PJ4/K1ABC')
    call assert_true('persistent hash calls22 seeded',shared_hash22_contains(c13))

    mycall='W1AW'
    hiscall='W9XYZ'
    call fillhashvar(1,.false.)

    call assert_call('persistent hash calls10 after mode sync', &
         calls10(n10),'PJ4/K1ABC')
    call assert_call('persistent hash calls12 after mode sync', &
         calls12(n12),'PJ4/K1ABC')
    call assert_true('persistent hash calls22 after mode sync', &
         shared_hash22_contains(c13))

    ntests=ntests+1
  end subroutine expect_shared_hash_tables_survive_mode_sync

  subroutine expect_default_pack77_tx_hash_recording()
    character(len=37) :: input, packed_input, decoded
    character(len=77) :: c77
    character(len=13) :: call1, call2, call12, call22, forced_call
    integer :: n10a, n12a, n10b, n12b, got_i3, got_n3
    integer :: forced_n10, forced_n12, forced_n22
    logical :: ok

    input='K1ABC W9XYZ RR73'
    packed_input='                                     '
    packed_input=input
    call normalize_call('K1ABC',call1)
    call normalize_call('W9XYZ',call2)
    n10a=ihashcall(call1,10)
    n12a=ihashcall(call1,12)
    n10b=ihashcall(call2,10)
    n12b=ihashcall(call2,12)

    call clear_all_state('N0AAA','N0BBB')
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(packed_input)
      call assert_pack77_result('default Type 1 tx hashes',packed_input,encoded)
      got_i3=encoded%i3
      got_n3=encoded%n3
    end block
    call assert_message_type('default Type 1 tx hashes',input,1,0,got_i3,got_n3)
    call assert_call('default Type 1 calls10 first call',calls10(n10a),'K1ABC')
    call assert_call('default Type 1 calls12 first call',calls12(n12a),'K1ABC')
    call assert_call('default Type 1 calls10 second call',calls10(n10b),'W9XYZ')
    call assert_call('default Type 1 calls12 second call',calls12(n12b),'W9XYZ')
    call assert_int('default Type 1 nzhash after pack',2,nzhash)
    call assert_true('default Type 1 hash22 first call',shared_hash22_contains(call1))
    call assert_true('default Type 1 hash22 second call',shared_hash22_contains(call2))

    input='PJ2/W1AW <W7ABC> RR73'
    packed_input='                                     '
    packed_input=input
    call normalize_call('W7ABC',call12)
    n10a=ihashcall(call12,10)
    n12a=ihashcall(call12,12)

    call clear_all_state('N0AAA','N0BBB')
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(packed_input)
      call assert_pack77_result('default Type 4 tx hashes',packed_input,encoded)
      got_i3=encoded%i3
      got_n3=encoded%n3
    end block
    call assert_message_type('default Type 4 tx hashes',input,4,0,got_i3,got_n3)
    call assert_call('default Type 4 calls10 hashed call',calls10(n10a),'W7ABC')
    call assert_call('default Type 4 calls12 hashed call',calls12(n12a),'W7ABC')
    call assert_int('default Type 4 nzhash after pack',1,nzhash)
    call assert_true('default Type 4 hash22 hashed call',shared_hash22_contains(call12))

    input='<W3CCX> <K1JT/P> 590001 FN20QI'
    packed_input='                                     '
    packed_input=input
    call normalize_call('W3CCX',call12)
    call normalize_call('K1JT/P',call22)
    n10a=ihashcall(call12,10)
    n12a=ihashcall(call12,12)
    n10b=ihashcall(call22,10)
    n12b=ihashcall(call22,12)

    call clear_all_state('N0AAA','N0BBB')
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(packed_input)
      call assert_pack77_result('default pack77 tx hashes',packed_input,encoded)
      got_i3=encoded%i3
      got_n3=encoded%n3
      c77=encoded%c77
    end block
    call assert_message_type('default pack77 tx hashes',input,5,0,got_i3,got_n3)
    call assert_call('default tx calls10 first hashed call',calls10(n10a), &
         'W3CCX')
    call assert_call('default tx calls12 first hashed call',calls12(n12a), &
         'W3CCX')
    call assert_call('default tx calls10 second hashed call',calls10(n10b), &
         'K1JT/P')
    call assert_call('default tx calls12 second hashed call',calls12(n12b), &
         'K1JT/P')
    call assert_int('default tx nzhash after pack',2,nzhash)
    call assert_true('default tx hash22 second hashed call', &
         shared_hash22_contains(call22))

    decoded='                                     '
    ok=.false.
    call unpack77(c77,0,decoded,ok)
    call assert_decode('default tx decode uses recorded hashes',input,input,decoded,ok)

    input='<PJ4/K1ABC> FK52AB'
    packed_input='                                     '
    packed_input=input
    call normalize_call('PJ4/K1ABC',forced_call)
    forced_n10=ihashcall(forced_call,10)
    forced_n12=ihashcall(forced_call,12)
    forced_n22=ihashcall(forced_call,22)

    call clear_all_state('N0AAA','N0BBB')
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(packed_input,pack77_options(prefer_wspr_50bit=.true.))
      call assert_pack77_result('prefer WSPR tx hashes',packed_input,encoded)
      got_i3=encoded%i3
      got_n3=encoded%n3
      c77=encoded%c77
    end block
    call assert_message_type('prefer WSPR tx hashes',input,0,6,got_i3,got_n3)
    call assert_call('prefer WSPR calls10',calls10(forced_n10),'PJ4/K1ABC')
    call assert_call('prefer WSPR calls12',calls12(forced_n12),'PJ4/K1ABC')
    call assert_int('prefer WSPR hash22',forced_n22,ihash22(1))
    call assert_call('prefer WSPR calls22',calls22(1),'PJ4/K1ABC')

    decoded='                                     '
    ok=.false.
    call unpack77(c77,0,decoded,ok)
    call assert_decode('prefer WSPR decode uses recorded hashes',input,input,decoded,ok)

    ntests=ntests+1
  end subroutine expect_default_pack77_tx_hash_recording

  subroutine expect_pack77_record_tx_hashes_option_suppresses_commit()
    character(len=37) :: input, packed_input

    input='K1ABC W9XYZ RR73'
    call assert_record_tx_disabled_same_payload(input,1,0)

    input='PJ2/W1AW <W7ABC> RR73'
    call assert_record_tx_disabled_same_payload(input,4,0)

    input='<W3CCX> <K1JT/P> 590001 FN20QI'
    call assert_record_tx_disabled_same_payload(input,5,0)

    input='<PJ4/K1ABC> FK52AB'
    packed_input='                                     '
    packed_input=input
    call clear_all_state('N0AAA','N0BBB')
    block
      type(pack77_result) :: default_encoded, disabled_encoded
      default_encoded=pack77_result_from_api(packed_input,pack77_options(prefer_wspr_50bit=.true.))
      call assert_pack77_result('default preferred WSPR payload',packed_input, &
           default_encoded)
      call clear_all_state('N0AAA','N0BBB')
      disabled_encoded=pack77_result_from_api(packed_input, &
           pack77_options(prefer_wspr_50bit=.true.,record_tx_hashes=.false.))
      call assert_pack77_result('disabled preferred WSPR payload',packed_input, &
           disabled_encoded)
      call assert_text_equal('disabled preferred WSPR c77',default_encoded%c77, &
           disabled_encoded%c77)
      call assert_message_type('disabled preferred WSPR type',input,0,6, &
           disabled_encoded%i3,disabled_encoded%n3)
    end block
    call assert_shared_hash_tables_empty('disabled preferred WSPR hashes')

    ntests=ntests+1
  end subroutine expect_pack77_record_tx_hashes_option_suppresses_commit

  subroutine expect_pack77_encode_rejects_dx_macro_by_default()
    character(len=37) :: shorthand

    shorthand='$DX K1ABC FN42'
    call clear_all_state('N0AAA','N0BBB')

    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(shorthand)
      call assert_true('pure Pack77 encode rejects DX macro', &
           .not.encoded%encoded)
    end block

    ntests=ntests+1
  end subroutine expect_pack77_encode_rejects_dx_macro_by_default

  subroutine expect_pack77_free_text_fallback_rejects_unexpanded_dx_macro()
    character(len=37) :: input, direct

    direct='W9XYZ K1ABC FN42'
    call clear_all_state('N0AAA','N0BBB')

    input='$DX K1ABC FN42'
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(input)
      call assert_true('Pack77 free-text fallback rejects DX macro', &
           .not.encoded%encoded)
    end block

    input='$dx K1ABC FN42'
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(input)
      call assert_true('Pack77 free-text fallback rejects lowercase DX macro', &
           .not.encoded%encoded)
    end block

    input='   $DX K1ABC FN42'
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(input)
      call assert_true('Pack77 free-text fallback rejects leading-space DX macro', &
           .not.encoded%encoded)
    end block

    input='$DXCALL K1ABC FN42'
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(input)
      call assert_true('Pack77 free-text fallback rejects DXCALL macro', &
           .not.encoded%encoded)
    end block

    input='$dxcall K1ABC FN42'
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(input)
      call assert_true('Pack77 free-text fallback rejects lowercase DXCALL macro', &
           .not.encoded%encoded)
    end block

    input='$DXFOO K1ABC FN42'
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(input)
      call assert_true('Pack77 free-text fallback rejects invalid DX prefix', &
           .not.encoded%encoded)
      call assert_true('Pack77 free-text fallback invalid DX prefix status', &
           encoded%status.eq.PACK77_STATUS_FREE_TEXT_INVALID)
    end block

    input='$DX K1ABC FN42'
    block
      type(pack77_result) :: shorthand_encoded, direct_encoded
      shorthand_encoded=pack77_result_from_api(input, &
           pack77_options(expand_dx_macro=.true.,dx_macro_base='W9XYZ'))
      direct_encoded=pack77_result_from_api(direct)
      call assert_pack77_result('Pack77 free-text fallback explicit DX expansion',input, &
           shorthand_encoded)
      call assert_text_equal('Pack77 free-text fallback explicit DX expansion c77', &
           direct_encoded%c77,shorthand_encoded%c77)
    end block

    ntests=ntests+1
  end subroutine expect_pack77_free_text_fallback_rejects_unexpanded_dx_macro

  subroutine expect_pack_unpack_hash_state_matches_pack_only()
    call assert_pack_unpack_hash_state_matches_pack_only( &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12')
    call assert_pack_unpack_hash_state_matches_pack_only('W1AW/P K1A 1A CT')
    call assert_pack_unpack_hash_state_matches_pack_only('PJ2/W1AW <W7ABC> RR73')
    call assert_pack_unpack_hash_state_matches_pack_only( &
         '<W3CCX> <K1JT/P> 590001 FN20QI')

    ntests=ntests+1
  end subroutine expect_pack_unpack_hash_state_matches_pack_only

  subroutine expect_genft8_failure_state()
    character(len=37) :: input, msgsent
    integer*1 :: msgbits(77)
    integer :: itone(79)
    integer :: i3,n3

    input='$DX K1ABC FN42'
    call genft8(input,i3,n3,msgsent,msgbits,itone)

    call assert_int('genft8 failure i3',-1,i3)
    call assert_int('genft8 failure n3',-1,n3)
    call assert_text_equal('genft8 failure msgsent', &
         '*** bad message ***                  ',msgsent)
    call assert_true('genft8 failure msgbits',all(msgbits.eq.0))
    call assert_true('genft8 failure itone',all(itone.eq.0))

    ntests=ntests+1
  end subroutine expect_genft8_failure_state

  subroutine expect_dx_macro_option_uses_explicit_base()
    character(len=37) :: shorthand, direct, decoded
    logical :: ok

    shorthand='$DX K1ABC FN42'
    direct='W9XYZ K1ABC FN42'
    call clear_all_state('N0AAA','N0BBB')

    block
      type(pack77_result) :: shorthand_encoded, direct_encoded
      shorthand_encoded=pack77_result_from_api(shorthand, &
           pack77_options(expand_dx_macro=.true.,dx_macro_base='W9XYZ'))
      direct_encoded=pack77_result_from_api(direct)
      call assert_pack77_result('DX rewrite shorthand',shorthand, &
           shorthand_encoded)
      call assert_pack77_result('DX rewrite direct',direct,direct_encoded)
      call assert_message_type('DX rewrite type',shorthand,1,0, &
           shorthand_encoded%i3,shorthand_encoded%n3)
      call assert_text_equal('DX rewrite c77',direct_encoded%c77, &
           shorthand_encoded%c77)

      decoded='                                     '
      ok=.false.
      call unpack77(shorthand_encoded%c77,0,decoded,ok)
      call assert_decode('DX rewrite decode',shorthand,direct,decoded,ok)
    end block

    shorthand='$DXCALL K1ABC FN42'
    block
      type(pack77_result) :: shorthand_encoded, direct_encoded
      shorthand_encoded=pack77_result_from_api(shorthand, &
           pack77_options(expand_dx_macro=.true.,dx_macro_base='W9XYZ'))
      direct_encoded=pack77_result_from_api(direct)
      call assert_pack77_result('DXCALL rewrite shorthand',shorthand, &
           shorthand_encoded)
      call assert_text_equal('DXCALL rewrite c77',direct_encoded%c77, &
           shorthand_encoded%c77)
    end block

    shorthand='$dx K1ABC FN42'
    block
      type(pack77_result) :: shorthand_encoded, direct_encoded
      shorthand_encoded=pack77_result_from_api(shorthand, &
           pack77_options(expand_dx_macro=.true.,dx_macro_base='W9XYZ'))
      direct_encoded=pack77_result_from_api(direct)
      call assert_pack77_result('lowercase DX rewrite shorthand',shorthand, &
           shorthand_encoded)
      call assert_text_equal('lowercase DX rewrite c77',direct_encoded%c77, &
           shorthand_encoded%c77)
    end block

    shorthand='   $DX K1ABC FN42'
    block
      type(pack77_result) :: shorthand_encoded, direct_encoded
      shorthand_encoded=pack77_result_from_api(shorthand, &
           pack77_options(expand_dx_macro=.true.,dx_macro_base='W9XYZ'))
      direct_encoded=pack77_result_from_api(direct)
      call assert_pack77_result('DX rewrite leading whitespace',shorthand, &
           shorthand_encoded)
      call assert_text_equal('DX rewrite leading whitespace c77', &
           direct_encoded%c77,shorthand_encoded%c77)
    end block

    ntests=ntests+1
  end subroutine expect_dx_macro_option_uses_explicit_base

  subroutine expect_failed_pack77_candidates_do_not_record_hashes()
    call assert_rejected_candidate_does_not_record('W9XYZ K1ABC R BAD MA')
    call assert_rejected_candidate_does_not_record('<PJ2/W1AW> K1ABC BOGUS')
    call assert_rejected_candidate_does_not_record('CQ PJ2/W1AW BOGUS')
    call assert_rejected_candidate_does_not_record('<PJ4/K1ABC>X W9XYZ RR73')
    call assert_rejected_candidate_does_not_record( &
         'K1ABC RR73; W9XYZ <KH1/KH7Z>X -12')
    call assert_rejected_candidate_does_not_record( &
         '<W3CCX <K1JT/P> 590001 FN20QI')
    call assert_rejected_candidate_does_not_record( &
         '<W3CCX> <K1JT/P 590001 FN20QI')

    ntests=ntests+1
  end subroutine expect_failed_pack77_candidates_do_not_record_hashes

  subroutine expect_ft8apsetvar_does_not_record_template_hashes()
    logical(1) :: lmycallstd, lhiscallstd

    call clear_all_state('N0AAA','PJ2/W1AW')
    mycall='N0AAA       '
    hiscall='PJ2/W1AW    '
    mybcall='N0AAA       '
    hisbcall='PJ2/W1AW    '
    hisgrid4='    '
    lhound=.false.
    lmycallstd=.true.
    lhiscallstd=.false.

    call ft8apsetvar(lmycallstd,lhiscallstd,1)
    call assert_shared_hash_tables_empty('ft8apsetvar template hashes')

    ntests=ntests+1
  end subroutine expect_ft8apsetvar_does_not_record_template_hashes

  subroutine expect_var_tx_rx_separation()
    character(len=37) :: input
    character(len=77) :: c77
    character(len=37) :: decoded
    character(len=13) :: call12, call22
    integer :: n10a, n12a, n10b, n12b
    logical :: ok

    input='<W3CCX> <K1JT/P> 590001 FN20QI'
    call normalize_call('W3CCX',call12)
    call normalize_call('K1JT/P',call22)
    n10a=ihashcall(call12,10)
    n12a=ihashcall(call12,12)
    n10b=ihashcall(call22,10)
    n12b=ihashcall(call22,12)

    call clear_all_state('N0AAA','N0BBB')
    ! record_tx_hashes writes accepted pack hashes into the shared lookup table.
    call pack_var_current_state(input,5,0,1,c77)

    call assert_call('var tx calls10 first hashed call',calls10(n10a), &
         'W3CCX')
    call assert_call('var tx calls12 first hashed call',calls12(n12a), &
         'W3CCX')
    call assert_call('var tx calls10 second hashed call',calls10(n10b), &
         'K1JT/P')
    call assert_call('var tx calls12 second hashed call',calls12(n12b), &
         'K1JT/P')
    call assert_int('var tx nzhash after pack',2,nzhash)
    call assert_true('var tx hash22 second hashed call', &
         shared_hash22_contains(call22))

    call unpack_var_current_state_thread(c77,1,26,decoded,ok)
    call assert_decode('var tx-state decode',input,input,decoded,ok)
    call unpack_var_current_state_thread(c77,1,1,decoded,ok)
    call assert_decode('var shared-state decodes tx hashes',input,input,decoded,ok)

    call clear_all_state('N0AAA','N0BBB')
    call pack_var_current_state(input,5,0,0,c77)

    call assert_int('var tx nzhash disabled',0,nzhash)
    call assert_call('var tx disabled calls10 first',calls10(n10a),'')
    call assert_call('var tx disabled calls12 first',calls12(n12a),'')
    call assert_call('var tx disabled calls10 second',calls10(n10b),'')
    call assert_call('var tx disabled calls12 second',calls12(n12b),'')
    call assert_true('var tx disabled hash22 empty', &
         .not.shared_hash22_contains(call22))

    call unpack_var_current_state_thread(c77,1,26,decoded,ok)
    call assert_decode('var tx-state disabled decode',input, &
         '<...> <...> 590001 FN20QI',decoded,ok)

    ntests=ntests+1
  end subroutine expect_var_tx_rx_separation

  subroutine expect_mycall_dxcall_substitutions()
    character(len=77) :: c77
    character(len=37) :: decoded_standard, decoded_var
    logical :: ok_standard, ok_var

    ! These decode paths substitute local my/dx calls without hash-table priming.
    call pack_and_assert_type('<PJ4/K1ABC> W9XYZ RR73',1,0,c77)
    call clear_all_state('PJ4/K1ABC','N0BBB')
    call unpack_standard_current_state(c77,1,decoded_standard,ok_standard)
    call clear_all_state('PJ4/K1ABC','N0BBB')
    call unpack_var_current_state(c77,1,decoded_var,ok_var)
    call assert_decode('standard Type 1 mycall hash22','<PJ4/K1ABC> W9XYZ RR73', &
         '<PJ4/K1ABC> W9XYZ RR73',decoded_standard,ok_standard)
    call assert_decode('var Type 1 mycall hash22','<PJ4/K1ABC> W9XYZ RR73', &
         '<PJ4/K1ABC> W9XYZ RR73',decoded_var,ok_var)

    call pack_and_assert_type('K1ABC RR73; W9XYZ <KH1/KH7Z> -12',0,1,c77)
    call clear_all_state('N0AAA','KH1/KH7Z')
    call unpack_standard_current_state(c77,1,decoded_standard,ok_standard)
    call clear_all_state('N0AAA','KH1/KH7Z')
    call unpack_var_current_state(c77,1,decoded_var,ok_var)
    call assert_decode('standard DXpedition dxcall hash10', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12',decoded_standard,ok_standard)
    call assert_decode('var DXpedition dxcall hash10', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12',decoded_var,ok_var)

    call clear_all_state('KH1/KH7Z','N0BBB')
    call unpack_standard_current_state(c77,0,decoded_standard,ok_standard)
    call clear_all_state('KH1/KH7Z','N0BBB')
    call unpack_var_current_state(c77,0,decoded_var,ok_var)
    call assert_decode('standard DXpedition mycall hash10', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12',decoded_standard,ok_standard)
    call assert_decode('var DXpedition mycall hash10', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12',decoded_var,ok_var)

    call pack_and_assert_type('<W3CCX> <K1JT/P> 590001 FN20QI',5,0,c77)
    call clear_all_state('W3CCX','N0BBB')
    call unpack_standard_current_state(c77,1,decoded_standard,ok_standard)
    call clear_all_state('W3CCX','N0BBB')
    call unpack_var_current_state(c77,1,decoded_var,ok_var)
    call assert_decode('standard Type 5 mycall hash12', &
         '<W3CCX> <K1JT/P> 590001 FN20QI', &
         '<W3CCX> <...> 590001 FN20QI',decoded_standard,ok_standard)
    call assert_decode('var Type 5 mycall hash12', &
         '<W3CCX> <K1JT/P> 590001 FN20QI', &
         '<W3CCX> <...> 590001 FN20QI',decoded_var,ok_var)

    ntests=ntests+1
  end subroutine expect_mycall_dxcall_substitutions

  subroutine expect_invalid_standard_call_decode_failure()
    character(len=13) :: decoded_standard, decoded_var
    integer, parameter :: n28_q1abc = 11395945
    logical :: ok_standard, ok_var

    call unpack28(n28_q1abc,decoded_standard,ok_standard)
    call unpack28(n28_q1abc,decoded_var,ok_var)

    call assert_true('standard invalid raw call fails',.not.ok_standard)
    call assert_true('var invalid raw call fails',.not.ok_var)

    ntests=ntests+1
  end subroutine expect_invalid_standard_call_decode_failure

  subroutine expect_hash_resolution_case(input,want_i3,want_n3,blank_expected, &
       primed_expected,width1,call1,width2,call2,width3,call3)
    character(len=*), intent(in) :: input, blank_expected, primed_expected
    character(len=*), intent(in) :: call1, call2, call3
    integer, intent(in) :: want_i3, want_n3, width1, width2, width3
    character(len=77) :: c77
    character(len=37) :: decoded_standard, decoded_var
    logical :: ok_standard, ok_var

    call pack_and_assert_type(input,want_i3,want_n3,c77)

    call decode_standard(c77,1,decoded_standard,ok_standard)
    call decode_var(c77,1,decoded_var,ok_var)
    call assert_decode('standard blank',input,blank_expected,decoded_standard,ok_standard)
    call assert_decode('var blank',input,blank_expected,decoded_var,ok_var)
    call assert_text_equal('blank standard/var',decoded_standard,decoded_var)

    call prime_hash_width(width1,call1,'standard')
    call prime_hash_width(width2,call2,'standard')
    call prime_hash_width(width3,call3,'standard')
    call unpack_standard_current_state(c77,1,decoded_standard,ok_standard)

    call prime_hash_width(width1,call1,'var')
    call prime_hash_width(width2,call2,'var')
    call prime_hash_width(width3,call3,'var')
    call unpack_var_current_state(c77,1,decoded_var,ok_var)

    call assert_decode('standard primed',input,primed_expected,decoded_standard,ok_standard)
    call assert_decode('var primed',input,primed_expected,decoded_var,ok_var)
    call assert_text_equal('primed standard/var',decoded_standard,decoded_var)

    ntests=ntests+1
  end subroutine expect_hash_resolution_case

  subroutine pack_and_assert_type(input,want_i3,want_n3,c77)
    character(len=*), intent(in) :: input
    integer, intent(in) :: want_i3, want_n3
    character(len=77), intent(out) :: c77
    character(len=37) :: packed_input
    integer :: got_i3, got_n3

    call clear_all_state('N0AAA','N0BBB')
    packed_input='                                     '
    packed_input=input
    got_i3=-1
    got_n3=-1
    c77=''
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(packed_input)
      call assert_pack77_result('pack77',packed_input,encoded)
      got_i3=encoded%i3
      got_n3=encoded%n3
      c77=encoded%c77
    end block
    call assert_message_type('pack77',input,want_i3,want_n3,got_i3,got_n3)
  end subroutine pack_and_assert_type

  subroutine decode_standard(c77,nrx,decoded,ok)
    character(len=77), intent(in) :: c77
    integer, intent(in) :: nrx
    character(len=37), intent(out) :: decoded
    logical, intent(out) :: ok

    call clear_all_state('N0AAA','N0BBB')
    call unpack_standard_current_state(c77,nrx,decoded,ok)
  end subroutine decode_standard

  subroutine decode_var(c77,nrx,decoded,ok)
    character(len=77), intent(in) :: c77
    integer, intent(in) :: nrx
    character(len=37), intent(out) :: decoded
    logical, intent(out) :: ok

    call clear_all_state('N0AAA','N0BBB')
    call unpack_var_current_state(c77,nrx,decoded,ok)
  end subroutine decode_var

  subroutine unpack_standard_current_state(c77,nrx,decoded,ok)
    character(len=77), intent(in) :: c77
    integer, intent(in) :: nrx
    character(len=37), intent(out) :: decoded
    logical, intent(out) :: ok

    decoded='                                     '
    ok=.false.
    call unpack77(c77,nrx,decoded,ok)
  end subroutine unpack_standard_current_state

  subroutine unpack_var_current_state(c77,nrx,decoded,ok)
    character(len=77), intent(in) :: c77
    integer, intent(in) :: nrx
    character(len=37), intent(out) :: decoded
    logical, intent(out) :: ok

    call unpack_var_current_state_thread(c77,nrx,1,decoded,ok)
  end subroutine unpack_var_current_state

  subroutine unpack_var_current_state_thread(c77,nrx,nthr,decoded,ok)
    character(len=77), intent(in) :: c77
    integer, intent(in) :: nrx, nthr
    character(len=37), intent(out) :: decoded
    logical, intent(out) :: ok

    decoded='                                     '
    ok=.false.
    call unpack77_configured(c77,nrx,decoded,ok,unpack77_options(thread_index=nthr))
  end subroutine unpack_var_current_state_thread

  subroutine pack_var_current_state(input,want_i3,want_n3,ntxhash,c77)
    character(len=*), intent(in) :: input
    integer, intent(in) :: want_i3, want_n3, ntxhash
    character(len=77), intent(out) :: c77
    character(len=37) :: packed_input
    integer :: got_i3, got_n3

    packed_input='                                     '
    packed_input=input
    got_i3=-1
    got_n3=-1
    c77=''
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(packed_input,pack77_options(record_tx_hashes=ntxhash.eq.1))
      call assert_pack77_result('pack77',packed_input,encoded)
      got_i3=encoded%i3
      got_n3=encoded%n3
      c77=encoded%c77
    end block
    call assert_message_type('pack77',input,want_i3,want_n3,got_i3,got_n3)
  end subroutine pack_var_current_state

  subroutine assert_record_tx_disabled_same_payload(input,want_i3,want_n3)
    character(len=*), intent(in) :: input
    integer, intent(in) :: want_i3, want_n3
    character(len=37) :: packed_input

    packed_input='                                     '
    packed_input=input
    call clear_all_state('N0AAA','N0BBB')
    block
      type(pack77_result) :: default_encoded, disabled_encoded
      default_encoded=pack77_result_from_api(packed_input)
      call assert_pack77_result('default record_tx payload',packed_input, &
           default_encoded)
      call clear_all_state('N0AAA','N0BBB')
      disabled_encoded=pack77_result_from_api(packed_input,pack77_options(record_tx_hashes=.false.))
      call assert_pack77_result('disabled record_tx payload',packed_input, &
           disabled_encoded)
      call assert_text_equal('disabled record_tx c77',default_encoded%c77, &
           disabled_encoded%c77)
      call assert_message_type('disabled record_tx type',input,want_i3,want_n3, &
           disabled_encoded%i3,disabled_encoded%n3)
    end block
    call assert_shared_hash_tables_empty('disabled record_tx hashes')
  end subroutine assert_record_tx_disabled_same_payload

  subroutine assert_rejected_candidate_does_not_record(input)
    character(len=*), intent(in) :: input
    character(len=37) :: packed_input

    packed_input='                                     '
    packed_input=input
    call clear_all_state('N0AAA','N0BBB')
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(packed_input)
      if(encoded%encoded) then
        call assert_message_type('rejected candidate fallback type',input,0,0, &
             encoded%i3,encoded%n3)
      endif
    end block
    call assert_shared_hash_tables_empty('rejected candidate hashes')
  end subroutine assert_rejected_candidate_does_not_record

  subroutine assert_pack_unpack_hash_state_matches_pack_only(input)
    character(len=*), intent(in) :: input
    character(len=37) :: packed_input, decoded
    character(len=77) :: c77
    character(len=13) :: calls10_after_unpack(0:1023)
    character(len=13) :: calls12_after_unpack(0:4095)
    character(len=13) :: calls22_after_unpack(1:MAXHASH)
    integer :: ihash22_after_unpack(1:MAXHASH)
    integer :: nzhash_after_unpack
    integer :: i3,n3
    logical :: ok

    packed_input='                                     '
    packed_input=input

    call clear_all_state('N0AAA','N0BBB')
    i3=-1
    n3=-1
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(packed_input)
      i3=encoded%i3
      n3=encoded%n3
      c77=encoded%c77
    end block
    call assert_true('pack/unpack hash-state packed '//trim(input),i3.ge.0)
    decoded='                                     '
    ok=.false.
    call unpack77(c77,0,decoded,ok)
    call assert_true('pack/unpack hash-state unpacked '//trim(input),ok)
    nzhash_after_unpack=nzhash
    calls10_after_unpack=calls10
    calls12_after_unpack=calls12
    calls22_after_unpack=calls22
    ihash22_after_unpack=ihash22

    call clear_all_state('N0AAA','N0BBB')
    i3=-1
    n3=-1
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(packed_input)
      i3=encoded%i3
      n3=encoded%n3
      c77=encoded%c77
    end block
    call assert_true('pack-only hash-state packed '//trim(input),i3.ge.0)

    call assert_int('pack/unpack nzhash '//trim(input),nzhash_after_unpack,nzhash)
    call assert_true('pack/unpack calls10 '//trim(input), &
         all(calls10_after_unpack.eq.calls10))
    call assert_true('pack/unpack calls12 '//trim(input), &
         all(calls12_after_unpack.eq.calls12))
    call assert_true('pack/unpack calls22 '//trim(input), &
         all(calls22_after_unpack.eq.calls22))
    call assert_true('pack/unpack ihash22 '//trim(input), &
         all(ihash22_after_unpack.eq.ihash22))
  end subroutine assert_pack_unpack_hash_state_matches_pack_only

  subroutine prime_hash_width(width,callsign,label)
    integer, intent(in) :: width
    character(len=*), intent(in) :: callsign, label
    character(len=13) :: c13
    integer :: n

    if(width.le.0 .or. len_trim(callsign).le.0) return
    call normalize_call(callsign,c13)
    if(len_trim(c13).le.0) return
    n=ihashcall(c13,width)
    if(width.eq.10) then
       calls10(n)=c13
    else if(width.eq.12) then
       calls12(n)=c13
    else if(width.eq.22) then
       if(nzhash.lt.MAXHASH) nzhash=nzhash+1
       ihash22(nzhash)=n
       calls22(nzhash)=c13
    else
       write(*,1010) trim(label), width
1010   format('Unsupported ',a,' hash width ',i0)
       error stop 1
    endif
  end subroutine prime_hash_width

  logical function shared_hash22_contains(callsign)
    character(len=*), intent(in) :: callsign
    character(len=13) :: c13
    integer :: n22

    call normalize_call(callsign,c13)
    n22=ihashcall(c13,22)
    shared_hash22_contains=any(ihash22(1:nzhash).eq.n22)
  end function shared_hash22_contains

end program test_packjt77_hash_state
