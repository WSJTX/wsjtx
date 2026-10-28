program test_jt9_input_validation
  use jt9_input_validation, only: parse_integer, parse_real, parse_fixed_text, parse_wav_filename_nutc, submode_limit
  implicit none

  integer :: integer_value, nutc
  real(kind=8) :: real_value
  logical :: ok, includes_seconds
  character(len=12) :: call_value
  character(len=6) :: grid_value

  call assert_true(submode_limit(9) == 7, 'JT9 accepts A-H')
  call assert_true(submode_limit(4) == 6, 'JT4 accepts A-G')
  call assert_true(submode_limit(65) == 2, 'JT65 accepts A-C')
  call assert_true(submode_limit(66) == 5, 'Q65 accepts A-F')
  call assert_true(submode_limit(8) == 7, 'modes without submodes retain the token range')

  call parse_fixed_text(' W9XYZ ', 7, call_value, ok)
  call assert_true(ok .and. call_value == 'W9XYZ', 'surrounding spaces are removed')

  call parse_fixed_text(achar(9) // 'PJ4/K1ABC' // achar(9), 11, call_value, ok)
  call assert_true(ok .and. call_value == 'PJ4/K1ABC', 'surrounding tabs preserve portable calls')

  call parse_fixed_text(' PJ4/K1ABC/P1 ', 14, call_value, ok)
  call assert_true(ok .and. call_value == 'PJ4/K1ABC/P1', 'callsign width is checked after normalization')

  call parse_fixed_text(' EN37aa ', 8, grid_value, ok)
  call assert_true(ok .and. grid_value == 'EN37aa', 'grid width is checked after normalization')

  call parse_fixed_text(' K1ABC/P123456 ', 15, call_value, ok)
  call assert_true(.not. ok, 'normalized overlong callsign is rejected')

  call parse_fixed_text(' EN37abc ', 9, grid_value, ok)
  call assert_true(.not. ok, 'normalized overlong grid is rejected')

  call parse_fixed_text('K1 ABC', 6, call_value, ok)
  call assert_true(.not. ok, 'embedded spaces are rejected')

  call parse_fixed_text('EN' // achar(9) // '37', 5, grid_value, ok)
  call assert_true(.not. ok, 'embedded tabs are rejected')

  call parse_fixed_text(' b ', 3, call_value, ok)
  call assert_true(ok .and. call_value == 'b', 'padded blank-call sentinel is normalized')

  call parse_fixed_text(' ' // achar(9) // achar(10) // achar(11) // achar(12) // achar(13), 6, call_value, ok)
  call assert_true(ok .and. call_value == ' ', 'ASCII whitespace-only text produces a blank field')

  call parse_fixed_text('PJ4/K1ABC', 9, call_value, ok)
  call assert_true(ok .and. call_value == 'PJ4/K1ABC', 'portable prefix is preserved')

  call parse_fixed_text('K1ABC/P', 7, call_value, ok)
  call assert_true(ok .and. call_value == 'K1ABC/P', 'portable suffix is preserved')

  call parse_fixed_text('PJ4/K1ABC/P1', 12, call_value, ok)
  call assert_true(ok .and. call_value == 'PJ4/K1ABC/P1', 'exact-width callsign is preserved')

  call parse_fixed_text('K1ABC/P123456', 13, call_value, ok)
  call assert_true(.not. ok, 'overlong callsign is rejected')

  call parse_fixed_text('EN37aa', 6, grid_value, ok)
  call assert_true(ok .and. grid_value == 'EN37aa', 'exact-width grid preserves case')

  call parse_fixed_text('EN/37', 5, grid_value, ok)
  call assert_true(ok .and. grid_value == 'EN/37', 'grid option is read literally')

  call parse_fixed_text('EN37abc', 7, grid_value, ok)
  call assert_true(.not. ok, 'overlong grid is rejected')

  call parse_fixed_text('b', 1, call_value, ok)
  call assert_true(ok .and. call_value == 'b', 'blank-call sentinel reaches caller unchanged')

  call parse_fixed_text('', 0, grid_value, ok)
  call assert_true(ok .and. grid_value == ' ', 'empty text produces a blank field')

  call parse_fixed_text('truncated buffer', 501, call_value, ok)
  call assert_true(.not. ok, 'actual length is checked before slicing the option buffer')

  call parse_integer('42', integer_value, ok)
  call assert_true(ok .and. integer_value == 42, 'integer parsing accepts integers')

  call parse_integer('-42', integer_value, ok)
  call assert_true(ok .and. integer_value == -42, 'integer parsing accepts signs')

  call parse_integer('not-a-number', integer_value, ok)
  call assert_true(.not. ok, 'integer parsing rejects text')

  call parse_integer('42x', integer_value, ok)
  call assert_true(.not. ok, 'integer parsing requires one complete token')

  call parse_real('7.5', real_value, ok)
  call assert_true(ok .and. real_value == 7.5d0, 'real parsing accepts finite values')

  call parse_real('60', real_value, ok)
  call assert_true(ok .and. real_value == 60.d0, 'real parsing accepts whole numbers')

  call parse_real('7.5e1', real_value, ok)
  call assert_true(ok .and. real_value == 75.d0, 'real parsing accepts decimal exponents')

  call parse_real('NaN', real_value, ok)
  call assert_true(.not. ok, 'real parsing rejects NaN')

  call parse_real('7.5x', real_value, ok)
  call assert_true(.not. ok, 'real parsing requires one complete token')

  call parse_wav_filename_nutc('240101_1234.wav', nutc)
  call assert_true(nutc == 1234, 'four-digit timestamp is read')

  call parse_wav_filename_nutc('240101_123456.WAV', nutc)
  call assert_true(nutc == 123456, 'six-digit timestamp is read')

  call parse_wav_filename_nutc('240101_001500.wav', nutc, includes_seconds)
  call assert_true(nutc == 1500 .and. includes_seconds, 'midnight HHMMSS keeps its format')

  call parse_wav_filename_nutc('240101_1500.wav', nutc, includes_seconds)
  call assert_true(nutc == 1500 .and. .not. includes_seconds, 'HHMM remains distinguishable from HHMMSS')

  call parse_wav_filename_nutc('recording2026.wav', nutc)
  call assert_true(nutc == 0, 'four-digit suffix without separator defaults to midnight')

  call parse_wav_filename_nutc('valid-audio', nutc)
  call assert_true(nutc == 0, 'extensionless files default to midnight')

  call parse_wav_filename_nutc('a.wav', nutc)
  call assert_true(nutc == 0, 'short names default to midnight')

contains

  subroutine assert_true(condition, message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if (.not. condition) then
      write(*, '(A)') message
      error stop 1
    end if
  end subroutine assert_true

end program test_jt9_input_validation
