module packjt77_test_helpers

  use packjt77
  use packjt77_schema
  implicit none
  private :: reset_packjt77_state, prime_hash_call, prime_hash_call_var, &
       store_rx_hash_var, wipe_shared_hash_tables, set_standard_call_state, &
       set_var_call_state

  type pack77_result
     logical :: encoded=.false.
     integer :: status=PACK77_STATUS_NOT_ENCODED
     integer :: i3=-1
     integer :: n3=-1
     character(len=77) :: c77=' '
  end type pack77_result

contains

  subroutine wipe_shared_hash_tables()
    calls10=''
    calls12=''
    calls22=''
    recent_calls=''
    ihash22=-1
    nzhash=0
  end subroutine wipe_shared_hash_tables

  subroutine set_standard_call_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    mycall13='             '
    dxcall13='             '
    mycall13_configured='             '
    dxcall13_configured='             '
    mycall13=mycall
    dxcall13=dxcall
    mycall13_configured=mycall
    dxcall13_configured=dxcall
  end subroutine set_standard_call_state

  subroutine set_var_call_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    queued_calls_by_thread=''
    queued_recent_calls_by_thread=''
    nqueued_calls_by_thread=0
    nqueued_recent_calls_by_thread=0
    mycall13='             '
    dxcall13='             '
    mycall13_configured_prev='             '
    dxcall13_configured_prev='             '
    mycall13_configured_set=.false.
    dxcall13_configured_set=.false.
    hashmy10_configured=-1
    hashmy12_configured=-1
    hashmy22_configured=-1
    hashdx10_configured=-1
    mycall13=mycall
    dxcall13=dxcall
    if(len_trim(mycall).gt.2) then
       mycall13_configured_set=.true.
       mycall13_configured_prev=mycall13
       hashmy10_configured=ihashcall(mycall13,10)
       hashmy12_configured=ihashcall(mycall13,12)
       hashmy22_configured=ihashcall(mycall13,22)
    endif
    if(len_trim(dxcall).gt.2) then
       dxcall13_configured_set=.true.
       dxcall13_configured_prev=dxcall13
       hashdx10_configured=ihashcall(dxcall13,10)
    endif
  end subroutine set_var_call_state

  subroutine clear_standard_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    call wipe_shared_hash_tables()
    call set_standard_call_state(mycall,dxcall)
  end subroutine clear_standard_state

  subroutine clear_var_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    call wipe_shared_hash_tables()
    call set_var_call_state(mycall,dxcall)
  end subroutine clear_var_state

  subroutine clear_all_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    call wipe_shared_hash_tables()
    call set_standard_call_state(mycall,dxcall)
    call set_var_call_state(mycall,dxcall)
  end subroutine clear_all_state

  subroutine normalize_call(callsign,c13)
    character(len=*), intent(in) :: callsign
    character(len=13), intent(out) :: c13
    integer :: i

    c13='             '
    c13=callsign
    if(c13(1:1).eq.'<') c13=c13(2:)
    i=index(c13,'>')
    if(i.gt.0) c13(i:)='             '
  end subroutine normalize_call

  subroutine assert_message_type(label,input,want_i3,want_n3,got_i3,got_n3)
    character(len=*), intent(in) :: label, input
    integer, intent(in) :: want_i3, want_n3, got_i3, got_n3

    if(got_i3.ne.want_i3 .or. got_n3.ne.want_n3) then
       write(*,1000) trim(label), trim(input), want_i3, want_n3, got_i3, got_n3
1000   format(a,' message type failure for "',a,'"; wanted ',i0,'.',i0, &
              ' got ',i0,'.',i0)
       error stop 1
    endif
  end subroutine assert_message_type

  subroutine assert_binary_payload(label,input,c77)
    character(len=*), intent(in) :: label, input
    character(len=77), intent(in) :: c77

    if(.not.is_binary_payload(c77)) then
       write(*,1010) trim(label), trim(input), c77
1010   format(a,' emitted non-binary payload for "',a,'": ',a)
       error stop 1
    endif
  end subroutine assert_binary_payload

  subroutine assert_pack77_result(label,input,encoded)
    character(len=*), intent(in) :: label, input
    type(pack77_result), intent(in) :: encoded

    if(.not.encoded%encoded) then
       write(*,1015) trim(label), trim(input)
1015   format(a,' failed to encode "',a,'"')
       error stop 1
    endif
    if(encoded%i3.lt.0 .or. encoded%i3.gt.7 .or. encoded%n3.lt.0 .or. &
         encoded%n3.gt.7) then
       write(*,1016) trim(label), trim(input), encoded%i3, encoded%n3
1016   format(a,' emitted invalid type for "',a,'": ',i0,'.',i0)
       error stop 1
    endif
    call assert_binary_payload(label,input,encoded%c77)
  end subroutine assert_pack77_result

  type(pack77_result) function pack77_result_from_api(msg,options) result(encoded)
    character(len=*), intent(in) :: msg
    type(pack77_options), intent(in), optional :: options

    encoded=pack77_result()
    call pack77(msg,encoded%i3,encoded%n3,encoded%c77,options, &
         status=encoded%status)
    encoded%encoded=encoded%i3.ge.0
  end function pack77_result_from_api

  type(pack77_result) function pack77_legacy_result_from_api(msg,options) &
       result(encoded)
    character(len=*), intent(in) :: msg
    type(pack77_options), intent(in), optional :: options

    encoded=pack77_result()
    call pack77_legacy_truncating_fallback(msg,encoded%i3,encoded%n3, &
         encoded%c77,options,status=encoded%status)
    encoded%encoded=encoded%i3.ge.0
  end function pack77_legacy_result_from_api

  type(pack77_result) function pack77_with_hint(msg,pack_i3,pack_n3) result(encoded)
    character(len=*), intent(in) :: msg
    integer, intent(in) :: pack_i3, pack_n3

    encoded=pack77_result()
    if(pack_i3.eq.0 .and. pack_n3.eq.5) then
       encoded=strict_telemetry_result_from_schema(msg)
    else if(pack_i3.eq.0 .and. pack_n3.eq.6) then
       encoded=pack77_result_from_api(msg, &
            pack77_options(prefer_wspr_50bit=.true.))
    else
       encoded=pack77_result_from_api(msg)
    endif
  end function pack77_with_hint

  type(pack77_result) function strict_telemetry_result_from_schema(msg) &
       result(encoded)
    character(len=*), intent(in) :: msg
    character(len=37) :: text
    character(len=18) :: c18
    character(len=1) :: ch
    integer :: i,n
    integer :: ntel(3)
    logical :: ok

    encoded=pack77_result()
    text=adjustl(msg)
    n=len_trim(text)
    if(n.lt.1 .or. n.gt.18) return
    do i=1,n
       ch=text(i:i)
       if(ch.ge.'a' .and. ch.le.'z') ch=char(ichar(ch)-32)
       if(index('0123456789ABCDEF',ch).eq.0) return
       text(i:i)=ch
    enddo

    c18=text(1:n)
    c18=adjustr(c18)
    ntel=-99
    read(c18,1005,err=6) ntel
1005 format(3z6)
6   if(ntel(1).lt.0 .or. ntel(2).lt.0 .or. ntel(3).lt.0) return

    call encode_pack77_telemetry(ntel(1),ntel(2),ntel(3),encoded%c77,ok)
    if(ok) then
       encoded%encoded=.true.
       encoded%status=PACK77_STATUS_ENCODED
       encoded%i3=0
       encoded%n3=5
    endif
  end function strict_telemetry_result_from_schema

  logical function is_binary_payload(c77)
    character(len=77), intent(in) :: c77
    integer :: i

    is_binary_payload=.true.
    do i=1,77
       if(c77(i:i).ne.'0' .and. c77(i:i).ne.'1') then
          is_binary_payload=.false.
          return
       endif
    enddo
  end function is_binary_payload

  subroutine assert_decode(label,input,expected,decoded,ok)
    character(len=*), intent(in) :: label, input, expected, decoded
    logical, intent(in) :: ok

    if(.not.ok) then
       write(*,1020) trim(label), trim(input)
