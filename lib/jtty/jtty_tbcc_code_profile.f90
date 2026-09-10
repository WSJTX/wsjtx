module jtty_tbcc_code_profiles
  use, intrinsic :: iso_fortran_env, only: int32
  implicit none
  private

  integer(int32), parameter, public :: JTTY_TBCC_PAYLOAD_BITS = 34_int32
  integer(int32), parameter, public :: JTTY_TBCC_OUTER_CHECK_BITS = 12_int32
  integer(int32), parameter, public :: JTTY_TBCC_INFORMATION_BITS = &
       JTTY_TBCC_PAYLOAD_BITS + JTTY_TBCC_OUTER_CHECK_BITS

  ! A circular rate-1/2 trellis with memory nu has 2**nu states and a
  ! constraint-length register of nu+1 bits.
  type, public :: jtty_tbcc_code_profile
    integer(int32) :: profile_id = 0_int32
    integer(int32) :: memory_nu = 0_int32
    integer(int32) :: constraint_length = 0_int32
    integer(int32) :: state_count = 0_int32
    integer(int32) :: state_mask = 0_int32
    integer(int32) :: register_mask = 0_int32
    ! Generator masks use integer bit patterns; octal notation is the
    ! conventional spelling for convolutional codes such as 1167/1545.
    integer(int32) :: generator_0 = 0_int32
    integer(int32) :: generator_1 = 0_int32
    ! The leading term of the outer polynomial is implicit; these masks describe
    ! the stored feedback coefficients and remainder register.
    integer(int32) :: outer_polynomial = 0_int32
    integer(int32) :: outer_top_bit_mask = 0_int32
    integer(int32) :: outer_register_mask = 0_int32
    integer(int32) :: payload_bits = 0_int32
    integer(int32) :: outer_check_bits = 0_int32
    integer(int32) :: information_bits = 0_int32
  end type jtty_tbcc_code_profile

  type(jtty_tbcc_code_profile), parameter, public :: &
       JTTY_TBCC_PROFILE_1167_1545_80F = jtty_tbcc_code_profile( &
       profile_id=1_int32, memory_nu=9_int32, constraint_length=10_int32, &
       state_count=512_int32, state_mask=int(z'1FF', int32), &
       register_mask=int(z'3FF', int32), &
       generator_0=int(o'1167', int32), generator_1=int(o'1545', int32), &
       outer_polynomial=int(z'80F', int32), &
       outer_top_bit_mask=int(z'800', int32), &
       outer_register_mask=int(z'FFF', int32), &
       payload_bits=JTTY_TBCC_PAYLOAD_BITS, &
       outer_check_bits=JTTY_TBCC_OUTER_CHECK_BITS, &
       information_bits=JTTY_TBCC_INFORMATION_BITS)

  type(jtty_tbcc_code_profile), parameter, public :: &
       JTTY_TBCC_PROFILE_1123_1475_22B = jtty_tbcc_code_profile( &
       profile_id=2_int32, memory_nu=9_int32, constraint_length=10_int32, &
       state_count=512_int32, state_mask=int(z'1FF', int32), &
       register_mask=int(z'3FF', int32), &
       generator_0=int(o'1123', int32), generator_1=int(o'1475', int32), &
       outer_polynomial=int(z'22B', int32), &
       outer_top_bit_mask=int(z'800', int32), &
       outer_register_mask=int(z'FFF', int32), &
       payload_bits=JTTY_TBCC_PAYLOAD_BITS, &
       outer_check_bits=JTTY_TBCC_OUTER_CHECK_BITS, &
       information_bits=JTTY_TBCC_INFORMATION_BITS)

  type(jtty_tbcc_code_profile), parameter, public :: &
       JTTY_TBCC_PROFILE_5363_6455_269 = jtty_tbcc_code_profile( &
       profile_id=3_int32, memory_nu=11_int32, constraint_length=12_int32, &
       state_count=2048_int32, state_mask=int(z'7FF', int32), &
       register_mask=int(z'FFF', int32), &
       generator_0=int(o'5363', int32), generator_1=int(o'6455', int32), &
       outer_polynomial=int(z'269', int32), &
       outer_top_bit_mask=int(z'800', int32), &
       outer_register_mask=int(z'FFF', int32), &
       payload_bits=JTTY_TBCC_PAYLOAD_BITS, &
       outer_check_bits=JTTY_TBCC_OUTER_CHECK_BITS, &
       information_bits=JTTY_TBCC_INFORMATION_BITS)

  type(jtty_tbcc_code_profile), save :: active_profile = &
       JTTY_TBCC_PROFILE_1167_1545_80F

  public :: jtty_tbcc_set_code_profile
  public :: jtty_tbcc_get_code_profile
  public :: jtty_tbcc_reset_code_profile
  public :: jtty_tbcc_code_profile_is_valid
  public :: jtty_tbcc_code_profile_is_supported
  public :: jtty_tbcc_code_profiles_equal

