program test_jtty_tbcc_code_profile
  use, intrinsic :: iso_fortran_env, only: int32, real32
  use jtty_tbcc_code_profiles
  use jtty_tbcc_decoder, only: jtty_tbcc_decode
  use jtty_tbcc_list_decoder, only: jtty_tbcc_crc_valid, &
       jtty_tbcc_supertransition, jtty_tbcc_tailbiting_state
  use jtty_mdec, only: jtty_tbcc_reencode_for_subtraction
  use tbcc, only: PAYLOAD_BITS, TOTAL_K, encode_crc12, tbcc_encode, &
       tbcc_wava_fsk_decode
  implicit none

  integer(int32), parameter :: golden_payload(PAYLOAD_BITS) = [ &
       1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0, &
       1,0,0,1,0,0,1,0,0,1 ]
  integer(int32), parameter :: golden_information_bits_80F(TOTAL_K) = [ &
       1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0, &
       1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,1,0 ]
  integer(int32), parameter :: golden_encoded_bits_1167_1545(2, TOTAL_K) = reshape([ &
       1,0, 0,0, 0,1, 0,0, 1,0, 1,1, 0,1, 1,1, 0,1, 1,1, &
       0,0, 0,1, 1,1, 0,0, 0,1, 1,1, 0,0, 0,1, 1,1, 0,0, &
       0,1, 1,1, 0,0, 0,1, 1,1, 0,0, 0,1, 1,1, 0,0, 0,1, &
       1,1, 0,0, 0,1, 1,1, 0,0, 0,1, 1,1, 0,0, 0,1, 1,1, &
       0,0, 0,1, 1,1, 0,0, 1,0, 1,0 ], [2, TOTAL_K])
  integer(int32), parameter :: golden_tones_1167_1545(TOTAL_K) = [ &
       3,0,1,0,3,2,1,2,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1, &
       2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,1,2,0,3,3 ]
  integer(int32), parameter :: golden_information_bits_22B(TOTAL_K) = [ &
       1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0, &
       1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,1,1,1,1,1,1 ]
  integer(int32), parameter :: golden_encoded_bits_1123_1475(2, TOTAL_K) = reshape([ &
       0,1, 1,1, 1,0, 0,0, 0,0, 0,0, 1,0, 1,1, 1,0, 1,1, &
       0,1, 0,1, 1,1, 0,1, 0,1, 1,1, 0,1, 0,1, 1,1, 0,1, &
       0,1, 1,1, 0,1, 0,1, 1,1, 0,1, 0,1, 1,1, 0,1, 0,1, &
       1,1, 0,1, 0,1, 1,1, 0,1, 0,1, 1,1, 0,1, 0,1, 1,1, &
       1,0, 0,0, 0,0, 1,0, 1,0, 1,0 ], [2, TOTAL_K])
  integer(int32), parameter :: golden_tones_1123_1475(TOTAL_K) = [ &
       1,2,3,0,0,0,3,2,3,2,1,1,2,1,1,2,1,1,2,1,1,2,1,1, &
       2,1,1,2,1,1,2,1,1,2,1,1,2,1,1,2,3,0,0,3,3,3 ]
  integer(int32), parameter :: golden_information_bits_269(TOTAL_K) = [ &
       1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0,1,0,0, &
       1,0,0,1,0,0,1,0,0,1,0,0,1,1,1,1,1,1,0,0,0,0 ]
  integer(int32), parameter :: golden_encoded_bits_5363_6455(2, TOTAL_K) = reshape([ &
       1,0, 1,1, 1,0, 1,1, 0,0, 0,0, 1,1, 1,0, 1,1, 1,0, &
       1,1, 0,0, 1,0, 1,1, 0,0, 1,0, 1,1, 0,0, 1,0, 1,1, &
       0,0, 1,0, 1,1, 0,0, 1,0, 1,1, 0,0, 1,0, 1,1, 0,0, &
       1,0, 1,1, 0,0, 1,0, 1,1, 0,0, 1,0, 0,0, 0,1, 0,1, &
       0,0, 1,0, 1,1, 0,0, 1,0, 1,0 ], [2, TOTAL_K])
  integer(int32), parameter :: golden_tones_5363_6455(TOTAL_K) = [ &
       3,2,3,2,0,0,2,3,2,3,2,0,3,2,0,3,2,0,3,2,0,3,2,0, &
       3,2,0,3,2,0,3,2,0,3,2,0,3,0,1,1,0,3,2,0,3,3 ]

  type(jtty_tbcc_code_profile) :: profile, unsupported_profile, selected_profile
  logical :: accepted

  call expect_descriptor_and_selection()
  call expect_golden_profile(JTTY_TBCC_PROFILE_1167_1545_80F, &
       golden_information_bits_80F, golden_encoded_bits_1167_1545, &
       golden_tones_1167_1545, '1167/1545 + 0x80F')
  call expect_golden_profile(JTTY_TBCC_PROFILE_1123_1475_22B, &
       golden_information_bits_22B, golden_encoded_bits_1123_1475, &
       golden_tones_1123_1475, '1123/1475 + 0x22B')
  call expect_golden_profile(JTTY_TBCC_PROFILE_5363_6455_269, &
       golden_information_bits_269, golden_encoded_bits_5363_6455, &
       golden_tones_5363_6455, '5363/6455 + 0x269')
  call require(.not.jtty_tbcc_crc_valid(JTTY_TBCC_PROFILE_1123_1475_22B, &
       golden_information_bits_80F), '0x22B accepted the 0x80F golden word')
  call require(.not.jtty_tbcc_crc_valid(JTTY_TBCC_PROFILE_1167_1545_80F, &
       golden_information_bits_22B), '0x80F accepted the 0x22B golden word')
  call require(.not.jtty_tbcc_crc_valid(JTTY_TBCC_PROFILE_5363_6455_269, &
       golden_information_bits_80F), '0x269 accepted the 0x80F golden word')
  call require(.not.jtty_tbcc_crc_valid(JTTY_TBCC_PROFILE_5363_6455_269, &
       golden_information_bits_22B), '0x269 accepted the 0x22B golden word')
  call require(.not.jtty_tbcc_crc_valid(JTTY_TBCC_PROFILE_1167_1545_80F, &
       golden_information_bits_269), '0x80F accepted the 0x269 golden word')
  call require(.not.jtty_tbcc_crc_valid(JTTY_TBCC_PROFILE_1123_1475_22B, &
       golden_information_bits_269), '0x22B accepted the 0x269 golden word')
  call expect_nu11_trellis_closure()
  call expect_plan_cache_switching()
  call jtty_tbcc_reset_code_profile()
  call jtty_tbcc_get_code_profile(selected_profile)
  call require(jtty_tbcc_code_profiles_equal(selected_profile, &
       JTTY_TBCC_PROFILE_1167_1545_80F), &
       'final reset did not restore 1167/1545 + 0x80F')

  print *, 'test_jtty_tbcc_code_profile: all checks passed'

