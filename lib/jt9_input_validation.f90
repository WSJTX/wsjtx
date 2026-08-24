module jt9_input_validation
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  implicit none

  private
  public :: parse_integer, parse_real, parse_wav_filename_nutc

contains

  subroutine parse_integer(text, value, ok)
    character(len=*), intent(in) :: text
    integer, intent(out) :: value
    logical, intent(out) :: ok

    integer :: ios

    value = 0
    if (.not. is_integer_token(text)) then
      ok = .false.
      return
    end if
    read(text, *, iostat=ios) value
    ok = ios == 0
  end subroutine parse_integer

  subroutine parse_real(text, value, ok)
    character(len=*), intent(in) :: text
    real(kind=8), intent(out) :: value
    logical, intent(out) :: ok

    integer :: ios

    value = 0.d0
    if (.not. is_real_token(text)) then
      ok = .false.
      return
    end if
    read(text, *, iostat=ios) value
    ok = ios == 0 .and. ieee_is_finite(value)
  end subroutine parse_real

  pure logical function is_integer_token(text)
    character(len=*), intent(in) :: text

    integer :: first

    first = 1
    if (len(text) > 0) then
      if (text(1:1) == '+' .or. text(1:1) == '-') first = 2
    end if
    is_integer_token = first <= len(text) .and. &
         verify(text(first:), '0123456789') == 0
  end function is_integer_token

  pure logical function is_real_token(text)
    character(len=*), intent(in) :: text

    integer :: digits, i, length

    is_real_token = .false.
    length = len(text)
    if (length == 0) return

    i = 1
    if (text(i:i) == '+' .or. text(i:i) == '-') i = i + 1
    if (i > length) return

    digits = 0
    do while (i <= length)
      if (text(i:i) < '0' .or. text(i:i) > '9') exit
      digits = digits + 1
      i = i + 1
    end do
    if (i <= length) then
      if (text(i:i) == '.') then
        i = i + 1
        do while (i <= length)
          if (text(i:i) < '0' .or. text(i:i) > '9') exit
          digits = digits + 1
          i = i + 1
        end do
      end if
    end if
    if (digits == 0) return

    if (i <= length) then
      if (text(i:i) /= 'e' .and. text(i:i) /= 'E') return
      i = i + 1
      if (i <= length) then
        if (text(i:i) == '+' .or. text(i:i) == '-') i = i + 1
      end if
      if (i > length) return
      digits = 0
      do while (i <= length)
        if (text(i:i) < '0' .or. text(i:i) > '9') return
        digits = digits + 1
        i = i + 1
      end do
      if (digits == 0) return
    end if

    is_real_token = i > length
  end function is_real_token

  subroutine parse_wav_filename_nutc(filename, nutc)
    character(len=*), intent(in) :: filename
    integer, intent(out) :: nutc

    integer :: dot, ios, length
    character(len=:), allocatable :: stem

    nutc = 0
    length = len_trim(filename)
    if (length == 0) return

    dot = index(filename(:length), '.', back=.true.)
    if (dot == 0) return
    if (.not. is_wav_extension(filename(dot:length))) return

    stem = filename(:dot - 1)
    if (len(stem) >= 6) then
      if (all_digits(stem(len(stem) - 5:))) then
        read(stem(len(stem) - 5:), *, iostat=ios) nutc
        if (ios == 0) return
      end if
    end if
    if (len(stem) >= 5) then
      if (stem(len(stem) - 4:len(stem) - 4) == '_' .and. &
           all_digits(stem(len(stem) - 3:))) then
        read(stem(len(stem) - 3:), *, iostat=ios) nutc
        if (ios == 0) return
      end if
    end if
    nutc = 0
  end subroutine parse_wav_filename_nutc

  pure logical function is_wav_extension(extension)
    character(len=*), intent(in) :: extension

    is_wav_extension = len(extension) == 4 .and. &
         (extension == '.wav' .or. extension == '.WAV')
  end function is_wav_extension

  pure logical function all_digits(text)
    character(len=*), intent(in) :: text

    integer :: i

    all_digits = len(text) > 0
    do i = 1, len(text)
      if (text(i:i) < '0' .or. text(i:i) > '9') then
        all_digits = .false.
        return
      end if
    end do
  end function all_digits

end module jt9_input_validation
