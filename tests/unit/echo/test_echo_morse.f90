program test_echo_morse

  use, intrinsic :: ieee_arithmetic
  use morse_encoder, only: encode_morse
  implicit none

  integer cwsim_bits(500),ncwsim,ntests

  ntests=0
  call expect_valid_morse('0Az/ ',ntests)
  call expect_invalid_morse('Z???',ntests)
  call expect_valid_morse(repeat('E',59)//'T',ntests,246)
  cwsim_bits=-1
  call encode_morse(repeat('0',22)//'EE',cwsim_bits,ncwsim)
  call expect_valid_bits(cwsim_bits,ncwsim,ntests,498)
  call expect_silent_wave(' ',ntests)
  call expect_silent_wave('?',ntests)
  call expect_signal_wave('O',ntests)

  write(*,1000) ntests
1000 format('Echo Morse tests passed: ',i0)

contains

  subroutine expect_valid_morse(message,ntests,expected_n)
    character(len=*), intent(in) :: message
    integer, intent(inout) :: ntests
    integer, intent(in), optional :: expected_n
    integer bits(250),n

    bits=-1
    call morse(message,bits,n)

    call expect_valid_bits(bits,n,ntests,expected_n)
  end subroutine expect_valid_morse

  subroutine expect_invalid_morse(message,ntests)
    character(len=*), intent(in) :: message
    integer, intent(inout) :: ntests
    integer bits(250),n

    bits=-1
    call morse(message,bits,n)

    if(n.ne.0 .or. any(bits.ne.0)) error stop 1
    ntests=ntests+1
  end subroutine expect_invalid_morse

  subroutine expect_valid_bits(bits,n,ntests,expected_n)
    integer, intent(in) :: bits(:),n
    integer, intent(inout) :: ntests
    integer, intent(in), optional :: expected_n

    if(n.le.0 .or. n.gt.size(bits)) error stop 1
    if(any(bits(1:n).lt.0) .or. any(bits(1:n).gt.1)) error stop 1
    if(present(expected_n)) then
       if(n.ne.expected_n) error stop 1
    endif
    ntests=ntests+1
  end subroutine expect_valid_bits

  subroutine expect_silent_wave(message,ntests)
    character(len=*), intent(in) :: message
    integer, intent(inout) :: ntests
    real*4, allocatable :: wave(:)

    allocate(wave(98304))
    wave=1.
    call gen_cw_wave(message,1500,wave)

    if(any(wave.ne.0.)) error stop 1
    ntests=ntests+1
  end subroutine expect_silent_wave

  subroutine expect_signal_wave(message,ntests)
    character(len=*), intent(in) :: message
    integer, intent(inout) :: ntests
    real*4, allocatable :: wave(:)

    allocate(wave(98304))
    wave=0.
    call gen_cw_wave(message,1500,wave)

    if(.not.all(ieee_is_finite(wave))) error stop 1
    if(maxval(abs(wave)).le.0.) error stop 1
    ntests=ntests+1
  end subroutine expect_signal_wave

end program test_echo_morse
