program test_jt65_source_encoding
  use iso_c_binding, only: c_char, c_int
  use jt65_test_vectors
  implicit none

  interface
     subroutine gen65(message, ichk, sent, tones, itype) bind(C, name='gen65')
       import c_char, c_int
       character(kind=c_char) :: message(23), sent(23)
       integer(kind=c_int) :: ichk, tones(126), itype
     end subroutine gen65
  end interface

  call test_standard_message()
  call test_ooo_message()
  call test_message_forms()
  call test_shorthand_messages()
  print '(a)', 'JT65 source encoding tests passed'

contains

  subroutine encode(text, ichk, sent, tones, itype)
    character(len=*), intent(in) :: text
    integer, intent(in) :: ichk
    character(len=1), intent(out) :: sent(23)
    integer, intent(out) :: tones(jt65_symbol_count), itype
    character(len=1) :: message(23)
    integer :: i

    message = char(0)
    do i = 1, min(len_trim(text), 22)
       message(i) = text(i:i)
    end do
    sent = char(0)
    tones = -1
    itype = -1
    call gen65(message, ichk, sent, tones, itype)
  end subroutine encode

  subroutine require(condition, description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if (.not. condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    end if
  end subroutine require

  subroutine require_message(sent, expected, description)
    character(len=1), intent(in) :: sent(23)
    character(len=22), intent(in) :: expected
    character(len=*), intent(in) :: description
    character(len=22) :: actual
    integer :: i

    actual = ' '
    do i = 1, 22
       actual(i:i) = sent(i)
    end do
    call require(actual == expected, description)
    call require(ichar(sent(23)) == 0, description//' has a null terminator')
  end subroutine require_message

  subroutine require_tone_domain(tones, description)
    integer, intent(in) :: tones(jt65_symbol_count)
    character(len=*), intent(in) :: description

    call require(all(tones == 0 .or. (tones >= 2 .and. tones <= 65)), &
         description//' stays in the JT65 tone domain')
  end subroutine require_tone_domain

  subroutine test_standard_message()
    character(len=1) :: sent(23)
    integer :: tones(jt65_symbol_count), itype
    character(len=22) :: expected

    call encode('K1ABC W9XYZ FN42', 0, sent, tones, itype)
    expected = padded_message('K1ABC W9XYZ FN42')
    call require_message(sent, expected, 'standard message')
    call require(itype == 1, 'standard message type')
    call require(all(tones == standard_tones), 'standard message tone vector')
    call require_tone_domain(tones, 'standard message')
    call require(count(tones == 0) == 63, 'standard message sync-symbol count')
    call require(count(tones /= 0) == 63, 'standard message data-symbol count')
  end subroutine test_standard_message

  subroutine test_ooo_message()
    character(len=1) :: sent(23), normal_sent(23)
    integer :: tones(jt65_symbol_count), normal_tones(jt65_symbol_count), itype
    integer :: normal_data(63), ooo_data(63)
    character(len=22) :: expected
    integer :: i, normal_data_index, ooo_data_index

    call encode('K1ABC W9XYZ FN42', 0, normal_sent, normal_tones, itype)
    call encode('K1ABC W9XYZ FN42 OOO', 0, sent, tones, itype)
    expected = padded_message('K1ABC W9XYZ FN42 OOO')
    call require_message(sent, expected, 'OOO message')
    call require(itype == 1, 'OOO message type')
    call require(all(tones == ooo_tones), 'OOO message tone vector')
    call require_tone_domain(tones, 'OOO message')
    call require(count(tones == 0) == 63, 'OOO sync-symbol count')
    call require(count(tones /= 0) == 63, 'OOO data-symbol count')

    normal_data_index = 0
    ooo_data_index = 0
    do i = 1, jt65_symbol_count
       if (normal_tones(i) /= 0) then
          normal_data_index = normal_data_index + 1
          normal_data(normal_data_index) = normal_tones(i)
          call require(tones(i) == 0, 'OOO reverses the normal sync polarity')
       end if
       if (tones(i) /= 0) then
          ooo_data_index = ooo_data_index + 1
          ooo_data(ooo_data_index) = tones(i)
          call require(normal_tones(i) == 0, 'normal reverses the OOO sync polarity')
       end if
    end do
    call require(normal_data_index == 63, 'normal data count for OOO comparison')
    call require(ooo_data_index == 63, 'OOO data count for comparison')
    call require(all(normal_data == ooo_data), 'OOO preserves the data codeword')
  end subroutine test_ooo_message

  subroutine test_message_forms()
    character(len=1) :: sent(23)
    integer :: tones(jt65_symbol_count), itype
    character(len=22) :: expected

    call encode('1A/KA1ABC WB9XYZ', 0, sent, tones, itype)
    expected = padded_message('1A/KA1ABC WB9XYZ')
    call require_message(sent, expected, 'type 1 prefix message')
    call require(itype == 2, 'type 1 prefix message type')
    call require_tone_domain(tones, 'type 1 prefix message')

    call encode('KA1ABC WB9XYZ/P', 0, sent, tones, itype)
    expected = padded_message('KA1ABC WB9XYZ/P')
    call require_message(sent, expected, 'type 1 suffix message')
    call require(itype == 3, 'type 1 suffix message type')
    call require_tone_domain(tones, 'type 1 suffix message')

    call encode('CQ A000/KA1ABC FM07', 0, sent, tones, itype)
    expected = padded_message('CQ A000/KA1ABC FM07')
    call require_message(sent, expected, 'type 2 prefix message')
    call require(itype == 4, 'type 2 prefix message type')
    call require_tone_domain(tones, 'type 2 prefix message')

    call encode('CQ WB9XYZ/W4 FM07', 0, sent, tones, itype)
    expected = padded_message('CQ WB9XYZ/W4 FM07')
    call require_message(sent, expected, 'type 2 suffix message')
    call require(itype == 5, 'type 2 suffix message type')
    call require_tone_domain(tones, 'type 2 suffix message')

    call encode('HELLO WORLD', 0, sent, tones, itype)
    expected = padded_message('HELLO WORLD')
    call require_message(sent, expected, 'free text message')
    call require(itype == 6, 'free text message type')
    call require_tone_domain(tones, 'free text message')
  end subroutine test_message_forms

  subroutine test_shorthand_messages()
    character(len=1) :: sent(23)
    integer :: tones(jt65_symbol_count), itype
    character(len=22) :: expected
    integer :: i, group

    call encode('RO', 0, sent, tones, itype)
    expected = padded_message('RO')
    call require_message(sent, expected, 'RO shorthand')
    call require(itype == 7, 'RO shorthand type')
    do i = 1, jt65_symbol_count
       group = 1 + (i - 1) / 4
       call require(tones(i) == merge(0, 20, mod(group, 2) == 1), &
            'RO shorthand symbol group')
    end do

    call encode('RRR', 0, sent, tones, itype)
    expected = padded_message('RRR')
    call require_message(sent, expected, 'RRR shorthand')
    call require(itype == 7, 'RRR shorthand type')
    call require(all(tones(1:4) == 0), 'RRR shorthand first group')
    call require(all(tones(5:8) == 30), 'RRR shorthand second group')

    call encode('73', 0, sent, tones, itype)
    expected = padded_message('73')
    call require_message(sent, expected, '73 shorthand')
    call require(itype == 7, '73 shorthand type')
    call require(all(tones(1:4) == 0), '73 shorthand first group')
    call require(all(tones(5:8) == 40), '73 shorthand second group')

    call encode(' k1abc  w9xyz  fn42 ', 0, sent, tones, itype)
    expected = padded_message('K1ABC W9XYZ FN42')
    call require_message(sent, expected, 'message normalization')
    call require(itype == 1, 'normalized message type')

    call encode('K1ABC W9XYZ FN42    OO', 0, sent, tones, itype)
    expected = padded_message('K1ABC W9XYZ FN42 OOO')
    call require_message(sent, expected, 'legacy OO suffix')
    call require(itype == 1, 'legacy OO suffix type')
  end subroutine test_shorthand_messages

end program test_jt65_source_encoding
