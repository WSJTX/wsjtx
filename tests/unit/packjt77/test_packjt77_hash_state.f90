program test_packjt77_hash_state

  use packjt77
  use packjt77_test_helpers
  implicit none

  integer :: ntests
  external :: fillhashvar

  ntests=0

  call expect_standard_hash_save_contract()
  call expect_var_hash_save_contract()
  call expect_var_thread_rx_accumulation()
  call expect_var_tx_rx_separation()
  call expect_mycall_dxcall_substitutions()

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

    call reset_all_state('N0AAA','N0BBB')

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

    call reset_all_state('K1ABC','N0BBB')
    c13='             '
    c13='K1ABC'
    call save_hash_call(c13,n10,n12,n22)
    call assert_call('standard mycall calls10 excluded',calls10(n10),'')
    call assert_call('standard mycall calls12 excluded',calls12(n12),'')
    call assert_call('standard mycall calls22 retained',calls22(1),'K1ABC')

    call reset_all_state('N0AAA','N0BBB')
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

    call reset_all_state('N0AAA','N0BBB')

    c13='             '
    c13='<PJ4/K1ABC>'
    call save_hash_mycallvar(c13,n10,n12,n22)
    call assert_int('var rx n10',346,n10)
    call assert_int('var rx n12',1387,n12)
    call assert_int('var rx n22',1420834,n22)
    call assert_call('var rx calls10',calls10var(n10),'PJ4/K1ABC')
    call assert_call('var rx calls12',calls12var(n12),'PJ4/K1ABC')
    call assert_int('var rx nzhash',1,nzhashvar)
    call assert_int('var rx ihash22(1)',n22,ihash22var(1))
    call assert_call('var rx calls22(1)',calls22var(1),'PJ4/K1ABC')

    call reset_all_state('N0AAA','N0BBB')
    c13='             '
    c13='<PJ4/K1ABC>'
    call save_hash_txcallvar(c13,n10,n12,n22,.false.)
    call assert_int('var tx disabled count',0,nztxhashvar)
    call assert_call('var tx disabled calls12',txcalls12var(1387),'')

    call save_hash_txcallvar(c13,n10,n12,n22,.true.)
    call assert_int('var tx n10',346,n10)
    call assert_int('var tx n12',1387,n12)
    call assert_int('var tx n22',1420834,n22)
    call assert_call('var tx calls10',txcalls10var(n10),'PJ4/K1ABC')
    call assert_call('var tx calls12',txcalls12var(n12),'PJ4/K1ABC')
    call assert_int('var tx nzhash',1,nztxhashvar)
    call assert_int('var tx ihash22(1)',n22,itxhash22var(1))
    call assert_call('var tx calls22(1)',txcalls22var(1),'PJ4/K1ABC')

    call reset_all_state('N0AAA','N0BBB')
    c13='             '
    c13='<W7ABC>'
    call save_hash_callvar(c13,3)
    slot=nthrindexvar(3)+1
    call assert_int('var thread 3 count',1,nlast_callsvar(3))
    call assert_int('var thread 2 count',0,nlast_callsvar(2))
    call assert_int('var thread 4 count',0,nlast_callsvar(4))
    call assert_call('var thread 3 call',last_callsvar(slot),'W7ABC')
    call assert_call('var thread call not folded',calls12var(ihashcallvar('W7ABC        ',12)),'')

    ntests=ntests+1
  end subroutine expect_var_hash_save_contract

  subroutine expect_var_thread_rx_accumulation()
    character(len=13) :: rx_call, later_thread_call
    integer :: n10, n12, n22
    integer :: later_n10, later_n12

    call reset_all_state('N0AAA','N0BBB')

    call normalize_call('W7ABC',rx_call)
    call normalize_call('K9XYZ',later_thread_call)

    ! save_hash_callvar queues RX hashes per thread until fillhashvar folds them.
    call save_hash_callvar(rx_call,3)
    call save_hash_callvar(later_thread_call,7)

    n10=ihashcallvar(rx_call,10)
    n12=ihashcallvar(rx_call,12)
    n22=ihashcallvar(rx_call,22)
    later_n10=ihashcallvar(later_thread_call,10)
    later_n12=ihashcallvar(later_thread_call,12)

    call assert_int('var thread 3 count before fold',1,nlast_callsvar(3))
    call assert_int('var thread 7 count before fold',1,nlast_callsvar(7))
    call assert_int('var thread 2 count before fold',0,nlast_callsvar(2))
    call assert_int('var thread 4 count before fold',0,nlast_callsvar(4))
    call assert_call('var thread 3 first call',last_callsvar(nthrindexvar(3)+1), &
         'W7ABC')
    call assert_call('var thread 7 first call',last_callsvar(nthrindexvar(7)+1), &
         'K9XYZ')
    call assert_call('var thread 2 first slot untouched', &
         last_callsvar(nthrindexvar(2)+1),'')
    call assert_call('var thread 4 first slot untouched', &
         last_callsvar(nthrindexvar(4)+1),'')
    call assert_call('var rx calls10 before fold',calls10var(n10),'')
    call assert_call('var rx calls12 before fold',calls12var(n12),'')
    call assert_int('var rx nzhash before fold',0,nzhashvar)

    call fillhashvar(3,.true.)

    call assert_call('var rx calls10 after fold',calls10var(n10),'W7ABC')
    call assert_call('var rx calls12 after fold',calls12var(n12),'W7ABC')
    call assert_int('var rx nzhash after fold',1,nzhashvar)
    call assert_int('var rx ihash22 after fold',n22,ihash22var(1))
    call assert_call('var rx calls22 after fold',calls22var(1),'W7ABC')
    call assert_call('var later thread not folded calls10',calls10var(later_n10),'')
    call assert_call('var later thread not folded calls12',calls12var(later_n12),'')
    call assert_int('var thread 3 count after fold',1,nlast_callsvar(3))
    call assert_int('var thread 7 count after fold',1,nlast_callsvar(7))
    call assert_call('var thread 7 call retained after fold', &
         last_callsvar(nthrindexvar(7)+1),'K9XYZ')

    ntests=ntests+1
  end subroutine expect_var_thread_rx_accumulation

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
    n10a=ihashcallvar(call12,10)
    n12a=ihashcallvar(call12,12)
    n10b=ihashcallvar(call22,10)
    n12b=ihashcallvar(call22,12)

    call reset_all_state('N0AAA','N0BBB')
    ! ntxhash populates TX lookup tables only; normal RX threads must not see them.
    call pack_var_current_state(input,5,0,1,c77)

    call assert_call('var tx calls10 first hashed call',txcalls10var(n10a), &
         'W3CCX')
    call assert_call('var tx calls12 first hashed call',txcalls12var(n12a), &
         'W3CCX')
    call assert_call('var tx calls10 second hashed call',txcalls10var(n10b), &
         'K1JT/P')
    call assert_call('var tx calls12 second hashed call',txcalls12var(n12b), &
         'K1JT/P')
    call assert_int('var tx nzhash after pack',2,nztxhashvar)
    call assert_true('var tx hash22 second hashed call', &
         var_tx_hash22_contains(call22))
    call assert_call('var rx calls10 first remains empty',calls10var(n10a),'')
    call assert_call('var rx calls12 first remains empty',calls12var(n12a),'')
    call assert_call('var rx calls10 second remains empty',calls10var(n10b),'')
    call assert_call('var rx calls12 second remains empty',calls12var(n12b),'')
    call assert_int('var rx nzhash after tx pack',0,nzhashvar)

    call unpack_var_current_state_thread(c77,1,26,decoded,ok)
    call assert_decode('var tx-state decode',input,input,decoded,ok)
    call unpack_var_current_state_thread(c77,1,1,decoded,ok)
    call assert_decode('var rx-state ignores tx',input, &
         '<...> <...> 590001 FN20QI',decoded,ok)

    call reset_all_state('N0AAA','N0BBB')
    call pack_var_current_state(input,5,0,0,c77)

    call assert_int('var tx nzhash disabled',0,nztxhashvar)
    call assert_call('var tx disabled calls10 first',txcalls10var(n10a),'')
    call assert_call('var tx disabled calls12 first',txcalls12var(n12a),'')
    call assert_call('var tx disabled calls10 second',txcalls10var(n10b),'')
    call assert_call('var tx disabled calls12 second',txcalls12var(n12b),'')
    call assert_true('var tx disabled hash22 empty', &
         .not.var_tx_hash22_contains(call22))

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
    call reset_all_state('PJ4/K1ABC','N0BBB')
    call unpack_standard_current_state(c77,1,decoded_standard,ok_standard)
    call reset_all_state('PJ4/K1ABC','N0BBB')
    call unpack_var_current_state(c77,1,decoded_var,ok_var)
    call assert_decode('standard Type 1 mycall hash22','<PJ4/K1ABC> W9XYZ RR73', &
         '<PJ4/K1ABC> W9XYZ RR73',decoded_standard,ok_standard)
    call assert_decode('var Type 1 mycall hash22','<PJ4/K1ABC> W9XYZ RR73', &
         '<PJ4/K1ABC> W9XYZ RR73',decoded_var,ok_var)

    call pack_and_assert_type('K1ABC RR73; W9XYZ <KH1/KH7Z> -12',0,1,c77)
    call reset_all_state('N0AAA','KH1/KH7Z')
    call unpack_standard_current_state(c77,1,decoded_standard,ok_standard)
    call reset_all_state('N0AAA','KH1/KH7Z')
    call unpack_var_current_state(c77,1,decoded_var,ok_var)
    call assert_decode('standard DXpedition dxcall hash10', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12',decoded_standard,ok_standard)
    call assert_decode('var DXpedition dxcall hash10', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12',decoded_var,ok_var)

    call reset_all_state('KH1/KH7Z','N0BBB')
    call unpack_standard_current_state(c77,0,decoded_standard,ok_standard)
    call reset_all_state('KH1/KH7Z','N0BBB')
    call unpack_var_current_state(c77,0,decoded_var,ok_var)
    call assert_decode('standard DXpedition mycall hash10', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12',decoded_standard,ok_standard)
    call assert_decode('var DXpedition mycall hash10', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12',decoded_var,ok_var)

    call pack_and_assert_type('<W3CCX> <K1JT/P> 590001 FN20QI',5,0,c77)
    call reset_all_state('W3CCX','N0BBB')
    call unpack_standard_current_state(c77,1,decoded_standard,ok_standard)
    call reset_all_state('W3CCX','N0BBB')
    call unpack_var_current_state(c77,1,decoded_var,ok_var)
    call assert_decode('standard Type 5 mycall hash12', &
         '<W3CCX> <K1JT/P> 590001 FN20QI', &
         '<W3CCX> <...> 590001 FN20QI',decoded_standard,ok_standard)
    call assert_decode('var Type 5 mycall hash12', &
         '<W3CCX> <K1JT/P> 590001 FN20QI', &
         '<W3CCX> <...> 590001 FN20QI',decoded_var,ok_var)

    ntests=ntests+1
  end subroutine expect_mycall_dxcall_substitutions

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

    call prime_standard_width(width1,call1)
    call prime_standard_width(width2,call2)
    call prime_standard_width(width3,call3)
    call unpack_standard_current_state(c77,1,decoded_standard,ok_standard)

    call prime_var_width(width1,call1)
    call prime_var_width(width2,call2)
    call prime_var_width(width3,call3)
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

    call reset_all_state('N0AAA','N0BBB')
    packed_input='                                     '
    packed_input=input
    got_i3=-1
    got_n3=-1
    c77=''
    call pack77(packed_input,got_i3,got_n3,c77)
    call assert_message_type('pack77',input,want_i3,want_n3,got_i3,got_n3)
  end subroutine pack_and_assert_type

  subroutine decode_standard(c77,nrx,decoded,ok)
    character(len=77), intent(in) :: c77
    integer, intent(in) :: nrx
    character(len=37), intent(out) :: decoded
    logical, intent(out) :: ok

    call reset_all_state('N0AAA','N0BBB')
    call unpack_standard_current_state(c77,nrx,decoded,ok)
  end subroutine decode_standard

  subroutine decode_var(c77,nrx,decoded,ok)
    character(len=77), intent(in) :: c77
    integer, intent(in) :: nrx
    character(len=37), intent(out) :: decoded
    logical, intent(out) :: ok

    call reset_all_state('N0AAA','N0BBB')
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
    call unpack77var(c77,nrx,decoded,ok,nthr)
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
    call pack77var(packed_input,got_i3,got_n3,c77,ntxhash)
    call assert_message_type('pack77var',input,want_i3,want_n3,got_i3,got_n3)
  end subroutine pack_var_current_state

  subroutine prime_standard_width(width,callsign)
    integer, intent(in) :: width
    character(len=*), intent(in) :: callsign
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
       write(*,1010) width
1010   format('Unsupported standard hash width ',i0)
       error stop 1
    endif
  end subroutine prime_standard_width

  subroutine prime_var_width(width,callsign)
    integer, intent(in) :: width
    character(len=*), intent(in) :: callsign
    character(len=13) :: c13
    integer :: n

    if(width.le.0 .or. len_trim(callsign).le.0) return
    call normalize_call(callsign,c13)
    if(len_trim(c13).le.0) return
    n=ihashcallvar(c13,width)
    if(width.eq.10) then
       calls10var(n)=c13
    else if(width.eq.12) then
       calls12var(n)=c13
    else if(width.eq.22) then
       if(nzhashvar.lt.MAXHASHVAR) nzhashvar=nzhashvar+1
       ihash22var(nzhashvar)=n
       calls22var(nzhashvar)=c13
    else
       write(*,1020) width
1020   format('Unsupported var hash width ',i0)
       error stop 1
    endif
  end subroutine prime_var_width

  subroutine reset_all_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    call clear_all_state(mycall,dxcall)
  end subroutine reset_all_state

  logical function var_tx_hash22_contains(callsign)
    character(len=*), intent(in) :: callsign
    character(len=13) :: c13
    integer :: n22

    call normalize_call(callsign,c13)
    n22=ihashcallvar(c13,22)
    var_tx_hash22_contains=any(itxhash22var(1:nztxhashvar).eq.n22)
  end function var_tx_hash22_contains

end program test_packjt77_hash_state
