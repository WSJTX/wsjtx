program test_jt9_input_validation
  use jt9_input_validation, only: parse_integer, parse_real, parse_wav_filename_nutc
  implicit none

  integer :: integer_value, nutc
  real(kind=8) :: real_value
  logical :: ok

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