contains

  subroutine jtty_tbcc_set_code_profile(profile, accepted)
    type(jtty_tbcc_code_profile), intent(in) :: profile
    logical, intent(out) :: accepted

    accepted = jtty_tbcc_code_profile_is_supported(profile)
    if (.not.accepted) return
    !$omp critical(jtty_tbcc_code_profile_selection)
    active_profile = profile
    !$omp end critical(jtty_tbcc_code_profile_selection)
  end subroutine jtty_tbcc_set_code_profile

  subroutine jtty_tbcc_get_code_profile(profile)
    type(jtty_tbcc_code_profile), intent(out) :: profile

    !$omp critical(jtty_tbcc_code_profile_selection)
    profile = active_profile
    !$omp end critical(jtty_tbcc_code_profile_selection)
  end subroutine jtty_tbcc_get_code_profile

  subroutine jtty_tbcc_reset_code_profile()
    !$omp critical(jtty_tbcc_code_profile_selection)
    active_profile = JTTY_TBCC_PROFILE_1167_1545_80F
    !$omp end critical(jtty_tbcc_code_profile_selection)
  end subroutine jtty_tbcc_reset_code_profile

  pure logical function jtty_tbcc_code_profile_is_valid(profile) result(valid)
    type(jtty_tbcc_code_profile), intent(in) :: profile
    integer(int32) :: expected_state_count, expected_register_mask

    valid = .false.
    if (profile%memory_nu < 1_int32 .or. profile%memory_nu > 20_int32) return
    if (profile%constraint_length /= profile%memory_nu + 1_int32) return
    expected_state_count = shiftl(1_int32, profile%memory_nu)
    expected_register_mask = shiftl(1_int32, profile%constraint_length) - 1_int32
    if (profile%state_count /= expected_state_count) return
    if (profile%state_mask /= expected_state_count - 1_int32) return
    if (profile%register_mask /= expected_register_mask) return
    if (profile%generator_0 <= 0_int32 .or. &
         iand(profile%generator_0, not(profile%register_mask)) /= 0_int32) return
    if (profile%generator_1 <= 0_int32 .or. &
         iand(profile%generator_1, not(profile%register_mask)) /= 0_int32) return
    if (profile%payload_bits /= JTTY_TBCC_PAYLOAD_BITS) return
    if (profile%outer_check_bits /= JTTY_TBCC_OUTER_CHECK_BITS) return
    if (profile%information_bits /= profile%payload_bits + profile%outer_check_bits) return
    if (profile%outer_top_bit_mask /= &
         shiftl(1_int32, profile%outer_check_bits - 1_int32)) return
    if (profile%outer_register_mask /= &
         shiftl(1_int32, profile%outer_check_bits) - 1_int32) return
    if (profile%outer_polynomial <= 0_int32 .or. &
         iand(profile%outer_polynomial, not(profile%outer_register_mask)) /= 0_int32) return
    valid = .true.
  end function jtty_tbcc_code_profile_is_valid

  pure logical function jtty_tbcc_code_profile_is_supported(profile) result(supported)
    type(jtty_tbcc_code_profile), intent(in) :: profile

    supported = jtty_tbcc_code_profile_is_valid(profile) .and. ( &
         jtty_tbcc_code_profiles_equal(profile, JTTY_TBCC_PROFILE_1167_1545_80F) .or. &
         jtty_tbcc_code_profiles_equal(profile, JTTY_TBCC_PROFILE_1123_1475_22B) .or. &
         jtty_tbcc_code_profiles_equal(profile, JTTY_TBCC_PROFILE_5363_6455_269))
  end function jtty_tbcc_code_profile_is_supported

  pure logical function jtty_tbcc_code_profiles_equal(left, right) result(equal)
    type(jtty_tbcc_code_profile), intent(in) :: left, right

    equal = left%profile_id == right%profile_id .and. &
         left%memory_nu == right%memory_nu .and. &
         left%constraint_length == right%constraint_length .and. &
         left%state_count == right%state_count .and. &
         left%state_mask == right%state_mask .and. &
         left%register_mask == right%register_mask .and. &
         left%generator_0 == right%generator_0 .and. &
         left%generator_1 == right%generator_1 .and. &
         left%outer_polynomial == right%outer_polynomial .and. &
         left%outer_top_bit_mask == right%outer_top_bit_mask .and. &
         left%outer_register_mask == right%outer_register_mask .and. &
         left%payload_bits == right%payload_bits .and. &
         left%outer_check_bits == right%outer_check_bits .and. &
         left%information_bits == right%information_bits
  end function jtty_tbcc_code_profiles_equal

end module jtty_tbcc_code_profiles
