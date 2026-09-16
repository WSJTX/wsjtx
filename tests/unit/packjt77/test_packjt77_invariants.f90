program test_packjt77_invariants

  use packjt77_test_helpers
  implicit none

  integer :: ntests
  external :: genft8
  external :: genft4
  external :: genfst4
  external :: genmsk_128_90
  external :: genq65

  ntests=0

  call run_packjt77_common_invariants(.false.,ntests)
  call run_packjt77_standard_schema_invariants(ntests)
  call expect_callok_suffix_bounds(ntests)
  call expect_tx_generators_use_legacy_fallback(ntests)
  call expect_tx_generators_reject_invalid_fallback(ntests)
  call expect_fst4w_keeps_preferred_wspr_strict(ntests)

  write(*,1000) ntests
1000 format('packjt77 invariant tests passed: ',i0)

contains

  subroutine expect_callok_suffix_bounds(ntests)
    integer, intent(inout) :: ntests
    logical :: callok

    call assert_true('callok accepts normal suffix length',callok('K1ABC'))
    call assert_true('callok rejects overlength suffix',.not.callok('N0CALL'))

    ntests=ntests+1
  end subroutine expect_callok_suffix_bounds

  subroutine expect_tx_generators_use_legacy_fallback(ntests)
    integer, intent(inout) :: ntests
    character(len=37) :: input, msgsent
    integer :: i3, n3, iwspr, itype
    integer :: itone79(79), itone85(85), itone105(105), itone144(144)
    integer :: itone160(160)
    integer*1 :: msgbits77(77), msgbits101(101)

    input='ABCDEFGHIJKLMN'

    call genft8(input,i3,n3,msgsent,msgbits77,itone79)
    call assert_int('FT8 legacy fallback i3',0,i3)
    call assert_int('FT8 legacy fallback n3',0,n3)
    call assert_text_equal('FT8 legacy fallback message', &
         'ABCDEFGHIJKLM',msgsent)
    call assert_true('FT8 legacy fallback tones',any(itone79.ne.0))

    call genft4(input,0,msgsent,msgbits77,itone105)
    call assert_text_equal('FT4 legacy fallback message', &
         'ABCDEFGHIJKLM',msgsent)
    call assert_true('FT4 legacy fallback bits',any(msgbits77.ne.0))
    call assert_true('FT4 legacy fallback tones',any(itone105.ne.0))

    iwspr=0
    call genfst4(input,0,msgsent,msgbits101,itone160,iwspr)
    call assert_text_equal('FST4 legacy fallback message', &
         'ABCDEFGHIJKLM',msgsent)
    call assert_int('FST4 legacy fallback iwspr',0,iwspr)
    call assert_true('FST4 legacy fallback bits',any(msgbits101.ne.0))
    call assert_true('FST4 legacy fallback tones',any(itone160.ne.0))

    itype=1
    call genmsk_128_90(input,0,msgsent,itone144,itype)
    call assert_int('MSK144 legacy fallback type',1,itype)
    call assert_text_equal('MSK144 legacy fallback message', &
         'ABCDEFGHIJKLM',msgsent)
    call assert_true('MSK144 legacy fallback tones',any(itone144.ne.0))

    call genq65(input,0,msgsent,itone85,i3,n3,0)
    call assert_int('Q65 legacy fallback i3',0,i3)
    call assert_int('Q65 legacy fallback n3',0,n3)
    call assert_text_equal('Q65 legacy fallback message', &
         'ABCDEFGHIJKLM',msgsent)
    call assert_true('Q65 legacy fallback tones',any(itone85.ne.0))

    ntests=ntests+1
  end subroutine expect_tx_generators_use_legacy_fallback

  subroutine expect_fst4w_keeps_preferred_wspr_strict(ntests)
    integer, intent(inout) :: ntests
    character(len=37) :: input, msgsent
    integer :: iwspr
    integer :: itone160(160)
    integer*1 :: msgbits101(101)

    input='PJ4/K1ABC FK52A!'
    iwspr=1
    msgsent='                                     '
    msgbits101=1
    itone160=1

    call genfst4(input,0,msgsent,msgbits101,itone160,iwspr)

    call assert_int('FST4W strict iwspr',1,iwspr)
    call assert_text_equal('FST4W strict message','*** bad message ***', &
         msgsent)
    call assert_true('FST4W strict bits',all(msgbits101.eq.0))
    call assert_true('FST4W strict tones',all(itone160.eq.0))

    ntests=ntests+1
  end subroutine expect_fst4w_keeps_preferred_wspr_strict

  subroutine expect_tx_generators_reject_invalid_fallback(ntests)
    integer, intent(inout) :: ntests
    character(len=37) :: input, msgsent
    integer :: i3, n3, itype
    integer :: itone85(85), itone105(105), itone144(144)
    integer*1 :: msgbits77(77)

    input='$DX K1ABC FN42'

    msgsent='                                     '
    msgbits77=1
    itone105=1
    call genft4(input,1,msgsent,msgbits77,itone105)
    call assert_text_equal('FT4 invalid fallback message', &
         '*** bad message ***',msgsent)
    call assert_true('FT4 invalid fallback bits',all(msgbits77.eq.0))
    call assert_true('FT4 invalid fallback tones',all(itone105(1:103).eq.0))

    itype=1
    msgsent='                                     '
    itone144=1
    call genmsk_128_90(input,0,msgsent,itone144,itype)
    call assert_int('MSK144 invalid fallback type',-1,itype)
    call assert_text_equal('MSK144 invalid fallback message', &
         '*** bad message ***',msgsent)
    call assert_true('MSK144 invalid fallback tones',all(itone144.eq.0))

    i3=0
    n3=0
    msgsent='                                     '
    itone85=1
    call genq65(input,0,msgsent,itone85,i3,n3,0)
    call assert_int('Q65 invalid fallback i3',-1,i3)
    call assert_int('Q65 invalid fallback n3',-1,n3)
    call assert_text_equal('Q65 invalid fallback message', &
         '*** bad message ***',msgsent)
    call assert_true('Q65 invalid fallback tones',all(itone85.eq.0))

    ntests=ntests+1
  end subroutine expect_tx_generators_reject_invalid_fallback

end program test_packjt77_invariants
