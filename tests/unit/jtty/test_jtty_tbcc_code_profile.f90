program test_jtty_tbcc_code_profile
  use, intrinsic :: iso_fortran_env, only: int32, real32
  use jtty_tbcc_code_profiles
  use jtty_tbcc_decoder, only: jtty_tbcc_decode
  use jtty_tbcc_list_decoder, only: jtty_tbcc_crc_valid
  use jtty_mdec, only: jtty_tbcc_reencode_for_subtraction
  use tbcc, only: PAYLOAD_BITS, TOTAL_K, encode_crc12, tbcc_encode, &
       tbcc_wava_fsk_decode
  implicit none

  integer(int32), parameter :: golden_payload(PAYLOAD_BITS) = [ &
       1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0, &
       1,0,0,1,0,0,1,0,0,1 ]
  integer(int32), parameter :: golden_information_bits(TOTAL_K) = [ &
       1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0, &
       1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,1,0 ]
  integer(int32), parameter :: golden_encoded_bits(2, TOTAL_K) = reshape([ &
       1,0, 0,0, 0,1, 0,0, 1,0, 1,1, 0,1, 1,1, 0,1, 1,1, &
       0,0, 0,1, 1,1, 0,0, 0,1, 1,1, 0,0, 0,1, 1,1, 0,0, &
       0,1, 1,1, 0,0, 0,1, 1,1, 0,0, 0,1, 1,1, 0,0, 0,1, &
       1,1, 0,0, 0,1, 1,1, 0,0, 0,1, 1,1, 0,0, 0,1, 1,1, &
       0,0, 0,1, 1,1, 0,0, 1,0, 1,0 ], [2, TOTAL_K])
  integer(int32), parameter :: golden_tones(TOTAL_K) = [ &
       3,0,1,0,3,2,1,2,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1, &
       2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,3,3 ]

  call expect_descriptor()
  call expect_golden_code()

  print *, 'test_jtty_tbcc_code_profile: all checks passed'

contains

  subroutine expect_descriptor()
    type(jtty_tbcc_code_profile) :: unsupported

    call require(jtty_tbcc_code_profile_is_valid(JTTY_TBCC_PROFILE_1167_1545_80F), &
         'established code descriptor is invalid')
    call require(jtty_tbcc_code_profile_is_supported(JTTY_TBCC_PROFILE_1167_1545_80F), &
         'established code descriptor is unsupported')
    call require(JTTY_TBCC_PROFILE_1167_1545_80F%memory_nu == 9_int32 .and. &
         JTTY_TBCC_PROFILE_1167_1545_80F%state_count == 512_int32, &
         'established code trellis dimensions changed')
    call require(JTTY_TBCC_PROFILE_1167_1545_80F%generator_0 == int(o'1167', int32) .and. &
         JTTY_TBCC_PROFILE_1167_1545_80F%generator_1 == int(o'1545', int32), &
         'established generator polynomials changed')
    call require(JTTY_TBCC_PROFILE_1167_1545_80F%outer_polynomial == int(z'80F', int32), &
         'established outer polynomial changed')

    unsupported = JTTY_TBCC_PROFILE_1167_1545_80F
    unsupported%generator_1 = int(o'1547', int32)
    call require(jtty_tbcc_code_profile_is_valid(unsupported), &
         'unsupported-code fixture is not structurally valid')
    call require(.not.jtty_tbcc_code_profile_is_supported(unsupported), &
         'noncanonical code was reported as supported')
  end subroutine expect_descriptor

  subroutine expect_golden_code()
    integer(int32) :: encoded_information(2, TOTAL_K), encoded_bits(2, TOTAL_K)
    integer(int32) :: invalid_information(TOTAL_K)
    integer(int32) :: tones(TOTAL_K), subtraction_tones(TOTAL_K)
    integer(int32) :: decoded(PAYLOAD_BITS)
    real(real32) :: tone_energies(0:3, TOTAL_K)
    complex(real32) :: correlations(0:3, TOTAL_K)
    logical :: success

    call encode_crc12(golden_payload, encoded_information, JTTY_TBCC_PROFILE_1167_1545_80F)
    call require(all(encoded_information(1, :) == golden_information_bits), &
         'outer-check bits changed from the golden vector')
    call require(jtty_tbcc_crc_valid(JTTY_TBCC_PROFILE_1167_1545_80F, &
         encoded_information(1, :)), 'golden outer-check word was rejected')
    invalid_information = encoded_information(1, :)
    invalid_information(TOTAL_K) = 1_int32 - invalid_information(TOTAL_K)
    call require(.not.jtty_tbcc_crc_valid(JTTY_TBCC_PROFILE_1167_1545_80F, &
         invalid_information), 'outer check accepted a corrupted word')

    call tbcc_encode(golden_payload, tones, JTTY_TBCC_PROFILE_1167_1545_80F, encoded_bits)
    call require(all(encoded_bits == golden_encoded_bits), &
         'encoded bits changed from the golden vector')
    call require(all(tones == golden_tones), 'tones changed from the golden vector')

    call make_noiseless_observations(tones, tone_energies, correlations)
    call tbcc_wava_fsk_decode(tone_energies, 4_int32, 2_int32, decoded, success, &
         reserved_zero_bit=33_int32, code_profile=JTTY_TBCC_PROFILE_1167_1545_80F)
    call require(success .and. all(decoded == golden_payload), 'WAVA decode failed')
    call jtty_tbcc_decode(correlations, correlations, decoded, success)
    call require(success .and. all(decoded == golden_payload), 'coherent decode failed')

    call jtty_tbcc_reencode_for_subtraction(golden_payload, subtraction_tones)
    call require(all(subtraction_tones == golden_tones), &
         'subtraction re-encoding did not reproduce the established tones')
  end subroutine expect_golden_code

  subroutine make_noiseless_observations(tones, tone_energies, correlations)
    integer(int32), intent(in) :: tones(TOTAL_K)
    real(real32), intent(out) :: tone_energies(0:3, TOTAL_K)
    complex(real32), intent(out) :: correlations(0:3, TOTAL_K)
    integer(int32) :: symbol

    tone_energies = 0.0_real32
    correlations = cmplx(0.0_real32, 0.0_real32, real32)
    do symbol = 1, TOTAL_K
      tone_energies(tones(symbol), symbol) = 100.0_real32
      correlations(tones(symbol), symbol) = cmplx(100.0_real32, 0.0_real32, real32)
    end do
  end subroutine make_noiseless_observations

  subroutine require(condition, message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if (.not.condition) error stop message
  end subroutine require

end program test_jtty_tbcc_code_profile