1020   format(a,' unpack failure for "',a,'"')
       error stop 1
    endif
    if(trim(decoded).ne.expected) then
       write(*,1030) trim(label), trim(input), trim(expected), trim(decoded)
1030   format(a,' decode failure for "',a,'"; expected "',a,'"; got "',a,'"')
       error stop 1
    endif
  end subroutine assert_decode

  subroutine assert_text_equal(label,left,right)
    character(len=*), intent(in) :: label, left, right

    if(trim(left).ne.trim(right)) then
       write(*,1040) trim(label), trim(left), trim(right)
1040   format(a,' mismatch; left "',a,'" right "',a,'"')
       error stop 1
    endif
  end subroutine assert_text_equal

  subroutine assert_canonical_text(label,input,canonical,decoded)
    character(len=*), intent(in) :: label, input, canonical, decoded

    if(canonical.ne.decoded) then
       write(*,1045) trim(label), trim(input), trim(canonical), trim(decoded)
1045   format(a,' canonical mismatch for "',a,'"; canonical "',a, &
              '"; decoded "',a,'"')
       error stop 1
    endif
  end subroutine assert_canonical_text

  logical function normalized_equal(left,right) result(equal)
    character(len=*), intent(in) :: left, right
    character(len=37) :: left_norm, right_norm

    call normalize_message(left,left_norm)
    call normalize_message(right,right_norm)
    equal=trim(left_norm).eq.trim(right_norm)
  end function normalized_equal

  logical function canonical_match(left,right) result(matches)
    character(len=*), intent(in) :: left, right
    character(len=37) :: left_tokens(19), right_tokens(19), wrapped
    integer :: left_n, right_n, i

    matches=.false.
    call split_tokens(left,left_tokens,left_n)
    call split_tokens(right,right_tokens,right_n)
    if(left_n.ne.right_n) return
    do i=1,left_n
       if(trim(left_tokens(i)).eq.trim(right_tokens(i))) cycle
       if(plausible_auto_hash_source(left_tokens(i))) then
          wrapped='                                     '
          wrapped='<'//trim(left_tokens(i))//'>'
          if(trim(right_tokens(i)).eq.trim(wrapped)) cycle
       endif
       if(trim(right_tokens(i)).eq.'<...>' .and. &
            hash_placeholder_source(left_tokens(i))) cycle
       return
    enddo
    matches=.true.
  end function canonical_match

  logical function telemetry_match(left,right) result(matches)
    character(len=*), intent(in) :: left, right
    character(len=37) :: left_tokens(19), right_tokens(19)
    integer :: left_n, right_n

    ! Telemetry messages allow a documented leading-zero canonical form.
    matches=.false.
    call split_tokens(left,left_tokens,left_n)
    call split_tokens(right,right_tokens,right_n)
    if(left_n.ne.1 .or. right_n.ne.1) return
    matches=telemetry_token_match(left_tokens(1),right_tokens(1))
  end function telemetry_match

  subroutine assert_call(label,got,expected)
    character(len=*), intent(in) :: label, got, expected

    if(trim(got).ne.expected) then
       write(*,1050) trim(label), trim(expected), trim(got)
1050   format(a,' failure; expected "',a,'"; got "',a,'"')
       error stop 1
    endif
  end subroutine assert_call

  subroutine assert_int(label,expected,got)
    character(len=*), intent(in) :: label
    integer, intent(in) :: expected, got

    if(got.ne.expected) then
       write(*,1060) trim(label), expected, got
1060   format(a,' failure; expected ',i0,' got ',i0)
       error stop 1
    endif
  end subroutine assert_int

  subroutine normalize_message(input_msg,normalized)
    character(len=*), intent(in) :: input_msg
    character(len=37), intent(out) :: normalized
    character(len=37) :: tokens(19)
    integer :: nwords, i

    normalized='                                     '
    call split_tokens(input_msg,tokens,nwords)
    do i=1,nwords
       if(len_trim(normalized).gt.0) normalized=trim(normalized)//' '
       normalized=trim(normalized)//trim(tokens(i))
    enddo
  end subroutine normalize_message

  subroutine split_tokens(input_msg,tokens,nwords)
    character(len=*), intent(in) :: input_msg
    character(len=37), intent(out) :: tokens(19)
    integer, intent(out) :: nwords
    character(len=1) :: c, previous
    integer :: i, n

    tokens='                                     '
    nwords=0
    n=0
    previous=' '
    do i=1,len_trim(input_msg)
       c=input_msg(i:i)
       if(ichar(c).eq.0) c=' '
       if(c.ge.'a' .and. c.le.'z') c=char(ichar(c)-32)
       if(c.eq.' ' .and. previous.eq.' ') cycle
       if(c.ne.' ' .and. previous.eq.' ') then
          nwords=nwords+1
          n=0
          if(nwords.gt.size(tokens)) return
       endif
       if(c.ne.' ') then
          n=n+1
          if(n.le.len(tokens(1))) tokens(nwords)(n:n)=c
       endif
       previous=c
    enddo
  end subroutine split_tokens

  logical function plausible_auto_hash_source(token) result(ok)
    character(len=*), intent(in) :: token
    integer :: i,n,slash

    n=len_trim(token)
    ok=.false.
    if(n.lt.3 .or. n.gt.11) return
    slash=index(token(1:n),'/')
    if(slash.lt.2 .or. slash.ge.n) return
    if(index(token(slash+1:n),'/').gt.0) return
    do i=1,n
       if(i.eq.slash) cycle
       if(.not.((token(i:i).ge.'A' .and. token(i:i).le.'Z') .or. &
            (token(i:i).ge.'0' .and. token(i:i).le.'9'))) return
    enddo
    ok=.true.
  end function plausible_auto_hash_source

  logical function hash_placeholder_source(token) result(ok)
    character(len=*), intent(in) :: token
    integer :: n

    n=len_trim(token)
    ok=plausible_auto_hash_source(token)
    if(ok) return
    if(n.lt.3 .or. n.gt.13) return
    ok=token(1:1).eq.'<' .and. token(n:n).eq.'>' .and. n-2.le.11
  end function hash_placeholder_source

  logical function telemetry_token_match(left,right) result(matches)
    character(len=*), intent(in) :: left, right
    character(len=18) :: left_hex, right_hex
    integer :: left_n, right_n

    matches=.false.
    left_hex=upper_token(left)
    right_hex=upper_token(right)
    left_n=len_trim(left_hex)
    right_n=len_trim(right_hex)
    if(left_n.lt.17 .or. left_n.gt.18) return
    if(right_n.lt.17 .or. right_n.gt.18) return
    if(.not.is_hex_token(left_hex,left_n)) return
    if(.not.is_hex_token(right_hex,right_n)) return
    if(left_n.eq.right_n .and. trim(left_hex).eq.trim(right_hex)) then
       matches=.true.
    else if(left_n.eq.right_n+1 .and. left_hex(1:1).eq.'0' .and. &
         left_hex(2:left_n).eq.right_hex(1:right_n)) then
       matches=.true.
    else if(right_n.eq.left_n+1 .and. right_hex(1:1).eq.'0' .and. &
         right_hex(2:right_n).eq.left_hex(1:left_n)) then
       matches=.true.
    endif
  end function telemetry_token_match

  logical function is_hex_token(token,n) result(ok)
    character(len=*), intent(in) :: token
    integer, intent(in) :: n
    integer :: i

    ok=.true.
    do i=1,n
       if(index('0123456789ABCDEF',token(i:i)).eq.0) then
          ok=.false.
          return
       endif
    enddo
  end function is_hex_token

  character(len=18) function upper_token(token) result(upper)
    character(len=*), intent(in) :: token
    integer :: i, n
    character :: c

    upper='                  '
    n=min(len_trim(token),len(upper))
    do i=1,n
       c=token(i:i)
       if(c.ge.'a' .and. c.le.'z') c=char(ichar(c)-32)
       upper(i:i)=c
    enddo
  end function upper_token

  subroutine assert_true(label,condition)
    character(len=*), intent(in) :: label
    logical, intent(in) :: condition

    if(.not.condition) then
       write(*,1070) trim(label)
