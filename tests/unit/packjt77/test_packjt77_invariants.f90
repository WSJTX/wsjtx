program test_packjt77_invariants

  use packjt77
  use packjt77_test_helpers
  implicit none

  integer :: ntests

  ntests=0

  call expect_round_trip('K1ABC W9XYZ FN42', &
       'K1ABC W9XYZ FN42', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ R-11', &
       'K1ABC W9XYZ R-11', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ', &
       'K1ABC W9XYZ', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ RRR', &
       'K1ABC W9XYZ RRR', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ RR73', &
       'K1ABC W9XYZ RR73', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ 73', &
       'K1ABC W9XYZ 73', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ +00', &
       'K1ABC W9XYZ +00', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ -30', &
       'K1ABC W9XYZ -30', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ -31', &
       'K1ABC W9XYZ -31', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ -50', &
       'K1ABC W9XYZ -50', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ R+00', &
       'K1ABC W9XYZ R+00', 1, 0, 0)
  ! R-prefixed reports use a separate pack path from bare SNR reports.
  call expect_round_trip('K1ABC W9XYZ R-31', &
       'K1ABC W9XYZ R-31', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ R-50', &
       'K1ABC W9XYZ R-50', 1, 0, 0)
  call expect_round_trip('K1ABC W9XYZ R FN42', &
       'K1ABC W9XYZ R FN42', 1, 0, 0)
  call expect_round_trip('K1ABC/P W9XYZ/P R FN42', &
       'K1ABC/P W9XYZ/P R FN42', 2, 0, 0)
  call expect_round_trip('K1ABC/P W9XYZ/P FN42', &
       'K1ABC/P W9XYZ/P FN42', 2, 0, 0)
  call expect_round_trip('FREE TEXT MSG', &
       'FREE TEXT MSG', 0, 0, 0)
  call expect_round_trip('ABCDEFGHIJKLM', &
       'ABCDEFGHIJKLM', 0, 0, 0)
  call expect_round_trip('ABCDEFGHIJKLMN', &
       'ABCDEFGHIJKLM', 0, 0, 0)
  call expect_free_text_fallback('A:B', 'A B')
  call expect_free_text_fallback('K1ABC R BOGUS', 'K1ABC R BOGUS')
  call expect_round_trip('WA9XYZ KA1ABC R 16A EMA', &
       'WA9XYZ KA1ABC R 16A EMA', 0, 3, 0)
  call expect_round_trip('WA9XYZ KA1ABC R 32A EMA', &
       'WA9XYZ KA1ABC R 32A EMA', 0, 4, 0)
  call expect_round_trip('WA9XYZ KA1ABC 1A EMA', &
       'WA9XYZ KA1ABC 1A EMA', 0, 3, 0)
  call expect_round_trip('WA9XYZ KA1ABC R 1A EMA', &
       'WA9XYZ KA1ABC R 1A EMA', 0, 3, 0)
  call expect_round_trip('WA9XYZ KA1ABC 15F DX', &
       'WA9XYZ KA1ABC 15F DX', 0, 3, 0)
  call expect_round_trip('WA9XYZ KA1ABC 17A EMA', &
       'WA9XYZ KA1ABC 17A EMA', 0, 4, 0)
  call expect_round_trip('WA9XYZ KA1ABC R 32A NB', &
       'WA9XYZ KA1ABC R 32A NB', 0, 4, 0)
  call expect_round_trip('WA9XYZ KA1ABC R 1H EMA', &
       'WA9XYZ KA1ABC R 1H EMA', 0, 3, 0)
  call expect_round_trip('PJ2/W1AW KA1ABC R 1A EMA', &
       '<...> KA1ABC R 1A EMA', 0, 3, 0)
  ! Invalid Field Day exchanges should fall back to 13-character free text.
  call expect_free_text_fallback('WA9XYZ KA1ABC R 0A EMA', 'WA9XYZ KA1ABC')
  call expect_free_text_fallback('WA9XYZ KA1ABC R 33A EMA', 'WA9XYZ KA1ABC')
  call expect_free_text_fallback('WA9XYZ KA1ABC R 1A XYZ', 'WA9XYZ KA1ABC')
  call expect_round_trip('123456789ABCDEF012', &
       '123456789ABCDEF012', 0, 5, 0)
  call expect_round_trip('TU; W9XYZ K1ABC R 579 MA', &
       'TU; W9XYZ K1ABC R 579 MA', 3, 0, 0)
  call expect_round_trip('W9XYZ K1ABC R 579 MA', &
       'W9XYZ K1ABC R 579 MA', 3, 0, 0)
  call expect_round_trip('W9XYZ K1ABC 579 MA', &
       'W9XYZ K1ABC 579 MA', 3, 0, 0)
  call expect_round_trip('TU; W9XYZ G8ABC R 559 0013', &
       'TU; W9XYZ G8ABC R 559 0013', 3, 0, 0)
  call expect_round_trip('W9XYZ G8ABC 559 0013', &
       'W9XYZ G8ABC 559 0013', 3, 0, 0)
  call expect_round_trip('W9XYZ K1ABC 529 MA', &
       'W9XYZ K1ABC 529 MA', 3, 0, 0)
  call expect_round_trip('TU; W9XYZ K1ABC 599 MA', &
       'TU; W9XYZ K1ABC 599 MA', 3, 0, 0)
  call expect_round_trip('TU; W9XYZ K1ABC R 589 DC', &
       'TU; W9XYZ K1ABC R 589 DC', 3, 0, 0)
  call expect_round_trip('W9XYZ VE3ABC 559 ON', &
       'W9XYZ VE3ABC 559 ON', 3, 0, 0)
  call expect_round_trip('W9XYZ G8ABC 529 0001', &
       'W9XYZ G8ABC 529 0001', 3, 0, 0)
  call expect_round_trip('W9XYZ G8ABC R 599 7999', &
       'W9XYZ G8ABC R 599 7999', 3, 0, 0)
  call expect_round_trip('W9XYZ K1ABC R 519 MA', &
       'W9XYZ K1ABC R 529 MA', 3, 0, 0)
  call expect_round_trip('W9XYZ K1ABC R 609 MA', &
       'W9XYZ K1ABC R 599 MA', 3, 0, 0)
  ! RTTY serial 8000 is neither a serial nor a multiplier; fallback keeps 13 chars.
  call expect_free_text_fallback('W9XYZ K1ABC R 579 XYZ', 'W9XYZ K1ABC R')
  call expect_free_text_fallback('W9XYZ G8ABC R 559 0000', 'W9XYZ G8ABC R')
  call expect_free_text_fallback('W9XYZ G8ABC R 559 8000', 'W9XYZ G8ABC R')
  call expect_not_message_type('<W9XYZ> <K1ABC> R 579 MA', 3, 0)
  call expect_round_trip('CQ PJ4/K1ABC', &
       'CQ PJ4/K1ABC', 4, 0, 0)
  ! CQ_nnn, CQ_text, and DE enter pack28 through special-token branches.
  call expect_round_trip('CQ 146 K1ABC FN42', &
       'CQ 146 K1ABC FN42', 1, 0, 0)
  call expect_round_trip('CQ DX K1ABC FN42', &
       'CQ DX K1ABC FN42', 1, 0, 0)
  call expect_round_trip('DE K1ABC FN42', &
       'DE K1ABC FN42', 1, 0, 0)

  call expect_round_trip('K1ABC FN42 37', &
       'K1ABC FN42 37', 0, 6, 0)
  call expect_round_trip('PJ2/K1ABC 37', &
       'PJ2/K1ABC 37', 0, 6, 0)
  call expect_round_trip('K1ABC/P 37', &
       'K1ABC/P 37', 0, 6, 0)
  call expect_round_trip_with_hint('<PJ4/K1ABC> FK52AB', &
       '<PJ4/K1ABC> FK52AB', 0, 6, 0, 6, 0, 'PJ4/K1ABC', '', '')

  ! Angle-bracket nonstandard call selected by the Type 1 path.
  call expect_round_trip_with_hashes('<PJ4/K1ABC> W9XYZ RR73', &
       '<PJ4/K1ABC> W9XYZ RR73', 1, 0, 0, 'PJ4/K1ABC', '', '')
  call expect_round_trip_with_hashes('<PJ4/K1ABC> W9XYZ RRR', &
       '<PJ4/K1ABC> W9XYZ RRR', 1, 0, 0, 'PJ4/K1ABC', '', '')
  call expect_round_trip_with_hashes('<PJ4/K1ABC> W9XYZ 73', &
       '<PJ4/K1ABC> W9XYZ 73', 1, 0, 0, 'PJ4/K1ABC', '', '')
  ! Type 4 hashed/nonstandard completion.
  call expect_round_trip_with_hashes('PJ2/W1AW <W7ABC>', &
       'PJ2/W1AW <W7ABC>', 4, 0, 0, 'W7ABC', '', '')
  call expect_round_trip_with_hashes('PJ2/W1AW <W7ABC> RRR', &
       'PJ2/W1AW <W7ABC> RRR', 4, 0, 0, 'W7ABC', '', '')
  call expect_round_trip_with_hashes('PJ2/W1AW <W7ABC> RR73', &
       'PJ2/W1AW <W7ABC> RR73', 4, 0, 0, 'W7ABC', '', '')
  call expect_round_trip_with_hashes('PJ2/W1AW <W7ABC> 73', &
       'PJ2/W1AW <W7ABC> 73', 4, 0, 0, 'W7ABC', '', '')
  call expect_round_trip_with_context('PJ2/W1AW <W7ABC> 73', &
       'PJ2/W1AW <W7ABC> 73', 4, 0, 0, 'W7ABC', 'N0BBB', '', '', '')
  call expect_round_trip_with_context('<PJ4/K1ABC> W9XYZ RR73', &
       '<PJ4/K1ABC> W9XYZ RR73', 1, 0, 1, 'PJ4/K1ABC', 'W9XYZ', '', '', '')
  call expect_not_message_type('<PJ4/K1ABC> W9XYZ -12', 4, 0)
  ! Type 4 CQ strips angle brackets from the nonstandard call on decode.
  call expect_round_trip('CQ <PJ4/K1ABC>', &
       'CQ PJ4/K1ABC', 4, 0, 0)

  ! Type 5 contest form with both callsigns carried through hashes.
  call expect_round_trip_with_hashes('<W3CCX> <K1JT/P> 590001 FN20QI', &
       '<W3CCX> <K1JT/P> 590001 FN20QI', 5, 0, 0, 'W3CCX', 'K1JT/P', '')
  call expect_round_trip_with_hashes('<W3CCX> <K1JT/P> 520001 FN20QI', &
       '<W3CCX> <K1JT/P> 520001 FN20QI', 5, 0, 0, 'W3CCX', 'K1JT/P', '')
  call expect_round_trip_with_hashes('<W3CCX> <K1JT/P> R 520001 FN20QI', &
       '<W3CCX> <K1JT/P> R 520001 FN20QI', 5, 0, 0, 'W3CCX', 'K1JT/P', '')
  call expect_round_trip_with_hashes('<W3CCX> <K1JT/P> R 592047 FN20QI', &
       '<W3CCX> <K1JT/P> R 592047 FN20QI', 5, 0, 0, 'W3CCX', 'K1JT/P', '')
  call expect_round_trip('<W3CCX> <K1JT/P> 590001 FN20QI', &
       '<...> <...> 590001 FN20QI', 5, 0, 0)
  call expect_not_message_type('<W3CCX> <K1JT/P> 520000 FN20QI', 5, 0)
  call expect_not_message_type('<W3CCX> <K1JT/P> 530000 FN20QI', 5, 0)
  call expect_not_message_type('<W3CCX> <K1JT/P> 592048 FN20QI', 5, 0)
  call expect_not_message_type('<W3CCX> <K1JT/P> 582048 FN20QI', 5, 0)
  call expect_not_message_type('<W3CCX> <K1JT/P> 594095 FN20QI', 5, 0)
  call expect_not_message_type('<W3CCX> <K1JT/P> 590001 FN20QY', 5, 0)
  call expect_not_message_type('<W3CCX> <K1JT/P> X 590001 FN20QI', 5, 0)
  call expect_not_message_type('<W3CCX> <K1JT/P> RR 590001 FN20QI', 5, 0)
  call expect_not_message_type('W3CCX <K1JT/P> 590001 FN20QI', 5, 0)

  ! DXpedition hash resolution is direction-sensitive.
  call expect_round_trip_with_context('K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context('K1ABC RR73; W9XYZ <KH1/KH7Z> -30', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> -30', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context('K1ABC RR73; W9XYZ <KH1/KH7Z> +00', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> +00', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context('K1ABC RR73; W9XYZ <KH1/KH7Z> +32', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> +32', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  ! DXpedition reports are quantized to 5 bits and clamp outside -30..+32.
  call expect_round_trip_with_context('K1ABC RR73; W9XYZ <KH1/KH7Z> -31', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> -30', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context('K1ABC RR73; W9XYZ <KH1/KH7Z> +33', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> +32', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context('K1ABC RR73; W9XYZ <KH1/KH7Z> +34', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> +32', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context('K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
       'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, 0, &
       'KH1/KH7Z', 'N0BBB', '', '', '')
  call expect_not_message_type('K1ABC RRR; W9XYZ <KH1/KH7Z> -12', 0, 1)
  call expect_round_trip_with_context('PJ2/W1AW RR73; W9XYZ <KH1/KH7Z> -12', &
       '<...> RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_round_trip_with_context('K1ABC RR73; PJ2/W1AW <KH1/KH7Z> -12', &
       'K1ABC RR73; <...> <KH1/KH7Z> -12', 0, 1, 1, &
       'N0AAA', 'KH1/KH7Z', '', '', '')
  call expect_not_message_type('K1ABC RR73; W9XYZ KH1/KH7Z -12', 0, 1)

  ! Operator input is canonicalized before packing.
  call expect_round_trip('  k1abc   w9xyz   fn42', &
       'K1ABC W9XYZ FN42', 1, 0, 0)
  call expect_round_trip('K1ABC/R W9XYZ/R R FN42', &
       'K1ABC/R W9XYZ/R R FN42', 1, 0, 0)
  call expect_round_trip('CQ PJ2/W1AW', &
       'CQ PJ2/W1AW', 4, 0, 0)
  call expect_round_trip('CQ TEST/W1AW', &
       'CQ TEST/W1AW', 4, 0, 0)
  call expect_round_trip('QRZ PJ4/K1ABC', &
       'QRZ PJ4/K1ABC', 0, 0, 0)
  call expect_type_unpack_failure('CQ K1ABC R FN42', 1, 0)
  call expect_not_message_type('<PJ4/K1ABC> <W7ABC> TEST', 5, 0)
  call expect_not_message_type('<A/B> <C/D>', 1, 0)
  call expect_not_message_type('<A/B> <C/D>', 5, 0)

  ! Type 1 messages with two hashed calls decode to placeholders unless the
  ! hash table is primed.
  call expect_dual_angle_type1_placeholders('<W1AW> <K1JT>', '<...> <...>')
  call expect_dual_angle_type1_placeholders('<W1AW> <K1JT> RR73', &
       '<...> <...> RR73')


  write(*,1000) ntests
1000 format('packjt77 invariant tests passed: ',i0)

contains

  subroutine expect_round_trip(input,expected,want_i3,want_n3,nrx)
    character(len=*), intent(in) :: input, expected
    integer, intent(in) :: want_i3, want_n3, nrx

    call expect_round_trip_with_context(input,expected,want_i3,want_n3,nrx, &
         'N0AAA', 'N0BBB', '', '', '')
  end subroutine expect_round_trip

  subroutine expect_round_trip_with_hashes(input,expected,want_i3,want_n3,nrx, &
       hash_call_1,hash_call_2,hash_call_3)
    character(len=*), intent(in) :: input, expected
    integer, intent(in) :: want_i3, want_n3, nrx
    character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3

    call expect_round_trip_with_context(input,expected,want_i3,want_n3,nrx, &
         'N0AAA', 'N0BBB', hash_call_1, hash_call_2, hash_call_3)
  end subroutine expect_round_trip_with_hashes

  subroutine expect_round_trip_with_context(input,expected,want_i3,want_n3,nrx, &
       mycall,dxcall,hash_call_1,hash_call_2,hash_call_3)
    character(len=*), intent(in) :: input, expected
    integer, intent(in) :: want_i3, want_n3, nrx
    character(len=*), intent(in) :: mycall, dxcall
    character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3

    call expect_round_trip_with_pack_state(input,expected,want_i3,want_n3, &
         -1,-1,nrx,mycall,dxcall,hash_call_1,hash_call_2,hash_call_3)
  end subroutine expect_round_trip_with_context

  subroutine expect_round_trip_with_hint(input,expected,want_i3,want_n3,pack_i3, &
       pack_n3,nrx,hash_call_1,hash_call_2,hash_call_3)
    character(len=*), intent(in) :: input, expected
    integer, intent(in) :: want_i3, want_n3, pack_i3, pack_n3, nrx
    character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3

    call expect_round_trip_with_pack_state(input,expected,want_i3,want_n3, &
         pack_i3,pack_n3,nrx,'N0AAA','N0BBB',hash_call_1,hash_call_2,hash_call_3)
  end subroutine expect_round_trip_with_hint

  subroutine expect_round_trip_with_pack_state(input,expected,want_i3,want_n3, &
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
    call reset_packjt77_state('N0AAA', 'N0BBB')

    got_i3=pack_i3
    got_n3=pack_n3
    c77=''
    call pack77(packed_input,got_i3,got_n3,c77)

    call assert_message_type('pack77',input,want_i3,want_n3,got_i3,got_n3)

    call reset_packjt77_state(mycall, dxcall)
    call prime_hash_call(hash_call_1)
    call prime_hash_call(hash_call_2)
    call prime_hash_call(hash_call_3)

    decoded='                                     '
    ok=.false.
    call unpack77(c77,nrx,decoded,ok)
    if(.not.ok) then
       write(*,1010) trim(input)
1010   format('Unpack failure for "',a,'"')
       error stop 1
    endif
    if(trim(decoded).ne.expected) then
       write(*,1020) trim(input), trim(expected), trim(decoded)
1020   format('Round-trip failure for "',a,'"; expected "',a,'"; got "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_round_trip_with_pack_state

  subroutine expect_not_message_type(input,blocked_i3,blocked_n3)
    character(len=*), intent(in) :: input
    integer, intent(in) :: blocked_i3, blocked_n3
    character(len=77) :: c77
    character(len=37) :: packed_input
    integer :: got_i3, got_n3

    packed_input='                                     '
    packed_input=input
    call reset_packjt77_state('N0AAA', 'N0BBB')

    got_i3=-1
    got_n3=-1
    c77=''
    call pack77(packed_input,got_i3,got_n3,c77)
    if(got_i3.eq.blocked_i3 .and. got_n3.eq.blocked_n3) then
       write(*,1060) trim(input), blocked_i3, blocked_n3
1060   format('Message "',a,'" packed as blocked type ',i0,'.',i0)
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_not_message_type

  subroutine expect_type_unpack_failure(input,want_i3,want_n3)
    character(len=*), intent(in) :: input
    integer, intent(in) :: want_i3, want_n3
    character(len=77) :: c77
    character(len=37) :: decoded, packed_input
    integer :: got_i3, got_n3
    logical :: ok

    packed_input='                                     '
    packed_input=input
    call reset_packjt77_state('N0AAA', 'N0BBB')

    got_i3=-1
    got_n3=-1
    c77=''
    call pack77(packed_input,got_i3,got_n3,c77)
    call assert_message_type('pack77',input,want_i3,want_n3,got_i3,got_n3)

    decoded='                                     '
    ok=.true.
    call unpack77(c77,0,decoded,ok)
    if(ok) then
       write(*,1090) trim(input), trim(decoded)
1090   format('Message "',a,'" decoded successfully as "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_type_unpack_failure

  subroutine expect_free_text_fallback(input,expected)
    character(len=*), intent(in) :: input, expected
    character(len=77) :: c77
    character(len=37) :: decoded, packed_input
    integer :: got_i3, got_n3
    logical :: ok

    packed_input='                                     '
    packed_input=input
    call reset_packjt77_state('N0AAA', 'N0BBB')

    got_i3=-1
    got_n3=-1
    c77=''
    call pack77(packed_input,got_i3,got_n3,c77)
    call assert_message_type('pack77',input,0,0,got_i3,got_n3)

    decoded='                                     '
    ok=.false.
    call unpack77(c77,0,decoded,ok)
    if(.not.ok) then
       write(*,1070) trim(input)
1070   format('Free-text unpack failure for "',a,'"')
       error stop 1
    endif
    if(trim(decoded).ne.expected) then
       write(*,1080) trim(input), trim(expected), trim(decoded)
1080   format('Free-text failure for "',a,'"; expected "',a,'"; got "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_free_text_fallback

  subroutine expect_dual_angle_type1_placeholders(input,expected)
    character(len=*), intent(in) :: input, expected
    character(len=77) :: c77
    character(len=37) :: decoded, packed_input
    integer :: got_i3, got_n3
    logical :: ok

    packed_input='                                     '
    packed_input=input
    call reset_packjt77_state('N0AAA', 'N0BBB')

    got_i3=-1
    got_n3=-1
    c77=''
    call pack77(packed_input,got_i3,got_n3,c77)
    call assert_message_type('pack77',input,1,0,got_i3,got_n3)

    call reset_packjt77_state('N0AAA', 'N0BBB')
    decoded='                                     '
    ok=.false.
    call unpack77(c77,0,decoded,ok)
    if(.not.ok) then
       write(*,1040) trim(input)
1040   format('Dual-angle Type 1 unpack failure for "',a,'"')
       error stop 1
    endif
    if(trim(decoded).ne.expected) then
       write(*,1050) trim(input), trim(expected), trim(decoded)
1050   format('Dual-angle Type 1 placeholder failure for "',a,'"; expected "',a, &
              '"; got "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_dual_angle_type1_placeholders

  subroutine reset_packjt77_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    call clear_standard_state(mycall,dxcall)
  end subroutine reset_packjt77_state

  subroutine prime_hash_call(callsign)
    character(len=*), intent(in) :: callsign
    integer :: n10, n12, n22
    character(len=13) :: c13

    if(len_trim(callsign).le.0) return
    c13='             '
    c13=callsign
    call save_hash_call(c13,n10,n12,n22)
  end subroutine prime_hash_call

end program test_packjt77_invariants
