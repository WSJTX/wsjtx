program test_tbcc

  ! Focused tests for the tail-biting convolutional code module
  ! (lib/jtty/tbcc.f90) at JTTY's chosen constraint length (K=10, nu=9):
  ! a noiseless encode/decode round trip, and checks that the optional
  ! reserved_zero_bit filter rejects a candidate and returns a deterministic
  ! payload on failure.

  use, intrinsic :: iso_fortran_env, only: real32, int32
  use tbcc
  implicit none

  call expect_encode_decode_roundtrip()
  call expect_reserved_zero_bit_filters_candidates()

  print *, 'test_tbcc: all checks passed'

contains

  subroutine expect_encode_decode_roundtrip()
    integer(int32) :: payload(PAYLOAD_BITS), decoded(PAYLOAD_BITS)
    integer(int32) :: tone_symbols(TOTAL_K)
    real(real32)   :: tone_energies(0:3, TOTAL_K)
    logical        :: success
    integer        :: i, t

    call tbcc_init(9)

    do i = 1, PAYLOAD_BITS
       payload(i) = mod(i, 2)   ! deterministic, not all-zero
    end do

    call tbcc_encode(payload, tone_symbols)

    ! Noiseless channel: all of a symbol's energy on the transmitted tone.
    tone_energies = 0.0_real32
    do t = 1, TOTAL_K
       tone_energies(tone_symbols(t), t) = 1.0_real32
    end do

    call tbcc_wava_fsk_decode(tone_energies, 4, 2, decoded, success)
    if (.not. success) then
       print *, 'test_tbcc: noiseless decode failed'
       error stop 1
    end if
    if (any(decoded /= payload)) then
       print *, 'test_tbcc: noiseless decode mismatch'
       error stop 1
    end if
  end subroutine expect_encode_decode_roundtrip

  subroutine expect_reserved_zero_bit_filters_candidates()
    integer(int32) :: payload(PAYLOAD_BITS), decoded(PAYLOAD_BITS)
    integer(int32) :: tone_symbols(TOTAL_K)
    real(real32)   :: tone_energies(0:3, TOTAL_K)
    logical        :: success
    integer        :: i, t, one_bit_pos

    call tbcc_init(9)

    do i = 1, PAYLOAD_BITS
       payload(i) = mod(i, 2)
    end do
    ! A payload-bit position this payload sets to 1, so requiring it to be
    ! 0 must reject this exact (otherwise noiseless-decodable) codeword.
    one_bit_pos = 0
    do i = 1, PAYLOAD_BITS
       if (payload(i) == 1) then
          one_bit_pos = i
          exit
       end if
    end do
    if (one_bit_pos == 0) then
       print *, 'test_tbcc: test payload unexpectedly all-zero'
       error stop 1
    end if

    call tbcc_encode(payload, tone_symbols)
    tone_energies = 0.0_real32
    do t = 1, TOTAL_K
       tone_energies(tone_symbols(t), t) = 1.0_real32
    end do

    ! Control case: no reserved_zero_bit, decode succeeds as above.
    call tbcc_wava_fsk_decode(tone_energies, 4, 2, decoded, success)
    if (.not. success) then
       print *, 'test_tbcc: control decode (no reserved_zero_bit) failed'
       error stop 1
    end if

    ! With the filter on a bit this payload actually sets to 1, the only
    ! codeword consistent with these noiseless tone energies fails the
    ! filter, so decoding must now report failure.
    decoded = huge(0_int32)
    call tbcc_wava_fsk_decode(tone_energies, 4, 2, decoded, success,   &
         reserved_zero_bit=one_bit_pos)
    if (success) then
       print *, 'test_tbcc: reserved_zero_bit did not filter the candidate'
       error stop 1
    end if
    if (any(decoded /= 0_int32)) then
       print *, 'test_tbcc: failed decode did not clear its payload'
       error stop 1
    end if
  end subroutine expect_reserved_zero_bit_filters_candidates

end program test_tbcc