contains

  subroutine expect_descriptor_and_selection()
    call jtty_tbcc_get_code_profile(profile)
    call require(jtty_tbcc_code_profiles_equal(profile, &
         JTTY_TBCC_PROFILE_1167_1545_80F), &
         'module-initialized TBCC profile is not 1167/1545 + 0x80F')
    call jtty_tbcc_reset_code_profile()
    call jtty_tbcc_get_code_profile(selected_profile)
    call require(jtty_tbcc_code_profiles_equal(selected_profile, &
         JTTY_TBCC_PROFILE_1167_1545_80F), &
         'profile reset did not restore 1167/1545 + 0x80F')

    call expect_descriptor(JTTY_TBCC_PROFILE_1167_1545_80F, &
         1_int32, 9_int32, 10_int32, 512_int32, int(z'1FF', int32), &
         int(z'3FF', int32), int(o'1167', int32), int(o'1545', int32), &
         int(z'80F', int32), '1167/1545 + 0x80F')
    call expect_descriptor(JTTY_TBCC_PROFILE_1123_1475_22B, &
         2_int32, 9_int32, 10_int32, 512_int32, int(z'1FF', int32), &
         int(z'3FF', int32), int(o'1123', int32), int(o'1475', int32), &
         int(z'22B', int32), '1123/1475 + 0x22B')
    call expect_descriptor(JTTY_TBCC_PROFILE_5363_6455_269, &
         3_int32, 11_int32, 12_int32, 2048_int32, int(z'7FF', int32), &
         int(z'FFF', int32), int(o'5363', int32), int(o'6455', int32), &
         int(z'269', int32), '5363/6455 + 0x269')
    call require(.not.jtty_tbcc_code_profiles_equal( &
         JTTY_TBCC_PROFILE_1167_1545_80F, JTTY_TBCC_PROFILE_1123_1475_22B), &
         'distinct nu=9 profiles compare equal')
    call require(.not.jtty_tbcc_code_profiles_equal( &
         JTTY_TBCC_PROFILE_1167_1545_80F, JTTY_TBCC_PROFILE_5363_6455_269) .and. &
         .not.jtty_tbcc_code_profiles_equal( &
         JTTY_TBCC_PROFILE_1123_1475_22B, JTTY_TBCC_PROFILE_5363_6455_269), &
         'nu=11 profile compares equal to a nu=9 profile')

    call jtty_tbcc_set_code_profile(JTTY_TBCC_PROFILE_5363_6455_269, accepted)
    call require(accepted, '5363/6455 + 0x269 profile was rejected')
    call jtty_tbcc_get_code_profile(profile)
    call require(jtty_tbcc_code_profiles_equal(profile, &
         JTTY_TBCC_PROFILE_5363_6455_269), &
         '5363/6455 + 0x269 selection was not retained')

    unsupported_profile = JTTY_TBCC_PROFILE_5363_6455_269
    unsupported_profile%generator_1 = int(o'5261', int32)
    call require(jtty_tbcc_code_profile_is_valid(unsupported_profile), &
         'unsupported-profile fixture is not structurally valid')
    call require(.not.jtty_tbcc_code_profile_is_supported(unsupported_profile), &
         'noncanonical profile with a supported ID was reported as selectable')
    call jtty_tbcc_set_code_profile(unsupported_profile, accepted)
    call require(.not.accepted, 'noncanonical TBCC profile was accepted')
    call jtty_tbcc_get_code_profile(selected_profile)
    call require(jtty_tbcc_code_profiles_equal(selected_profile, profile), &
         'unsupported profile selection changed active state')

    call jtty_tbcc_reset_code_profile()
    call jtty_tbcc_get_code_profile(selected_profile)
    call require(jtty_tbcc_code_profiles_equal(selected_profile, &
         JTTY_TBCC_PROFILE_1167_1545_80F), &
         'profile reset did not preserve the existing default')
  end subroutine expect_descriptor_and_selection

  subroutine expect_descriptor(code_profile, profile_id, memory_nu, &
       constraint_length, state_count, state_mask, register_mask, &
       generator_0, generator_1, outer_polynomial, description)
    type(jtty_tbcc_code_profile), intent(in) :: code_profile
    integer(int32), intent(in) :: profile_id, memory_nu, constraint_length
    integer(int32), intent(in) :: state_count, state_mask, register_mask
    integer(int32), intent(in) :: generator_0, generator_1, outer_polynomial
    character(len=*), intent(in) :: description

    call require(jtty_tbcc_code_profile_is_valid(code_profile), &
         description//' descriptor is invalid')
    call require(jtty_tbcc_code_profile_is_supported(code_profile), &
         description//' descriptor is not selectable')
    call require(code_profile%profile_id == profile_id, description//' profile ID is incorrect')
    call require(code_profile%memory_nu == memory_nu, description//' nu is incorrect')
    call require(code_profile%constraint_length == constraint_length, &
         description//' constraint length is incorrect')
    call require(code_profile%state_count == state_count, &
         description//' state count is incorrect')
    call require(code_profile%state_mask == state_mask, &
         description//' state mask is incorrect')
    call require(code_profile%register_mask == register_mask, &
         description//' register mask is incorrect')
    call require(code_profile%generator_0 == generator_0, &
         description//' generator 0 is incorrect')
    call require(code_profile%generator_1 == generator_1, &
         description//' generator 1 is incorrect')
    call require(code_profile%outer_polynomial == outer_polynomial, &
         description//' outer polynomial is incorrect')
    call require(code_profile%outer_top_bit_mask == int(z'800', int32), &
         description//' outer top-bit mask is not 0x800')
    call require(code_profile%outer_register_mask == int(z'FFF', int32), &
         description//' outer register mask is not 0xFFF')
    call require(code_profile%payload_bits == 34_int32 .and. &
         code_profile%outer_check_bits == 12_int32 .and. &
         code_profile%information_bits == 46_int32, &
         description//' dimensions are incorrect')
  end subroutine expect_descriptor

  subroutine expect_golden_profile(code_profile, golden_information_bits, &
       golden_encoded_bits, golden_tones, description)
    type(jtty_tbcc_code_profile), intent(in) :: code_profile
    integer(int32), intent(in) :: golden_information_bits(TOTAL_K)
    integer(int32), intent(in) :: golden_encoded_bits(2, TOTAL_K)
    integer(int32), intent(in) :: golden_tones(TOTAL_K)
    character(len=*), intent(in) :: description
    integer(int32) :: encoded_information(2, TOTAL_K), encoded_bits(2, TOTAL_K)
    integer(int32) :: invalid_information(TOTAL_K)
    integer(int32) :: tones(TOTAL_K), subtraction_tones(TOTAL_K)
    integer(int32) :: decoded(PAYLOAD_BITS)
    real(real32) :: tone_energies(0:3, TOTAL_K)
    complex(real32) :: correlations(0:3, TOTAL_K)
    logical :: success

    call jtty_tbcc_set_code_profile(code_profile, accepted)
    call require(accepted, description//' selection failed')
    call jtty_tbcc_get_code_profile(selected_profile)
    call require(jtty_tbcc_code_profiles_equal(selected_profile, code_profile), &
         description//' active profile mismatch')

    call encode_crc12(golden_payload, encoded_information, selected_profile)
    call require(all(encoded_information(1, :) == golden_information_bits), &
         description//' outer-check bits changed from the golden vector')
    call require(jtty_tbcc_crc_valid(selected_profile, encoded_information(1, :)), &
         description//' golden outer-check word was rejected')
    invalid_information = encoded_information(1, :)
    invalid_information(TOTAL_K) = 1_int32 - invalid_information(TOTAL_K)
    call require(.not.jtty_tbcc_crc_valid(selected_profile, invalid_information), &
         description//' outer check accepted a corrupted word')

    call tbcc_encode(golden_payload, tones, selected_profile, encoded_bits)
    call require(all(encoded_bits == golden_encoded_bits), &
         description//' encoded bits changed from the golden vector')
    call require(all(tones == golden_tones), &
         description//' tones changed from the golden vector')

    call make_noiseless_observations(tones, tone_energies, correlations)
    call tbcc_wava_fsk_decode(tone_energies, 4_int32, 2_int32, decoded, success, &
         reserved_zero_bit=33_int32, code_profile=selected_profile)
    call require(success .and. all(decoded == golden_payload), &
         description//' WAVA decode failed')
    call expect_coherent_decode(correlations, selected_profile, description)

    call jtty_tbcc_reset_code_profile()
    call jtty_tbcc_reencode_for_subtraction(golden_payload, selected_profile, &
         subtraction_tones)
    call require(all(subtraction_tones == golden_tones), &
         description//' subtraction re-encoding did not reproduce the selected tones')
  end subroutine expect_golden_profile

  subroutine expect_nu11_trellis_closure()
    integer(int32) :: start_state, end_state, tones(TOTAL_K)

    start_state = jtty_tbcc_tailbiting_state(JTTY_TBCC_PROFILE_5363_6455_269, &
         golden_information_bits_269)
    call require(start_state == int(z'3F0', int32) .and. start_state > 511_int32, &
         'nu=11 tail-biting state did not retain bits above the nu=9 state space')
    call jtty_tbcc_supertransition(JTTY_TBCC_PROFILE_5363_6455_269, &
         start_state, golden_information_bits_269, end_state, tones)
    call require(end_state == start_state, 'nu=11 golden trellis path is not circular')
    call require(all(tones == golden_tones_5363_6455), &
         'nu=11 trellis construction changed the golden tones')
  end subroutine expect_nu11_trellis_closure

  subroutine expect_plan_cache_switching()
    call expect_active_coherent_decode(JTTY_TBCC_PROFILE_1167_1545_80F, &
         golden_tones_1167_1545)
    call expect_active_coherent_decode(JTTY_TBCC_PROFILE_1123_1475_22B, &
         golden_tones_1123_1475)
    call expect_active_coherent_decode(JTTY_TBCC_PROFILE_5363_6455_269, &
         golden_tones_5363_6455)
    call expect_active_coherent_decode(JTTY_TBCC_PROFILE_1167_1545_80F, &
         golden_tones_1167_1545)
  end subroutine expect_plan_cache_switching

  subroutine expect_active_coherent_decode(code_profile, tones)
    type(jtty_tbcc_code_profile), intent(in) :: code_profile
    integer(int32), intent(in) :: tones(TOTAL_K)
    integer(int32) :: decoded(PAYLOAD_BITS)
    real(real32) :: tone_energies(0:3, TOTAL_K)
    complex(real32) :: correlations(0:3, TOTAL_K)
    logical :: success

    call jtty_tbcc_set_code_profile(code_profile, accepted)
    call require(accepted, 'profile switch failed')
    call make_noiseless_observations(tones, tone_energies, correlations)
    call jtty_tbcc_decode(correlations, correlations, decoded, success)
    call require(success .and. all(decoded == golden_payload), &
         'profile switch reused a stale coherent decoder plan or workspace')
  end subroutine expect_active_coherent_decode

  subroutine expect_coherent_decode(correlations, code_profile, description)
    complex(real32), intent(in) :: correlations(0:3, TOTAL_K)
    type(jtty_tbcc_code_profile), intent(in) :: code_profile
    character(len=*), intent(in) :: description
    integer(int32) :: decoded(PAYLOAD_BITS)
    logical :: success

    call jtty_tbcc_decode(correlations, correlations, decoded, success, code_profile=code_profile)
    call require(success .and. all(decoded == golden_payload), &
         description//' coherent decode failed')
  end subroutine expect_coherent_decode

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
