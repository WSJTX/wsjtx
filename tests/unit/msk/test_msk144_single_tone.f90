! SPDX-License-Identifier: GPL-3.0-or-later
! MSK144 "@" single tones: the tone the operator asks for, whatever the
! generator encoded before, and msgsent says what was asked for.
program test_msk144_single_tone
  implicit none
  external :: genmsk_128_90
  character*37 :: msg, msgsent
  integer*4 :: tones(144), reference(144)
  integer :: itype

  call encode('CQ K1ABC FN42', 0, reference)
  call assert_true(all(reference.eq.0 .or. reference.eq.1), &
       'a standard message keys tones 0 and 1')

  call encode('@1000', 0, tones)
  call assert_tone(1000, '@1000', 'a first single tone')

  call encode('CQ K1ABC FN42', 0, tones)
  call encode('@1500', 0, tones)
  call assert_tone(1500, '@1500', 'a single tone after a standard message')

  call encode('<K1ABC W9XYZ> R-03', 0, tones)
  call encode('@1700', 0, tones)
  call assert_tone(1700, '@1700', 'a single tone after a short message')

  call encode('@', 0, tones)
  call assert_tone(1000, '@', 'a bare @ keys 1000 Hz')
  call encode('@ABCD', 0, tones)
  call assert_tone(1000, '@ABCD', 'an unreadable frequency keys 1000 Hz')

  call encode('@1200', 1, tones)
  call assert_true(msgsent.eq.'@1200', 'a check-only call returns msgsent')

  call encode('CQ K1ABC FN42', 0, tones)
  call assert_true(all(tones.eq.reference), &
       'a standard message after single tones keys the same tones')

  print *, 'MSK144 single-tone tests passed'

contains

  subroutine encode(text, ichk, out)
    character(len=*), intent(in) :: text
    integer, intent(in) :: ichk
    integer*4, intent(inout) :: out(144)

    msg = text
    msgsent = 'unchanged'
    itype = 0
    call genmsk_128_90(msg, ichk, msgsent, out, itype)
  end subroutine encode

  subroutine assert_tone(hz, sent, label)
    integer, intent(in) :: hz
    character(len=*), intent(in) :: sent, label

    if(tones(1).ne.hz) then
      write(*,'(a,": tone ",i0,", expected ",i0)') label, tones(1), hz
      error stop 1
    endif
    call assert_true(msgsent.eq.sent, label//': msgsent')
  end subroutine assert_tone

  subroutine assert_true(condition, label)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: label

    if(.not.condition) then
      write(*,'(a)') label
      error stop 1
    endif
  end subroutine assert_true

end program test_msk144_single_tone
