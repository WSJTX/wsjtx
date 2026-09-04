program test_tbcc

  ! Transition invariants across supported memories and JTTY candidate acceptance.

  use, intrinsic :: iso_fortran_env, only: real32, int32
  use tbcc
  implicit none

  call expect_incoming_tones()
  call expect_empty_and_tied_candidates()
  call expect_encode_decode_roundtrip()
  call expect_reserved_zero_bit_filters_candidates()

  print *, 'test_tbcc: all checks passed'

contains

  subroutine expect_incoming_tones()
    integer, parameter :: memories(5) = [9, 12, 10, 11, 9]
    integer, parameter :: gray_tones(0:3) = [0, 1, 3, 2]
    integer :: n, predecessor, bit, destination, register_value, expected

    do n = 1, size(memories)
       call tbcc_init(memories(n))
       if (size(incoming_tones, 2) /= num_states) error stop 'incorrect transition table size'
       do predecessor = 0, num_states-1
          do bit = 0, 1
             register_value = 2*predecessor + bit
             destination = mod(register_value, num_states)
             expected = gray_tones(2*mod(popcnt(iand(register_value, g0_poly)), 2) + &
                  mod(popcnt(iand(register_value, g1_poly)), 2))
             if (incoming_tones(predecessor/(num_states/2), destination) /= expected) &
                  error stop 'incoming tone differs from encoder polynomial parity'
          end do
       end do
    end do
  end subroutine expect_incoming_tones

  subroutine expect_empty_and_tied_candidates()
    integer :: decoded(PAYLOAD_BITS)
    real(real32) :: energies(0:3, TOTAL_K)
    logical :: success

    call tbcc_init(9)
    energies = 1.0_real32
    call tbcc_wava_fsk_decode(energies, 0, 2, decoded, success)
    if (success .or. any(decoded /= 0)) error stop 'empty candidate list must fail cleanly'

    ! Equal branch metrics select the all-one circular path, which fails CRC.
    call tbcc_wava_fsk_decode(energies, 4, 2, decoded, success)
    if (success .or. any(decoded /= 0)) error stop 'tied candidate CRC rejection changed'
  end subroutine expect_empty_and_tied_candidates

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
