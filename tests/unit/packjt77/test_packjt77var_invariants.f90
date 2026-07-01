program test_packjt77var_invariants

  use packjt77
  use packjt77_test_helpers
  implicit none

  integer :: ntests

  ntests=0

  call expect_round_trip_var('K1ABC W9XYZ FN42', &
       'K1ABC W9XYZ FN42', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ R-11', &
       'K1ABC W9XYZ R-11', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ', &
       'K1ABC W9XYZ', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ RRR', &
       'K1ABC W9XYZ RRR', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ RR73', &
       'K1ABC W9XYZ RR73', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ 73', &
       'K1ABC W9XYZ 73', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ +00', &
       'K1ABC W9XYZ +00', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ -30', &
       'K1ABC W9XYZ -30', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ -31', &
       'K1ABC W9XYZ -31', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ -50', &
       'K1ABC W9XYZ -50', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ R+00', &
       'K1ABC W9XYZ R+00', 1, 0, 0)
  ! R-prefixed reports use a separate pack path from bare SNR reports.
  call expect_round_trip_var('K1ABC W9XYZ R-31', &
       'K1ABC W9XYZ R-31', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ R-50', &
       'K1ABC W9XYZ R-50', 1, 0, 0)
  call expect_round_trip_var('K1ABC W9XYZ R FN42', &
       'K1ABC W9XYZ R FN42', 1, 0, 0)
  call expect_round_trip_var('K1ABC/P W9XYZ/P R FN42', &
       'K1ABC/P W9XYZ/P R FN42', 2, 0, 0)
  call expect_round_trip_var('K1ABC/P W9XYZ/P FN42', &
       'K1ABC/P W9XYZ/P FN42', 2, 0, 0)
  call expect_round_trip_var('FREE TEXT MSG', &
       'FREE TEXT MSG', 0, 0, 0)
  call expect_round_trip_var('ABCDEFGHIJKLM', &
       'ABCDEFGHIJKLM', 0, 0, 0)
  call expect_round_trip_var('ABCDEFGHIJKLMN', &
       'ABCDEFGHIJKLM', 0, 0, 0)
  call expect_free_text_fallback_var('A:B', 'A B')
  call expect_free_text_fallback_var('K1ABC R BOGUS', 'K1ABC R BOGUS')
  call expect_round_trip_var('WA9XYZ KA1ABC R 16A EMA', &
       'WA9XYZ KA1ABC R 16A EMA', 0, 3, 0)
  call expect_round_trip_var('WA9XYZ KA1ABC R 32A EMA', &
       'WA9XYZ KA1ABC R 32A EMA', 0, 4, 0)
  call expect_round_trip_var('WA9XYZ KA1ABC 1A EMA', &
       'WA9XYZ KA1ABC 1A EMA', 0, 3, 0)
  call expect_round_trip_var('WA9XYZ KA1ABC R 1A EMA', &
       'WA9XYZ KA1ABC R 1A EMA', 0, 3, 0)
  call expect_round_trip_var('WA9XYZ KA1ABC 15F DX', &
       'WA9XYZ KA1ABC 15F DX', 0, 3, 0)
  call expect_round_trip_var('WA9XYZ KA1ABC 17A EMA', &
       'WA9XYZ KA1ABC 17A EMA', 0, 4, 0)
  call expect_round_trip_var('WA9XYZ KA1ABC R 32A NB', &
       'WA9XYZ KA1ABC R 32A NB', 0, 4, 0)
  call expect_round_trip_var('WA9XYZ KA1ABC R 1H EMA', &
       'WA9XYZ KA1ABC R 1H EMA', 0, 3, 0)
  call expect_round_trip_var('PJ2/W1AW KA1ABC R 1A EMA', &
       '<...> KA1ABC R 1A EMA', 0, 3, 0)
  ! Invalid Field Day exchanges should fall back to 13-character free text.
  call expect_free_text_fallback_var('WA9XYZ KA1ABC R 0A EMA', 'WA9XYZ KA1ABC')
  call expect_free_text_fallback_var('WA9XYZ KA1ABC R 33A EMA', 'WA9XYZ KA1ABC')
  call expect_free_text_fallback_var('WA9XYZ KA1ABC R 1A XYZ', 'WA9XYZ KA1ABC')
  call expect_round_trip_var('123456789ABCDEF012', &
       '123456789ABCDEF012', 0, 5, 0)
  call expect_round_trip_var('TU; W9XYZ K1ABC R 579 MA', &
       'TU; W9XYZ K1ABC R 579 MA', 3, 0, 0)
  call expect_round_trip_var('W9XYZ K1ABC R 579 MA', &
       'W9XYZ K1ABC R 579 MA', 3, 0, 0)
  call expect_round_trip_var('W9XYZ K1ABC 579 MA', &
       'W9XYZ K1ABC 579 MA', 3, 0, 0)
  call expect_round_trip_var('TU; W9XYZ G8ABC R 559 0013', &
       'TU; W9XYZ G8ABC R 559 0013', 3, 0, 0)
  call expect_round_trip_var('W9XYZ G8ABC 559 0013', &
       'W9XYZ G8ABC 559 0013', 3, 0, 0)
  call expect_round_trip_var('W9XYZ K1ABC 529 MA', &
       'W9XYZ K1ABC 529 MA', 3, 0, 0)
  call expect_round_trip_var('TU; W9XYZ K1ABC 599 MA', &
       'TU; W9XYZ K1ABC 599 MA', 3, 0, 0)
  call expect_round_trip_var('TU; W9XYZ K1ABC R 589 DC', &
       'TU; W9XYZ K1ABC R 589 DC', 3, 0, 0)
  call expect_round_trip_var('W9XYZ VE3ABC 559 ON', &
       'W9XYZ VE3ABC 559 ON', 3, 0, 0)
  call expect_round_trip_var('W9XYZ G8ABC 529 0001', &
       'W9XYZ G8ABC 529 0001', 3, 0, 0)
  call expect_round_trip_var('W9XYZ G8ABC R 599 7999', &
       'W9XYZ G8ABC R 599 7999', 3, 0, 0)
  call expect_round_trip_var('W9XYZ K1ABC R 519 MA', &
       'W9XYZ K1ABC R 529 MA', 3, 0, 0)
  call expect_round_trip_var('W9XYZ K1ABC R 609 MA', &
       'W9XYZ K1ABC R 599 MA', 3, 0, 0)
  ! RTTY serial 8000 is neither a serial nor a multiplier; fallback keeps 13 chars.
  call expect_free_text_fallback_var('W9XYZ K1ABC R 579 XYZ', 'W9XYZ K1ABC R')
  call expect_free_text_fallback_var('W9XYZ G8ABC R 559 0000', 'W9XYZ G8ABC R')
  call expect_free_text_fallback_var('W9XYZ G8ABC R 559 8000', 'W9XYZ G8ABC R')
  call expect_not_message_type_var('<W9XYZ> <K1ABC> R 579 MA', 3, 0)
  call expect_round_trip_var('CQ PJ4/K1ABC', &
       'CQ PJ4/K1ABC', 4, 0, 0)
  ! CQ_nnn, CQ_text, and DE enter pack28 through special-token branches.
  call expect_round_trip_var('CQ 146 K1ABC FN42', &
       'CQ 146 K1ABC FN42', 1, 0, 0)
  call expect_round_trip_var('CQ DX K1ABC FN42', &
       'CQ DX K1ABC FN42', 1, 0, 0)
  call expect_round_trip_var('DE K1ABC FN42', &
       'DE K1ABC FN42', 1, 0, 0)

  call expect_round_trip_var('K1ABC FN42 37', &
       'K1ABC FN42 37', 0, 6, 0)
  call expect_round_trip_var('PJ2/K1ABC 37', &
       'PJ2/K1ABC 37', 0, 6, 0)
  call expect_round_trip_var('K1ABC/P 37', &
       'K1ABC/P 37', 0, 6, 0)
  call expect_round_trip_with_hint_var('<PJ4/K1ABC> FK52AB', &
       '<PJ4/K1ABC> FK52AB', 0, 6, 0, 6, 0, 'PJ4/K1ABC', '', '')
  call expect_raw_wspr_unpack_failure_var('invalid WSPR selector', '110', '00000')
  call expect_raw_wspr_unpack_failure_var('invalid WSPR power', '100', '11111')

  ! Angle-bracket nonstandard call selected by the Type 1 path.
  call expect_round_trip_with_hashes_var('<PJ4/K1ABC> W9XYZ RR73', &
       '<PJ4/K1ABC> W9XYZ RR73', 1, 0, 0, 'PJ4/K1ABC', '', '')
  call expect_round_trip_with_hashes_var('<PJ4/K1ABC> W9XYZ RRR', &
       '<PJ4/K1ABC> W9XYZ RRR', 1, 0, 0, 'PJ4/K1ABC', '', '')
  call expect_round_trip_with_hashes_var('<PJ4/K1ABC> W9XYZ 73', &
       '<PJ4/K1ABC> W9XYZ 73', 1, 0, 0, 'PJ4/K1ABC', '', '')
  ! Type 4 hashed/nonstandard completion.
  ! Var unpack rejects the blank Type 4 completion while tokened forms decode.
  call expect_type_unpack_failure_with_hashes_var('PJ2/W1AW <W7ABC>', &
       4, 0, 0, 'W7ABC', '', '')
  call expect_round_trip_with_hashes_var('PJ2/W1AW <W7ABC> RRR', &
       'PJ2/W1AW <W7ABC> RRR', 4, 0, 0, 'W7ABC', '', '')
  call expect_round_trip_with_hashes_var('PJ2/W1AW <W7ABC> RR73', &
       'PJ2/W1AW <W7ABC> RR73', 4, 0, 0, 'W7ABC', '', '')
  call expect_round_trip_with_hashes_var('PJ2/W1AW <W7ABC> 73', &
       'PJ2/W1AW <W7ABC> 73', 4, 0, 0, 'W7ABC', '', '')
  call expect_round_trip_with_context_var('PJ2/W1AW <W7ABC> 73', &
       'PJ2/W1AW <W7ABC> 73', 4, 0, 0, 'W7ABC', 'N0BBB', '', '', '')
  call expect_round_trip_with_context_var('<PJ4/K1ABC> W9XYZ RR73', &
       '<PJ4/K1ABC> W9XYZ RR73', 1, 0, 1, 'PJ4/K1ABC', 'W9XYZ', '', '', '')
  call expect_not_message_type_var('<PJ4/K1ABC> W9XYZ -12', 4, 0)
  ! Type 4 CQ strips angle brackets from the nonstandard call on decode.
  call expect_round_trip_var('CQ <PJ4/K1ABC>', &
       'CQ PJ4/K1ABC', 4, 0, 0)

  ! Type 5 contest form with both callsigns carried through hashes.
  call expect_round_trip_with_hashes_var('<W3CCX> <K1JT/P> 590001 FN20QI', &
       '<W3CCX> <K1JT/P> 590001 FN20QI', 5, 0, 0, 'W3CCX', 'K1JT/P', '')
  call expect_round_trip_with_hashes_var('<W3CCX> <K1JT/P> 520001 FN20QI', &
       '<W3CCX> <K1JT/P> 520001 FN20QI', 5, 0, 0, 'W3CCX', 'K1JT/P', '')
  call expect_round_trip_with_hashes_var('<W3CCX> <K1JT/P> R 520001 FN20QI', &
       '<W3CCX> <K1JT/P> R 520001 FN20QI', 5, 0, 0, 'W3CCX', 'K1JT/P', '')
  call expect_round_trip_with_hashes_var('<W3CCX> <K1JT/P> R 592047 FN20QI', &
       '<W3CCX> <K1JT/P> R 592047 FN20QI', 5, 0, 0, 'W3CCX', 'K1JT/P', '')
  call expect_round_trip_var('<W3CCX> <K1JT/P> 590001 FN20QI', &
       '<...> <...> 590001 FN20QI', 5, 0, 0)
  call expect_not_message_type_var('<W3CCX> <K1JT/P> 520000 FN20QI', 5, 0)
  call expect_not_message_type_var('<W3CCX> <K1JT/P> 530000 FN20QI', 5, 0)
  call expect_not_message_type_var('<W3CCX> <K1JT/P> 592048 FN20QI', 5, 0)
  call expect_not_message_type_var('<W3CCX> <K1JT/P> 582048 FN20QI', 5, 0)
  call expect_not_message_type_var('<W3CCX> <K1JT/P> 594095 FN20QI', 5, 0)
  call expect_not_message_type_var('<W3CCX> <K1JT/P> 590001 FN20QY', 5, 0)
  call expect_not_message_type_var('<W3CCX> <K1JT/P> X 590001 FN20QI', 5, 0)
  call expect_not_message_type_var('<W3CCX> <K1JT/P> RR 590001 FN20QI', 5, 0)
  call expect_not_message_type_var('W3CCX <K1JT/P> 590001 FN20QI', 5, 0)

  ! DXpedition hash resolution is direction-sensitive.
  call expect_round_trip_with_context_var('K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context_var('K1ABC RR73; W9XYZ <KH1/KH7Z> -30', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> -30', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context_var('K1ABC RR73; W9XYZ <KH1/KH7Z> +00', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> +00', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context_var('K1ABC RR73; W9XYZ <KH1/KH7Z> +32', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> +32', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  ! DXpedition reports are quantized to 5 bits and clamp outside -30..+32.
  call expect_round_trip_with_context_var('K1ABC RR73; W9XYZ <KH1/KH7Z> -31', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> -30', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context_var('K1ABC RR73; W9XYZ <KH1/KH7Z> +33', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> +32', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context_var('K1ABC RR73; W9XYZ <KH1/KH7Z> +34', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> +32', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context_var('K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, 0, &
       'KH1/KH7Z', 'N0BBB', '', '', '')
  call expect_not_message_type_var('K1ABC RRR; W9XYZ <KH1/KH7Z> -12', 0, 1)
  call expect_round_trip_with_context_var('PJ2/W1AW RR73; W9XYZ <KH1/KH7Z> -12', &
       '<...> RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context_var('K1ABC RR73; PJ2/W1AW <KH1/KH7Z> -12', &
       'K1ABC RR73; <...> <KH1/KH7Z> -12', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_not_message_type_var('K1ABC RR73; W9XYZ KH1/KH7Z -12', 0, 1)

  ! Operator input is canonicalized before packing.
  call expect_round_trip_var('  k1abc   w9xyz   fn42', &
       'K1ABC W9XYZ FN42', 1, 0, 0)
  call expect_round_trip_var('K1ABC/R W9XYZ/R R FN42', &
       'K1ABC/R W9XYZ/R R FN42', 1, 0, 0)
  call expect_round_trip_var('CQ PJ2/W1AW', &
       'CQ PJ2/W1AW', 4, 0, 0)
  call expect_round_trip_var('CQ TEST/W1AW', &
       'CQ TEST/W1AW', 4, 0, 0)
  call expect_round_trip_var('QRZ PJ4/K1ABC', &
       'QRZ PJ4/K1ABC', 0, 0, 0)
  call expect_type_unpack_failure_var('CQ K1ABC R FN42', 1, 0)
  call expect_not_message_type_var('<PJ4/K1ABC> <W7ABC> TEST', 5, 0)
  call expect_not_message_type_var('<A/B> <C/D>', 1, 0)
  call expect_not_message_type_var('<A/B> <C/D>', 5, 0)

  ! Type 1 messages with two hashed calls decode to placeholders unless the
  ! hash table is primed.
  call expect_dual_angle_type1_placeholders_var('<W1AW> <K1JT>', '<...> <...>')
  call expect_dual_angle_type1_placeholders_var('<W1AW> <K1JT> RR73', &
       '<...> <...> RR73')

  write(*,1000) ntests
1000 format('packjt77var invariant tests passed: ',i0)

contains

  subroutine expect_round_trip_var(input,expected,want_i3,want_n3,nrx)
    character(len=*), intent(in) :: input, expected
    integer, intent(in) :: want_i3, want_n3, nrx

    call expect_round_trip_with_context_var(input,expected,want_i3,want_n3,nrx, &
         'N0AAA', 'N0BBB', '', '', '')
  end subroutine expect_round_trip_var

  subroutine expect_round_trip_with_hashes_var(input,expected,want_i3,want_n3,nrx, &
       hash_call_1,hash_call_2,hash_call_3)
    character(len=*), intent(in) :: input, expected
    integer, intent(in) :: want_i3, want_n3, nrx
    character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3

    call expect_round_trip_with_context_var(input,expected,want_i3,want_n3,nrx, &
         'N0AAA', 'N0BBB', hash_call_1, hash_call_2, hash_call_3)
  end subroutine expect_round_trip_with_hashes_var

  subroutine expect_round_trip_with_context_var(input,expected,want_i3,want_n3,nrx, &
       mycall,dxcall,hash_call_1,hash_call_2,hash_call_3)
    character(len=*), intent(in) :: input, expected
    integer, intent(in) :: want_i3, want_n3, nrx
    character(len=*), intent(in) :: mycall, dxcall
    character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3

    call expect_round_trip_with_pack_state_var(input,expected,want_i3,want_n3, &
         -1,-1,nrx,mycall,dxcall,hash_call_1,hash_call_2,hash_call_3)
  end subroutine expect_round_trip_with_context_var

  subroutine expect_round_trip_with_hint_var(input,expected,want_i3,want_n3, &
       pack_i3,pack_n3,nrx,hash_call_1,hash_call_2,hash_call_3)
    character(len=*), intent(in) :: input, expected
    integer, intent(in) :: want_i3, want_n3, pack_i3, pack_n3, nrx
    character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3

    call expect_round_trip_with_pack_state_var(input,expected,want_i3,want_n3, &
         pack_i3,pack_n3,nrx,'N0AAA','N0BBB',hash_call_1,hash_call_2,hash_call_3)
  end subroutine expect_round_trip_with_hint_var

  subroutine expect_round_trip_with_pack_state_var(input,expected,want_i3,want_n3, &
       pack_i3,pack_n3,nrx,mycall,dxcall,hash_call_1,hash_call_2,hash_call_3)
    character(len=*), intent(in) :: input, expected
    integer, intent(in) :: want_i3, want_n3, pack_i3, pack_n3, nrx
    character(len=*), intent(in) :: mycall, dxcall
    character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3
    character(len=77) :: c77
    character(len=37) :: decoded, packed_input
    integer :: got_i3, got_n3
    logical :: ok

    packed_input='                                     '
    packed_input=input
    call reset_packjt77var_state('N0AAA', 'N0BBB')

    got_i3=pack_i3
    got_n3=pack_n3
    c77=''
    call pack77var(packed_input,got_i3,got_n3,c77,0)

    call assert_message_type('pack77var',input,want_i3,want_n3,got_i3,got_n3)

    call reset_packjt77var_state(mycall, dxcall)
    call prime_hash_call_var(hash_call_1)
    call prime_hash_call_var(hash_call_2)
    call prime_hash_call_var(hash_call_3)

    decoded='                                     '
    ok=.false.
    call unpack77var(c77,nrx,decoded,ok,1)
    if(.not.ok) then
       write(*,1010) trim(input)
1010   format('Var unpack failure for "',a,'"')
       error stop 1
    endif
    if(trim(decoded).ne.expected) then
       write(*,1020) trim(input), trim(expected), trim(decoded)
1020   format('Var round-trip failure for "',a,'"; expected "',a,'"; got "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_round_trip_with_pack_state_var

  subroutine expect_not_message_type_var(input,blocked_i3,blocked_n3)
    character(len=*), intent(in) :: input
    integer, intent(in) :: blocked_i3, blocked_n3
    character(len=77) :: c77
    character(len=37) :: packed_input
    integer :: got_i3, got_n3

    packed_input='                                     '
    packed_input=input
    call reset_packjt77var_state('N0AAA', 'N0BBB')

    got_i3=-1
    got_n3=-1
    c77=''
    call pack77var(packed_input,got_i3,got_n3,c77,0)
    if(got_i3.eq.blocked_i3 .and. got_n3.eq.blocked_n3) then
       write(*,1060) trim(input), blocked_i3, blocked_n3
1060   format('Var message "',a,'" packed as blocked type ',i0,'.',i0)
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_not_message_type_var

  subroutine expect_type_unpack_failure_var(input,want_i3,want_n3)
    character(len=*), intent(in) :: input
    integer, intent(in) :: want_i3, want_n3
    character(len=77) :: c77
    character(len=37) :: decoded, packed_input
    integer :: got_i3, got_n3
    logical :: ok

    packed_input='                                     '
    packed_input=input
    call reset_packjt77var_state('N0AAA', 'N0BBB')

    got_i3=-1
    got_n3=-1
    c77=''
    call pack77var(packed_input,got_i3,got_n3,c77,0)
    call assert_message_type('pack77var',input,want_i3,want_n3,got_i3,got_n3)

    decoded='                                     '
    ok=.true.
    call unpack77var(c77,0,decoded,ok,1)
    if(ok) then
       write(*,1090) trim(input), trim(decoded)
1090   format('Var message "',a,'" decoded successfully as "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_type_unpack_failure_var

  subroutine expect_raw_wspr_unpack_failure_var(label,selector,idbm_bits)
    character(len=*), intent(in) :: label, selector, idbm_bits
    character(len=77) :: c77
    character(len=37) :: decoded
    logical :: ok

    c77=repeat('0',77)
    c77(44:48)=idbm_bits
    c77(48:50)=selector
    c77(72:77)='110000'

    call reset_packjt77var_state('N0AAA', 'N0BBB')
    decoded='                                     '
    ok=.true.
    call unpack77var(c77,0,decoded,ok,1)
    if(ok) then
       write(*,1110) trim(label), trim(decoded)
1110   format('Var WSPR raw-bit unpack unexpectedly succeeded for ',a, &
              '; decoded "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_raw_wspr_unpack_failure_var

  subroutine expect_type_unpack_failure_with_hashes_var(input,want_i3,want_n3,nrx, &
       hash_call_1,hash_call_2,hash_call_3)
    character(len=*), intent(in) :: input
    integer, intent(in) :: want_i3, want_n3, nrx
    character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3
    character(len=77) :: c77
    character(len=37) :: decoded, packed_input
    integer :: got_i3, got_n3
    logical :: ok

    packed_input='                                     '
    packed_input=input
    call reset_packjt77var_state('N0AAA', 'N0BBB')

    got_i3=-1
    got_n3=-1
    c77=''
    call pack77var(packed_input,got_i3,got_n3,c77,0)
    call assert_message_type('pack77var',input,want_i3,want_n3,got_i3,got_n3)

    call reset_packjt77var_state('N0AAA', 'N0BBB')
    call prime_hash_call_var(hash_call_1)
    call prime_hash_call_var(hash_call_2)
    call prime_hash_call_var(hash_call_3)

    decoded='                                     '
    ok=.true.
    call unpack77var(c77,nrx,decoded,ok,1)
    if(ok) then
       write(*,1100) trim(input), trim(decoded)
1100   format('Var message "',a,'" decoded successfully as "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_type_unpack_failure_with_hashes_var

  subroutine expect_free_text_fallback_var(input,expected)
    character(len=*), intent(in) :: input, expected
    character(len=77) :: c77
    character(len=37) :: decoded, packed_input
    integer :: got_i3, got_n3
    logical :: ok

    packed_input='                                     '
    packed_input=input
    call reset_packjt77var_state('N0AAA', 'N0BBB')

    got_i3=-1
    got_n3=-1
    c77=''
    call pack77var(packed_input,got_i3,got_n3,c77,0)
    call assert_message_type('pack77var',input,0,0,got_i3,got_n3)

    decoded='                                     '
    ok=.false.
    call unpack77var(c77,0,decoded,ok,1)
    if(.not.ok) then
       write(*,1070) trim(input)
1070   format('Var free-text unpack failure for "',a,'"')
       error stop 1
    endif
    if(trim(decoded).ne.expected) then
       write(*,1080) trim(input), trim(expected), trim(decoded)
1080   format('Var free-text failure for "',a,'"; expected "',a,'"; got "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_free_text_fallback_var

  subroutine expect_dual_angle_type1_placeholders_var(input,expected)
    character(len=*), intent(in) :: input, expected
    character(len=77) :: c77
    character(len=37) :: decoded, packed_input
    integer :: got_i3, got_n3
    logical :: ok

    packed_input='                                     '
    packed_input=input
    call reset_packjt77var_state('N0AAA', 'N0BBB')

    got_i3=-1
    got_n3=-1
    c77=''
    call pack77var(packed_input,got_i3,got_n3,c77,0)
    call assert_message_type('pack77var',input,1,0,got_i3,got_n3)

    call reset_packjt77var_state('N0AAA', 'N0BBB')
    decoded='                                     '
    ok=.false.
    call unpack77var(c77,0,decoded,ok,1)
    if(.not.ok) then
       write(*,1040) trim(input)
1040   format('Dual-angle var Type 1 unpack failure for "',a,'"')
       error stop 1
    endif
    if(trim(decoded).ne.expected) then
       write(*,1050) trim(input), trim(expected), trim(decoded)
1050   format('Dual-angle var Type 1 placeholder failure for "',a,'"; expected "',a, &
              '"; got "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_dual_angle_type1_placeholders_var

  subroutine reset_packjt77var_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall
    integer :: n10, n12, n22

    call clear_all_state(mycall,dxcall)
    if(len_trim(mycall).gt.2) then
       call save_hash_mycallvar(mycall13var,hashmy10var,hashmy12var,hashmy22var)
    endif
    if(len_trim(dxcall).gt.2) then
       call prime_hash_call_var(dxcall)
       call save_hash_call(dxcall13,n10,n12,n22)
    endif
  end subroutine reset_packjt77var_state

  subroutine prime_hash_call_var(callsign)
    character(len=*), intent(in) :: callsign
    integer :: n10, n12, n22
    character(len=13) :: c13

    if(len_trim(callsign).le.0) return
    call normalize_call(callsign,c13)
    if(len_trim(c13).le.0) return
    call save_hash_call(c13,n10,n12,n22)
    call store_rx_hash_var(c13)
  end subroutine prime_hash_call_var

  subroutine store_rx_hash_var(c13)
    character(len=*), intent(in) :: c13
    integer :: n10, n12, n22, i

    n10=ihashcall(c13,10)
    if(n10.ge.0 .and. n10.le.1023) calls10var(n10)=c13
    n12=ihashcall(c13,12)
    if(n12.ge.0 .and. n12.le.4095) calls12var(n12)=c13
    n22=ihashcall(c13,22)
    do i=1,nzhashvar
       if(ihash22var(i).eq.n22) then
          calls22var(i)=c13
          return
       endif
    enddo
    if(nzhashvar.lt.MAXHASHVAR) then
       if(nzhashvar.ge.1) then
          ihash22var(nzhashvar+1:2:-1)=ihash22var(nzhashvar:1:-1)
          calls22var(nzhashvar+1:2:-1)=calls22var(nzhashvar:1:-1)
       endif
       nzhashvar=nzhashvar+1
       ihash22var(1)=n22
       calls22var(1)=c13
    endif
  end subroutine store_rx_hash_var

end program test_packjt77var_invariants