1070   format(a,' failure')
       error stop 1
    endif
  end subroutine assert_true

  subroutine assert_shared_hash_tables_empty(label)
    character(len=*), intent(in) :: label

    call assert_int(trim(label)//' nzhash',0,nzhash)
    call assert_true(trim(label)//' calls10',all(calls10.eq.''))
    call assert_true(trim(label)//' calls12',all(calls12.eq.''))
    call assert_true(trim(label)//' calls22',all(calls22.eq.''))
    call assert_true(trim(label)//' recent calls',all(recent_calls.eq.''))
    call assert_true(trim(label)//' ihash22',all(ihash22.eq.-1))
    call assert_true(trim(label)//' queued hash counts', &
         all(nqueued_calls_by_thread.eq.0))
    call assert_true(trim(label)//' queued recent counts', &
         all(nqueued_recent_calls_by_thread.eq.0))
    call assert_true(trim(label)//' queued calls', &
         all(queued_calls_by_thread.eq.''))
    call assert_true(trim(label)//' queued recent calls', &
         all(queued_recent_calls_by_thread.eq.''))
  end subroutine assert_shared_hash_tables_empty

  subroutine run_packjt77_common_invariants(configured_var,ntests)
    logical, intent(in) :: configured_var
    integer, intent(inout) :: ntests

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
    call expect_round_trip('K1ABC W9XYZ +50', &
         'K1ABC W9XYZ +50', 1, 0, 0)
    call expect_pack77_failure('K1ABC W9XYZ -55')
    call expect_pack77_failure('K1ABC W9XYZ +51')
    call expect_round_trip('K1ABC W9XYZ R+00', &
         'K1ABC W9XYZ R+00', 1, 0, 0)
    call expect_round_trip('K1ABC W9XYZ R-31', &
         'K1ABC W9XYZ R-31', 1, 0, 0)
    call expect_round_trip('K1ABC W9XYZ R-50', &
         'K1ABC W9XYZ R-50', 1, 0, 0)
    call expect_round_trip('K1ABC W9XYZ R+50', &
         'K1ABC W9XYZ R+50', 1, 0, 0)
    call expect_pack77_failure('K1ABC W9XYZ R-55')
    call expect_pack77_failure('K1ABC W9XYZ R+51')
    call expect_round_trip('K1ABC W9XYZ R FN42', &
         'K1ABC W9XYZ R FN42', 1, 0, 0)
    call expect_pack77_failure('K1ABC W9XYZ FN42X')
    call expect_pack77_failure('K1ABC W9XYZ R FN42X')
    call expect_round_trip('K1ABC/P W9XYZ/P R FN42', &
         'K1ABC/P W9XYZ/P R FN42', 2, 0, 0)
    call expect_round_trip('K1ABC/P W9XYZ/P FN42', &
         'K1ABC/P W9XYZ/P FN42', 2, 0, 0)
    call expect_not_message_type('K1ABC/R W9XYZ/P FN42', 2, 0)
    call expect_not_message_type('K1ABC W9XYZ +3', 1, 0)
    call expect_round_trip('HELLO WORLD', &
         'HELLO WORLD', 0, 0, 0)
    call expect_round_trip('hello', &
         'HELLO', 0, 0, 0)
    call expect_round_trip('FREE TEXT MSG', &
         'FREE TEXT MSG', 0, 0, 0)
    call expect_round_trip('ABCDEFGHIJKLM', &
         'ABCDEFGHIJKLM', 0, 0, 0)
    call expect_pack77_failure('ABCDEFGHIJKLMN')
    call expect_pack77_failure('HELLO WORLD 73')
    call expect_pack77_failure('HELLO@WORLD')
    call expect_pack77_failure('K1ABC PJ4/K9XYZ')
    call expect_free_text_rejection('HELLO@WORLD', &
         PACK77_STATUS_FREE_TEXT_INVALID)
    call expect_free_text_rejection('THIS MESSAGE IS TOO LONG', &
         PACK77_STATUS_FREE_TEXT_TOO_LONG)
    call expect_pack77_structured('K1ABC W9XYZ FN42', 1, 0)
    call expect_free_text_rejection('K1ABC W9XYZ@', &
         PACK77_STATUS_FREE_TEXT_INVALID)
    call expect_free_text_rejection('ABCDEFGHIJKLMN', &
         PACK77_STATUS_FREE_TEXT_TOO_LONG)
    call expect_free_text_rejection('A:B', &
         PACK77_STATUS_FREE_TEXT_INVALID)
    call expect_free_text_fallback('K1ABC R BOGUS', 'K1ABC R BOGUS')
    call expect_legacy_truncating_fallback('ABCDEFGHIJKLMN', &
         'ABCDEFGHIJKLM')
    call expect_legacy_truncating_fallback('K1ABCW9XYZ FN42', &
         'K1ABCW9XYZ FN')
    call expect_legacy_matches_strict('K1ABC W9XYZ FN42')
    call expect_legacy_matches_strict('FREE TEXT MSG')
    call expect_legacy_fallback_failure('HELLO@WORLD 73', &
         PACK77_STATUS_FREE_TEXT_INVALID)
    call expect_round_trip('WA9XYZ KA1ABC R 16A EMA', &
         'WA9XYZ KA1ABC R 16A EMA', 0, 3, 0)
    call expect_round_trip('WA9XYZ KA1ABC R 32A EMA', &
         'WA9XYZ KA1ABC R 32A EMA', 0, 4, 0)
    call expect_round_trip('WA9XYZ KA1ABC 1A EMA', &
         'WA9XYZ KA1ABC 1A EMA', 0, 3, 0)
    call expect_round_trip('WA9XYZ KA1ABC R 1A EMA', &
         'WA9XYZ KA1ABC R 1A EMA', 0, 3, 0)
    call expect_round_trip_with_hashes('W1AW/P K1ABC 5A CT', &
         '<W1AW/P> K1ABC 5A CT', 0, 3, 0, 'W1AW/P', '', '')
    call expect_round_trip_with_hashes('ABCDEFG/HIJ KLMNOPQ/RST R 32A EMA', &
         '<ABCDEFG/HIJ> <KLMNOPQ/RST> R 32A EMA', 0, 4, 0, &
         'ABCDEFG/HIJ', 'KLMNOPQ/RST', '')
    call expect_round_trip('WA9XYZ KA1ABC 15F DX', &
         'WA9XYZ KA1ABC 15F DX', 0, 3, 0)
    call expect_round_trip('WA9XYZ KA1ABC 17A EMA', &
         'WA9XYZ KA1ABC 17A EMA', 0, 4, 0)
    call expect_round_trip_with_hashes('Y8W MW8WA/A R 17A ONE', &
         'Y8W <MW8WA/A> R 17A ONE', 0, 4, 0, 'MW8WA/A', '', '')
    call expect_round_trip('WA9XYZ KA1ABC R 32A NB', &
         'WA9XYZ KA1ABC R 32A NB', 0, 4, 0)
    call expect_pack77_failure('WA9XYZ KA1ABC R 1H EMA')
    call expect_round_trip('PJ2/W1AW KA1ABC R 1A EMA', &
         '<...> KA1ABC R 1A EMA', 0, 3, 0)
    call expect_pack77_failure('WA9XYZ KA1ABC R 0A EMA')
    call expect_pack77_failure('WA9XYZ KA1ABC R 33A EMA')
    call expect_pack77_failure('WA9XYZ KA1ABC R 1A XYZ')
    call expect_not_message_type('WA9XYZ KA1ABC +1A EMA', 0, 3)
    call expect_not_message_type('WA9XYZ KA1ABC 01A EMA', 0, 3)
    call expect_not_message_type('WA9XYZ KA1ABC R 1A EM', 0, 3)
    call expect_not_message_type('WA9XYZ KA1ABC X 1A EMA', 0, 3)
    call expect_not_message_type('<WA9XYZ> KA1ABC R 1A EMA', 0, 3)
    call expect_pack77_failure('K1ABC/ K1A 1A CT')
    call expect_pack77_failure('/K1ABC K1A 1A CT')
    call expect_pack77_failure('A//B K1A 1A CT')
    call expect_pack77_failure('K1ABC//P K1A 1A CT')
    call expect_pack77_failure('ABCDEFGHI/JKL MNOPQRSTU/VWX 1A EMA')
    call expect_round_trip('123456789ABCDEF012', &
         '123456789ABCDEF012', 0, 5, 0)
    call expect_round_trip_with_hint('0de', &
         '0000000000000000DE', 0, 5, 0, 5, 0, '', '', '')
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
    call expect_round_trip_with_hashes('W9XYZ W1AW/P R 599 MA', &
         'W9XYZ <W1AW/P> R 599 MA', 3, 0, 0, 'W1AW/P', '', '')
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
    call expect_round_trip('TU; W9XYZ G8ABC 529 0001', &
         'TU; W9XYZ G8ABC 529 0001', 3, 0, 0)
    call expect_pack77_failure('W9XYZ K1ABC 509 MA')
    call expect_pack77_failure('W9XYZ K1ABC R 519 MA')
    call expect_pack77_failure('W9XYZ K1ABC R 609 MA')
    call expect_pack77_failure('W9XYZ K1ABC R 579 XYZ')
    call expect_pack77_failure('W9XYZ K1ABC 579 MAX')
    call expect_pack77_failure('W9XYZ G8ABC R 559 13')
    call expect_pack77_failure('W9XYZ G8ABC R 559 0000')
    call expect_pack77_failure('W9XYZ G8ABC R 559 8000')
    call expect_not_message_type('W9XYZ K1ABC R 579 MA JUNK', 3, 0)
    call expect_not_message_type('<W9XYZ> K1ABC R 579 MA', 3, 0)
    call expect_not_message_type('TU; GF9/XJ4PIE GP5MZC/1 R 539 1843', 3, 0)
    call expect_raw_type3_unpack_failure(0)
    call expect_raw_type3_unpack_success(1, &
         'W9XYZ K1ABC R 579 0001')
    call expect_raw_type3_unpack_success(7999, &
         'W9XYZ K1ABC R 579 7999')
    call expect_raw_type3_unpack_failure(8000)
    call expect_raw_type3_unpack_failure(8191)
    call expect_nonbinary_unpack_failure()
    call expect_not_message_type('<W9XYZ> <K1ABC> R 579 MA', 3, 0)
    call expect_round_trip('CQ PJ4/K1ABC', &
         'CQ PJ4/K1ABC', 4, 0, 0)
    call expect_pack77_failure('CQ PJ4/K1ABC RR73')
    call expect_not_message_type('CQ PJ4/K1ABC FK52', 1, 0)
    call expect_round_trip('CQ N0CALL', &
         'CQ N0CALL', 4, 0, 0)
    call expect_round_trip('CQ N0CAL FN31', &
         'CQ N0CAL FN31', 1, 0, 0)
    call expect_not_message_type('CQ N0CALL FN31', 1, 0)
    call expect_not_message_type('CQ N0CALL FN31', 4, 0)
    call expect_pack77_failure('CQ N0CALL FN31')
    call expect_pack77_failure('K1ABC PJ4/K9XYZ')
    call expect_not_message_type('CQ 3DA0RS/W1ABC', 4, 0)
    call expect_round_trip('CQ K1ABC FN42', &
         'CQ K1ABC FN42', 1, 0, 0)
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
    call expect_round_trip('DL/K1ABC 37', &
         'DL/K1ABC 37', 0, 6, 0)
    call expect_round_trip('K1ABC/P 37', &
         'K1ABC/P 37', 0, 6, 0)
    call expect_not_message_type('00/K1ABC 37', 0, 6)
    call expect_not_message_type('K1ABC/00 37', 0, 6)
    call expect_round_trip_with_hint('<PJ4/K1ABC> FK52AB', &
         '<PJ4/K1ABC> FK52AB', 0, 6, 0, 6, 0, 'PJ4/K1ABC', '', '')
    call expect_not_message_type('PJ2/K1ABC FN42 37', 0, 6)
    call expect_not_message_type('K1ABC FN4 37', 0, 6)
    call expect_not_message_type('Q5A FN20 37', 0, 6)
    call expect_not_message_type('VK9X/W1ABC 37', 0, 6)
    call expect_not_message_type('A$/K1ABC 37', 0, 6)
    call expect_not_message_type('K1ABC/A$ 37', 0, 6)
    call expect_not_message_type('K1ABC/ABCD 37', 0, 6)
    call expect_not_message_type('PJ2/Q5A 37', 0, 6)
    call expect_not_message_type('K1ABC FN42 55', 0, 6)
    call expect_not_message_type('K1ABC FN42 03', 0, 6)
    call expect_not_message_type('<PJ4/K1ABC> FK52AB', 0, 6)
    call expect_strict_telemetry_rejects('K1ABC W9XYZ')
    call expect_strict_telemetry_rejects('')
    call expect_strict_telemetry_rejects('                   ')
    call expect_strict_telemetry_rejects('+1')
    call expect_strict_telemetry_rejects('-1')
    call expect_strict_telemetry_rejects('ABC DEF')
    call expect_strict_telemetry_rejects('123G')
    call expect_strict_telemetry_rejects('123456789ABCDEF0123')
    call expect_not_message_type('6666AAAAAAAA66666661ZZ', 0, 5)
    call expect_prefer_wspr_rejects_invalid_type3('<PJ4/K1ABC> FK52A!')
    call expect_prefer_wspr_rejects_invalid_type3('<PJ4/K1ABC>X FK52AB')
    call expect_legacy_prefer_wspr_rejects('PJ4/K1ABC FK52A!')

    call expect_round_trip_with_hashes('<PJ4/K1ABC> W9XYZ RR73', &
         '<PJ4/K1ABC> W9XYZ RR73', 1, 0, 0, 'PJ4/K1ABC', '', '')
    call expect_round_trip_with_hashes('<PJ4/K1ABC> W9XYZ RRR', &
         '<PJ4/K1ABC> W9XYZ RRR', 1, 0, 0, 'PJ4/K1ABC', '', '')
    call expect_round_trip_with_hashes('<PJ4/K1ABC> W9XYZ 73', &
         '<PJ4/K1ABC> W9XYZ 73', 1, 0, 0, 'PJ4/K1ABC', '', '')
    if(configured_var) then
       call expect_type_unpack_failure_with_hashes('PJ2/W1AW <W7ABC>', &
            4, 0, 0, 'W7ABC', '', '')
    else
       call expect_round_trip_with_hashes('PJ2/W1AW <W7ABC>', &
            'PJ2/W1AW <W7ABC>', 4, 0, 0, 'W7ABC', '', '')
    endif
    call expect_round_trip_with_hashes('PJ2/W1AW <W7ABC> RRR', &
         'PJ2/W1AW <W7ABC> RRR', 4, 0, 0, 'W7ABC', '', '')
    call expect_round_trip_with_hashes('PJ2/W1AW <W7ABC> RR73', &
         'PJ2/W1AW <W7ABC> RR73', 4, 0, 0, 'W7ABC', '', '')
    call expect_round_trip_with_hashes('PJ2/W1AW <W7ABC> 73', &
         'PJ2/W1AW <W7ABC> 73', 4, 0, 0, 'W7ABC', '', '')
    call expect_round_trip_with_hashes('<W7ABC> PJ2/W1AW RR73', &
         '<W7ABC> PJ2/W1AW RR73', 4, 0, 0, 'W7ABC', '', '')
    call expect_round_trip_with_context('PJ2/W1AW <W7ABC> 73', &
         'PJ2/W1AW <W7ABC> 73', 4, 0, 0, 'W7ABC', 'N0BBB', '', '', '')
    call expect_round_trip_with_context('<PJ4/K1ABC> W9XYZ RR73', &
         '<PJ4/K1ABC> W9XYZ RR73', 1, 0, 1, 'PJ4/K1ABC', 'W9XYZ', '', '', '')
    call expect_round_trip_with_hashes('<PJ4/K1ABC> W9XYZ -12', &
         '<PJ4/K1ABC> W9XYZ -12', 1, 0, 0, 'PJ4/K1ABC', '', '')
    call expect_not_message_type('<PJ4/K1ABC> W9XYZ -12', 4, 0)
    call expect_not_message_type('<PJ4/K1ABC>X W9XYZ RR73', 1, 0)
    call expect_not_message_type('W9XYZ <PJ4/K1ABC>X RR73', 1, 0)
    call expect_not_message_type('K1ABC <W7ABC> RR73', 4, 0)
    call expect_not_message_type('<W7ABC> K1ABC RR73', 4, 0)
    call expect_not_message_type('<W7ABC> <PJ4/K1ABC> RR73', 4, 0)
    call expect_pack77_failure('CQ <PJ4/K1ABC>')

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
    call expect_not_message_type('<W3CCX> <K1JT/P> 590001 FN20Q', 5, 0)
    call expect_not_message_type('<W3CCX> <K1JT/P> X 590001 FN20QI', 5, 0)
    call expect_not_message_type('<W3CCX> <K1JT/P> RR 590001 FN20QI', 5, 0)
    call expect_not_message_type('W3CCX <K1JT/P> 590001 FN20QI', 5, 0)
    call expect_not_message_type('<W3CCX <K1JT/P> 590001 FN20QI', 5, 0)
    call expect_not_message_type('<W3CCX> <K1JT/P 590001 FN20QI', 5, 0)
    call expect_not_message_type('<ABCDEFGHIJK>X <K1JT/P> 590001 FN20QI', 5, 0)
    call expect_not_message_type('<W3CCX> <K1JT/P> 0590001 FN20QI', 5, 0)

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
    call expect_pack77_failure('K1ABC RR73; W9XYZ <KH1/KH7Z> -11')
    call expect_pack77_failure('K1ABC RR73; W9XYZ <KH1/KH7Z> -31')
    call expect_pack77_failure('K1ABC RR73; W9XYZ <KH1/KH7Z> +33')
    call expect_pack77_failure('K1ABC RR73; W9XYZ <KH1/KH7Z> +34')
    call expect_round_trip_with_context('K1ABC RR73; W9XYZ <KH1/KH7Z> -12', &
         'K1ABC RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, 0, &
         'KH1/KH7Z', 'N0BBB', '', '', '')
    call expect_not_message_type('K1ABC RR73: W9XYZ <KH1/KH7Z> -12', 0, 1)
    call expect_not_message_type('K1ABC RRR; W9XYZ <KH1/KH7Z> -12', 0, 1)
    call expect_round_trip_with_context('PJ2/W1AW RR73; W9XYZ <KH1/KH7Z> -12', &
         '<...> RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, 1, &
         'N0AAA', 'KH1/KH7Z', '', '', '')
    call expect_round_trip_with_context('K1ABC RR73; PJ2/W1AW <KH1/KH7Z> -12', &
         'K1ABC RR73; <...> <KH1/KH7Z> -12', 0, 1, 1, &
         'N0AAA', 'KH1/KH7Z', '', '', '')
    call expect_round_trip_with_hashes('PJ2/W1AW RR73; W9XYZ <KH1/KH7Z> -12', &
         '<PJ2/W1AW> RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, 0, &
         'PJ2/W1AW', 'KH1/KH7Z', '')
    call expect_pack77_failure('PJ2/W1AW RR73; W9/YZ <KH1/KH7Z> -12')
    call expect_not_message_type('<K1ABC> RR73; W9XYZ <KH1/KH7Z> -12', 0, 1)
    call expect_not_message_type('K1ABC RR73; W9XYZ KH1/KH7Z -12', 0, 1)
    call expect_not_message_type('K1ABC RR73; W9XYZ <KH1/KH7Z>X -12', 0, 1)
    call expect_not_message_type('K1ABC RR73; W9XYZ <KH1/KH7Z> 3', 0, 1)
    call expect_not_message_type('K1ABC RR73; W9XYZ <KH1/KH7Z> -12 JUNK', 0, 1)

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
    call expect_pack77_failure('CQ K1ABC R FN42')
    call expect_not_message_type('CQ K1A W1AW FN20', 1, 0)
    call expect_not_message_type('CQ TJ. K1ABC FN20', 1, 0)
    call expect_not_message_type('<PJ4/K1ABC> <W7ABC> TEST', 5, 0)
    call expect_not_message_type('<A/B> <C/D>', 1, 0)
    call expect_not_message_type('<A/B> <C/D>', 5, 0)

    call expect_dual_angle_type1_placeholders('<W1AW> <K1JT>', '<...> <...>')
    call expect_dual_angle_type1_placeholders('<W1AW> <K1JT> RR73', &
         '<...> <...> RR73')

  contains

    subroutine expect_round_trip(input,expected,want_i3,want_n3,nrx)
      character(len=*), intent(in) :: input, expected
      integer, intent(in) :: want_i3, want_n3, nrx

      call expect_round_trip_with_context(input,expected,want_i3,want_n3,nrx, &
           'N0AAA', 'N0BBB', '', '', '')
    end subroutine expect_round_trip

    subroutine expect_round_trip_with_hashes(input,expected,want_i3,want_n3, &
         nrx,hash_call_1,hash_call_2,hash_call_3)
      character(len=*), intent(in) :: input, expected
      integer, intent(in) :: want_i3, want_n3, nrx
      character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3

      call expect_round_trip_with_context(input,expected,want_i3,want_n3,nrx, &
           'N0AAA', 'N0BBB', hash_call_1, hash_call_2, hash_call_3)
    end subroutine expect_round_trip_with_hashes

    subroutine expect_round_trip_with_context(input,expected,want_i3,want_n3, &
         nrx,mycall,dxcall,hash_call_1,hash_call_2,hash_call_3)
      character(len=*), intent(in) :: input, expected
      integer, intent(in) :: want_i3, want_n3, nrx
      character(len=*), intent(in) :: mycall, dxcall
      character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3

      call expect_round_trip_with_pack_state(input,expected,want_i3,want_n3, &
           -1,-1,nrx,mycall,dxcall,hash_call_1,hash_call_2,hash_call_3)
    end subroutine expect_round_trip_with_context

    subroutine expect_round_trip_with_hint(input,expected,want_i3,want_n3, &
         pack_i3,pack_n3,nrx,hash_call_1,hash_call_2,hash_call_3)
      character(len=*), intent(in) :: input, expected
      integer, intent(in) :: want_i3, want_n3, pack_i3, pack_n3, nrx
      character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3

      call expect_round_trip_with_pack_state(input,expected,want_i3,want_n3, &
           pack_i3,pack_n3,nrx,'N0AAA','N0BBB',hash_call_1,hash_call_2, &
           hash_call_3)
    end subroutine expect_round_trip_with_hint

    subroutine expect_round_trip_with_pack_state(input,expected,want_i3,want_n3, &
         pack_i3,pack_n3,nrx,mycall,dxcall,hash_call_1,hash_call_2,hash_call_3)
      character(len=*), intent(in) :: input, expected
      integer, intent(in) :: want_i3, want_n3, pack_i3, pack_n3, nrx
      character(len=*), intent(in) :: mycall, dxcall
      character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3
      character(len=77) :: c77
      character(len=37) :: canonical, canonical_decoded, decoded, packed_input
      integer :: got_i3, got_n3
      logical :: canonical_ok, ok

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      got_i3=pack_i3
      got_n3=pack_n3
      c77=''
      block
        type(pack77_result) :: encoded
        encoded=pack77_with_hint(packed_input,pack_i3,pack_n3)
        call assert_pack77_result('pack77',input,encoded)
        call assert_true('pack77 gate not rejected',encoded%status.ne.PACK77_STATUS_INTERNAL_ROUNDTRIP_REJECTED)
        got_i3=encoded%i3
        got_n3=encoded%n3
        c77=encoded%c77
      end block

      call assert_message_type('pack77',input,want_i3,want_n3,got_i3,got_n3)
      canonical_decoded='                                     '
      canonical_ok=.false.
      call unpack77(c77,0,canonical_decoded,canonical_ok)
      call assert_decode('pack77 canonical',input,canonical_decoded, &
           canonical_decoded,canonical_ok)
      canonical=canonical_decoded
      call assert_canonical_text('pack77',input,canonical,canonical_decoded)

      call reset_packjt77_mode_state(mycall, dxcall)
      call prime_hash_call_mode(hash_call_1)
      call prime_hash_call_mode(hash_call_2)
      call prime_hash_call_mode(hash_call_3)

      decoded='                                     '
      ok=.false.
      call unpack77_mode(c77,nrx,decoded,ok)
      if(.not.ok) then
         write(*,1010) trim(input)
1010     format('Unpack failure for "',a,'"')
         error stop 1
      endif
      if(trim(decoded).ne.expected) then
         write(*,1020) trim(input), trim(expected), trim(decoded)
1020     format('Round-trip failure for "',a,'"; expected "',a,'"; got "',a,'"')
         error stop 1
      endif

      ntests=ntests+1
    end subroutine expect_round_trip_with_pack_state

    subroutine expect_not_message_type(input,blocked_i3,blocked_n3)
      character(len=*), intent(in) :: input
      integer, intent(in) :: blocked_i3, blocked_n3
      character(len=37) :: packed_input
      integer :: got_i3, got_n3

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      got_i3=-1
      got_n3=-1
      block
        type(pack77_result) :: encoded
        encoded=pack77_result_from_api(packed_input)
        if(.not.encoded%encoded) then
           ntests=ntests+1
           return
        endif
        got_i3=encoded%i3
        got_n3=encoded%n3
      end block
      if(got_i3.eq.blocked_i3 .and. got_n3.eq.blocked_n3) then
         write(*,1060) trim(input), blocked_i3, blocked_n3
1060     format('Message "',a,'" packed as blocked type ',i0,'.',i0)
         error stop 1
      endif

      ntests=ntests+1
    end subroutine expect_not_message_type

    subroutine expect_raw_type3_unpack_failure(nexch)
      integer, intent(in) :: nexch
      character(len=77) :: c77
      character(len=37) :: decoded
      logical :: ok, unpack_ok

      call encode_pack77_type3(0,12751800,10214965,1,5,nexch,c77,ok)
      call assert_true('raw Type 3 payload encoded',ok)

      call reset_packjt77_mode_state('N0AAA', 'N0BBB')
      decoded='                                     '
      unpack_ok=.true.
      call unpack77_mode(c77,0,decoded,unpack_ok)
      if(unpack_ok) then
         write(*,1080) nexch, trim(decoded)
1080     format('Raw Type 3 exchange ',i0,' decoded successfully as "',a,'"')
         error stop 1
      endif

      ntests=ntests+1
    end subroutine expect_raw_type3_unpack_failure

    subroutine expect_raw_type3_unpack_success(nexch,expected)
      integer, intent(in) :: nexch
      character(len=*), intent(in) :: expected
      character(len=77) :: c77
      character(len=37) :: decoded
      logical :: ok, unpack_ok

      call encode_pack77_type3(0,12751800,10214965,1,5,nexch,c77,ok)
      call assert_true('raw Type 3 payload encoded',ok)

      call reset_packjt77_mode_state('N0AAA', 'N0BBB')
      decoded='                                     '
      unpack_ok=.false.
      call unpack77_mode(c77,0,decoded,unpack_ok)
      call assert_decode('raw Type 3 exchange boundary',expected,expected, &
           decoded,unpack_ok)

      ntests=ntests+1
    end subroutine expect_raw_type3_unpack_success

    subroutine expect_nonbinary_unpack_failure()
      character(len=77) :: c77
      character(len=37) :: decoded
      logical :: ok

      c77=repeat('0',77)
      c77(10:10)='x'
      c77(75:77)='001'

      call reset_packjt77_mode_state('N0AAA', 'N0BBB')
      decoded='                                     '
      ok=.true.
      call unpack77_mode(c77,0,decoded,ok)
      if(ok) then
         write(*,1082) trim(decoded)
1082     format('Non-binary payload decoded successfully as "',a,'"')
         error stop 1
      endif

      ntests=ntests+1
    end subroutine expect_nonbinary_unpack_failure

    subroutine expect_pack77_failure(input)
      character(len=*), intent(in) :: input
      character(len=37) :: packed_input

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      block
        type(pack77_result) :: encoded
        encoded=pack77_result_from_api(packed_input)
        if(encoded%encoded) then
           write(*,1095) trim(input), encoded%i3, encoded%n3
1095       format('Message "',a,'" unexpectedly packed as ',i0,'.',i0)
           error stop 1
        endif
      end block

      ntests=ntests+1
    end subroutine expect_pack77_failure

    subroutine expect_free_text_rejection(input,want_status)
      character(len=*), intent(in) :: input
      integer, intent(in) :: want_status
      character(len=37) :: packed_input

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      block
        type(pack77_result) :: encoded
        encoded=pack77_result_from_api(packed_input)
        if(encoded%encoded) then
           write(*,1098) trim(input), encoded%i3, encoded%n3
1098       format('Free text "',a,'" unexpectedly packed as ',i0, &
                  '.',i0)
           error stop 1
        endif
        call assert_int('free text reject status '//trim(input), &
             want_status,encoded%status)
      end block

      ntests=ntests+1
    end subroutine expect_free_text_rejection

    subroutine expect_strict_telemetry_rejects(input)
      character(len=*), intent(in) :: input
      character(len=37) :: packed_input

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      block
        type(pack77_result) :: encoded
        encoded=strict_telemetry_result_from_schema(packed_input)
        if(encoded%encoded) then
           write(*,1097) trim(input), encoded%i3, encoded%n3
1097       format('Strict telemetry message "',a,'" unexpectedly packed as ', &
                  i0,'.',i0)
           error stop 1
        endif
      end block

      ntests=ntests+1
    end subroutine expect_strict_telemetry_rejects

    subroutine expect_prefer_wspr_rejects_invalid_type3(input)
      character(len=*), intent(in) :: input

      call expect_prefer_wspr_rejects_invalid_type3_impl(input)
    end subroutine expect_prefer_wspr_rejects_invalid_type3

    subroutine expect_prefer_wspr_rejects_invalid_type3_impl(input)
      character(len=*), intent(in) :: input
      character(len=37) :: packed_input
      character(len=24) :: prefix

      prefix='Preferred WSPR'
      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      block
        type(pack77_result) :: encoded
        encoded=pack77_result_from_api(packed_input, &
             pack77_options(prefer_wspr_50bit=.true.))
        if(encoded%encoded) then
           write(*,1096) trim(prefix), trim(input), encoded%i3, encoded%n3
1096       format(a,' message "',a,'" unexpectedly packed as ',i0,'.',i0)
           error stop 1
        endif
        call assert_true('failed preferred WSPR status', &
             encoded%status.eq.PACK77_STATUS_PREFERRED_FAMILY_REJECTED)
      end block

      ntests=ntests+1
    end subroutine expect_prefer_wspr_rejects_invalid_type3_impl

    subroutine expect_type_unpack_failure_with_hashes(input,want_i3,want_n3, &
         nrx,hash_call_1,hash_call_2,hash_call_3)
      character(len=*), intent(in) :: input
      integer, intent(in) :: want_i3, want_n3, nrx
      character(len=*), intent(in) :: hash_call_1, hash_call_2, hash_call_3
      character(len=77) :: c77
      character(len=37) :: decoded, packed_input
      integer :: got_i3, got_n3
      logical :: ok

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

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

      call reset_packjt77_mode_state('N0AAA', 'N0BBB')
      call prime_hash_call_mode(hash_call_1)
      call prime_hash_call_mode(hash_call_2)
      call prime_hash_call_mode(hash_call_3)

      decoded='                                     '
      ok=.true.
      call unpack77_mode(c77,nrx,decoded,ok)
      if(ok) then
         write(*,1100) trim(input), trim(decoded)
1100     format('Message "',a,'" decoded successfully as "',a,'"')
         error stop 1
      endif

      ntests=ntests+1
    end subroutine expect_type_unpack_failure_with_hashes

    subroutine expect_free_text_fallback(input,expected)
      character(len=*), intent(in) :: input, expected

      call expect_free_text_fallback_impl(input,expected)
    end subroutine expect_free_text_fallback

    subroutine expect_free_text_fallback_impl(input,expected)
      character(len=*), intent(in) :: input, expected
      character(len=77) :: c77
      character(len=37) :: canonical, decoded, packed_input
      character(len=40) :: pack_label
      character(len=16) :: prefix
      integer :: got_i3, got_n3
      logical :: ok

      pack_label='pack77'
      prefix='Free-text'
      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      got_i3=-1
      got_n3=-1
      c77=''
      block
        type(pack77_result) :: encoded
        encoded=pack77_result_from_api(packed_input)
        call assert_pack77_result(trim(pack_label),packed_input,encoded)
        got_i3=encoded%i3
        got_n3=encoded%n3
        c77=encoded%c77
      end block
      call assert_message_type(trim(pack_label),input,0,0,got_i3,got_n3)

      decoded='                                     '
      ok=.false.
      call unpack77_mode(c77,0,decoded,ok)
      if(.not.ok) then
         write(*,1070) trim(prefix), trim(input)
1070     format(a,' unpack failure for "',a,'"')
         error stop 1
      endif
      if(trim(decoded).ne.expected) then
         write(*,1085) trim(prefix), trim(input), trim(expected), trim(decoded)
1085     format(a,' failure for "',a,'"; expected "',a,'"; got "',a,'"')
         error stop 1
      endif
      canonical=decoded
      call assert_canonical_text('pack77 free text',input,canonical,decoded)

      ntests=ntests+1
    end subroutine expect_free_text_fallback_impl

    subroutine expect_legacy_truncating_fallback(input,expected)
      character(len=*), intent(in) :: input, expected
      character(len=37) :: decoded, packed_input
      logical :: ok

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      block
        type(pack77_result) :: strict_encoded, legacy_encoded
        strict_encoded=pack77_result_from_api(packed_input)
        if(strict_encoded%encoded) then
           write(*,1105) trim(input), strict_encoded%i3, strict_encoded%n3
1105       format('Strict message "',a,'" unexpectedly packed as ',i0,'.',i0)
           error stop 1
        endif

        legacy_encoded=pack77_legacy_result_from_api(packed_input)
        call assert_pack77_result('legacy truncating fallback', &
             packed_input,legacy_encoded)
        call assert_message_type('legacy truncating fallback',input, &
             0,0,legacy_encoded%i3,legacy_encoded%n3)

        decoded='                                     '
        ok=.false.
        call unpack77_mode(legacy_encoded%c77,0,decoded,ok)
        call assert_decode('legacy truncating fallback',input,expected, &
             decoded,ok)
      end block

      ntests=ntests+1
    end subroutine expect_legacy_truncating_fallback

    subroutine expect_legacy_matches_strict(input)
      character(len=*), intent(in) :: input
      character(len=37) :: packed_input

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      block
        type(pack77_result) :: strict_encoded, legacy_encoded
        strict_encoded=pack77_result_from_api(packed_input)
        legacy_encoded=pack77_legacy_result_from_api(packed_input)
        call assert_pack77_result('strict pack77',packed_input,strict_encoded)
        call assert_pack77_result('legacy wrapper',packed_input,legacy_encoded)
        call assert_int('legacy matches strict i3',strict_encoded%i3, &
             legacy_encoded%i3)
        call assert_int('legacy matches strict n3',strict_encoded%n3, &
             legacy_encoded%n3)
        call assert_text_equal('legacy matches strict c77', &
             strict_encoded%c77,legacy_encoded%c77)
      end block

      ntests=ntests+1
    end subroutine expect_legacy_matches_strict

    subroutine expect_legacy_fallback_failure(input,want_status)
      character(len=*), intent(in) :: input
      integer, intent(in) :: want_status
      character(len=37) :: packed_input

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      block
        type(pack77_result) :: encoded
        encoded=pack77_legacy_result_from_api(packed_input)
        if(encoded%encoded) then
           write(*,1110) trim(input), encoded%i3, encoded%n3
1110       format('Legacy fallback message "',a,'" unexpectedly packed as ', &
                  i0,'.',i0)
           error stop 1
        endif
        call assert_int('legacy fallback reject status '//trim(input), &
             want_status,encoded%status)
      end block

      ntests=ntests+1
    end subroutine expect_legacy_fallback_failure

    subroutine expect_legacy_prefer_wspr_rejects(input)
      character(len=*), intent(in) :: input
      character(len=37) :: packed_input

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      block
        type(pack77_result) :: encoded
        encoded=pack77_legacy_result_from_api(packed_input, &
             pack77_options(prefer_wspr_50bit=.true.))
        if(encoded%encoded) then
           write(*,1115) trim(input), encoded%i3, encoded%n3
1115       format('Legacy WSPR message "',a,'" unexpectedly packed as ', &
                  i0,'.',i0)
           error stop 1
        endif
        call assert_int('legacy preferred WSPR status', &
             PACK77_STATUS_PREFERRED_FAMILY_REJECTED,encoded%status)
      end block

      ntests=ntests+1
    end subroutine expect_legacy_prefer_wspr_rejects

    subroutine expect_pack77_structured(input,want_i3,want_n3)
      character(len=*), intent(in) :: input
      integer, intent(in) :: want_i3, want_n3
      character(len=37) :: packed_input

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      block
        type(pack77_result) :: encoded
        encoded=pack77_result_from_api(packed_input)
        call assert_pack77_result('pack77 structured', &
             packed_input,encoded)
        call assert_message_type('pack77 structured',input, &
             want_i3,want_n3,encoded%i3,encoded%n3)
      end block

      ntests=ntests+1
    end subroutine expect_pack77_structured

    subroutine expect_dual_angle_type1_placeholders(input,expected)
      character(len=*), intent(in) :: input, expected
      character(len=77) :: c77
      character(len=37) :: canonical, canonical_decoded, decoded, packed_input
      integer :: got_i3, got_n3
      logical :: canonical_ok, ok

      packed_input='                                     '
      packed_input=input
      call reset_packjt77_mode_state('N0AAA', 'N0BBB')

      c77=''
      block
        type(pack77_result) :: encoded
        encoded=pack77_result_from_api(packed_input)
        call assert_pack77_result('pack77',packed_input,encoded)
        got_i3=encoded%i3
        got_n3=encoded%n3
        c77=encoded%c77
      end block
      call assert_message_type('pack77',input,1,0,got_i3,got_n3)
      canonical_decoded='                                     '
      canonical_ok=.false.
      call unpack77(c77,0,canonical_decoded,canonical_ok)
      call assert_decode('dual-angle Type 1 canonical',input,canonical_decoded, &
           canonical_decoded,canonical_ok)
      canonical=canonical_decoded
      call assert_canonical_text('dual-angle Type 1',input,canonical, &
           canonical_decoded)

      call reset_packjt77_mode_state('N0AAA', 'N0BBB')
      decoded='                                     '
      ok=.false.
      call unpack77_mode(c77,0,decoded,ok)
      if(.not.ok) then
         write(*,1040) trim(input)
1040     format('Dual-angle Type 1 unpack failure for "',a,'"')
         error stop 1
      endif
      if(trim(decoded).ne.expected) then
         write(*,1050) trim(input), trim(expected), trim(decoded)
1050     format('Dual-angle Type 1 placeholder failure for "',a, &
                '"; expected "',a,'"; got "',a,'"')
         error stop 1
      endif

      ntests=ntests+1
    end subroutine expect_dual_angle_type1_placeholders

    subroutine reset_packjt77_mode_state(mycall,dxcall)
      character(len=*), intent(in) :: mycall, dxcall

      if(configured_var) then
         call reset_packjt77var_state(mycall,dxcall)
      else
         call reset_packjt77_state(mycall,dxcall)
      endif
    end subroutine reset_packjt77_mode_state

    subroutine prime_hash_call_mode(callsign)
      character(len=*), intent(in) :: callsign

      if(configured_var) then
         call prime_hash_call_var(callsign)
      else
         call prime_hash_call(callsign)
      endif
    end subroutine prime_hash_call_mode

    subroutine unpack77_mode(c77,nrx,decoded,ok)
      character(len=77), intent(in) :: c77
      integer, intent(in) :: nrx
      character(len=37), intent(out) :: decoded
      logical, intent(out) :: ok

      if(configured_var) then
         call unpack77_configured(c77,nrx,decoded,ok,unpack77_options(thread_index=1))
      else
         call unpack77(c77,nrx,decoded,ok)
      endif
    end subroutine unpack77_mode

  end subroutine run_packjt77_common_invariants

  subroutine run_packjt77_standard_schema_invariants(ntests)
    integer, intent(inout) :: ntests

    call expect_type12_qso_tail_mapping()

  contains

    subroutine expect_type12_qso_tail_mapping()
      call expect_type12_qso_tail_irpt('Type 1 no tail', &
           1,32400+1,'K1ABC W9XYZ')
      call expect_type12_qso_tail_irpt('Type 1 RRR', &
           1,32400+2,'K1ABC W9XYZ RRR')
      call expect_type12_qso_tail_irpt('Type 1 RR73', &
           1,32400+3,'K1ABC W9XYZ RR73')
      call expect_type12_qso_tail_irpt('Type 1 73', &
           1,32400+4,'K1ABC W9XYZ 73')

      call expect_type12_qso_tail_irpt('Type 2 no tail', &
           2,32400+1,'K1ABC/P W9XYZ/P')
      call expect_type12_qso_tail_irpt('Type 2 RRR', &
           2,32400+2,'K1ABC/P W9XYZ/P RRR')
      call expect_type12_qso_tail_irpt('Type 2 RR73', &
           2,32400+3,'K1ABC/P W9XYZ/P RR73')
      call expect_type12_qso_tail_irpt('Type 2 73', &
           2,32400+4,'K1ABC/P W9XYZ/P 73')

      ntests=ntests+1
    end subroutine expect_type12_qso_tail_mapping

    subroutine expect_type12_qso_tail_irpt(label,want_i3,want_igrid4,expected)
      character(len=*), intent(in) :: label, expected
      integer, intent(in) :: want_i3, want_igrid4
      character(len=37) :: decoded
      character(len=77) :: c77
      type(pack77_type12_fields) :: fields
      logical :: ok, unpack_ok

      ok=.false.
      if(want_i3.eq.1) then
         call encode_pack77_type1(10214965,0,12751800,0,0,want_igrid4,c77,ok)
      else
         call encode_pack77_type2(10214965,1,12751800,1,0,want_igrid4,c77,ok)
      endif
      call assert_true(trim(label)//' Type 1/2 schema encode',ok)

      fields=pack77_type12_fields()
      if(want_i3.eq.1) then
         call decode_pack77_type1(c77,fields,ok)
      else
         call decode_pack77_type2(c77,fields,ok)
      endif
      call assert_true(trim(label)//' Type 1/2 schema decode',ok)
      call assert_int(trim(label)//' igrid4',want_igrid4,fields%igrid4)

      call reset_packjt77_state('N0AAA', 'N0BBB')
      decoded='                                     '
      unpack_ok=.false.
      call unpack77(c77,0,decoded,unpack_ok)
      call assert_true(trim(label)//' unpack',unpack_ok)
      call assert_text_equal(trim(label)//' decoded tail',expected,decoded)
    end subroutine expect_type12_qso_tail_irpt

  end subroutine run_packjt77_standard_schema_invariants

  subroutine reset_packjt77_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall

    call clear_standard_state(mycall,dxcall)
  end subroutine reset_packjt77_state

  subroutine reset_packjt77var_state(mycall,dxcall)
    character(len=*), intent(in) :: mycall, dxcall
    integer :: n10, n12, n22

    call clear_all_state(mycall,dxcall)
    if(len_trim(mycall).gt.2) then
       call save_hash_call(mycall13,hashmy10_configured,hashmy12_configured, &
            hashmy22_configured)
    endif
    if(len_trim(dxcall).gt.2) then
       call prime_hash_call_var(dxcall)
       call save_hash_call(dxcall13,n10,n12,n22)
    endif
  end subroutine reset_packjt77var_state

  subroutine prime_hash_call(callsign)
    character(len=*), intent(in) :: callsign
    integer :: n10, n12, n22
    character(len=13) :: c13

    if(len_trim(callsign).le.0) return
    c13='             '
    c13=callsign
    call save_hash_call(c13,n10,n12,n22)
  end subroutine prime_hash_call

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
    if(n10.ge.0 .and. n10.le.1023) calls10(n10)=c13
    n12=ihashcall(c13,12)
    if(n12.ge.0 .and. n12.le.4095) calls12(n12)=c13
    n22=ihashcall(c13,22)
    do i=1,nzhash
       if(ihash22(i).eq.n22) then
          calls22(i)=c13
          return
       endif
    enddo
    if(nzhash.lt.MAXHASH) then
       if(nzhash.ge.1) then
          ihash22(nzhash+1:2:-1)=ihash22(nzhash:1:-1)
          calls22(nzhash+1:2:-1)=calls22(nzhash:1:-1)
       endif
       nzhash=nzhash+1
       ihash22(1)=n22
       calls22(1)=c13
    endif
  end subroutine store_rx_hash_var

end module packjt77_test_helpers
