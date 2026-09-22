module jtty_tbcc_decoder
  use, intrinsic :: iso_fortran_env, only: int32, int64, real32, real64
  use jtty_tbcc_code_profiles, only: JTTY_TBCC_PROFILE_1167_1545_80F
  use jtty_tbcc_list_decoder, only: jtty_tbcc_decoder_plan, &
       jtty_tbcc_decoder_workspace, jtty_tbcc_init_decoder_plan, &
       jtty_tbcc_init_decoder_workspace, jtty_tbcc_list_wava_optimized, &
       JTTY_TBCC_INFORMATION_BITS, JTTY_TBCC_MAX_HYPOTHESES
  use tbcc, only: PAYLOAD_BITS
  implicit none
  private

  integer(int32), parameter :: COHERENT_LENGTHS(3) = [1_int32, 2_int32, 4_int32]
  integer(int32), parameter :: CIRCULAR_PASSES = 2_int32

  type, public :: jtty_tbcc_decode_result
    integer(int32) :: accepted_hypothesis_rank = 0_int32
    integer(int32) :: exported_candidate_count = 0_int32
    integer(int32) :: circular_candidate_count = 0_int32
    integer(int32) :: coherent_block_length = 0_int32
    integer(int32) :: evaluated_rung_count = 0_int32
    integer(int64) :: accepted_identity = 0_int64
    real(real64) :: accepted_metric = -huge(0.0_real64)
    logical :: used_half_symbol_observation = .false.
  end type jtty_tbcc_decode_result

  type :: jtty_tbcc_rung_result
    integer(int32) :: candidates(JTTY_TBCC_INFORMATION_BITS, &
         JTTY_TBCC_MAX_HYPOTHESES) = 0_int32
    integer(int64) :: identities(JTTY_TBCC_MAX_HYPOTHESES) = 0_int64
    real(real64) :: clean_metrics(JTTY_TBCC_MAX_HYPOTHESES) = &
         -huge(0.0_real64)
    logical :: crc_valid(JTTY_TBCC_MAX_HYPOTHESES) = .false.
    type(jtty_tbcc_decode_result) :: result
    logical :: accepted = .false.
  end type jtty_tbcc_rung_result

  logical, save :: decoders_initialized = .false.
  type(jtty_tbcc_decoder_plan), save :: plans(3)
  type(jtty_tbcc_decoder_workspace), save :: workspaces(3)

  public :: jtty_tbcc_decode

contains

  subroutine jtty_tbcc_decode(correlations, half_correlations, payload, success, result)
    complex(real32), intent(in) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    complex(real32), intent(in) :: half_correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32), intent(out) :: payload(PAYLOAD_BITS)
    logical, intent(out) :: success
    type(jtty_tbcc_decode_result), intent(out), optional :: result
    type(jtty_tbcc_decode_result) :: local_result

    !$omp critical(jtty_tbcc_decoder)
    call initialize_decoders()
    call decode_ladder(correlations, half_correlations, payload, success, local_result)
    !$omp end critical(jtty_tbcc_decoder)
    if (present(result)) result = local_result
  end subroutine jtty_tbcc_decode

  subroutine initialize_decoders()
    integer :: coherent_index

    if (decoders_initialized) return

    do coherent_index = 1, size(COHERENT_LENGTHS)
      call jtty_tbcc_init_decoder_plan(plans(coherent_index), &
           JTTY_TBCC_PROFILE_1167_1545_80F, &
           COHERENT_LENGTHS(coherent_index), 4_int32, CIRCULAR_PASSES, &
           JTTY_TBCC_MAX_HYPOTHESES)
      call jtty_tbcc_init_decoder_workspace(workspaces(coherent_index), &
           plans(coherent_index))
    end do
    decoders_initialized = .true.
  end subroutine initialize_decoders

  subroutine decode_ladder(correlations, half_correlations, payload, success, result)
    complex(real32), intent(in) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    complex(real32), intent(in) :: half_correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32), intent(out) :: payload(PAYLOAD_BITS)
    logical, intent(out) :: success
    type(jtty_tbcc_decode_result), intent(out) :: result
    type(jtty_tbcc_rung_result) :: decoded_rung
    integer :: rung

    payload = 0_int32
    success = .false.
    result = jtty_tbcc_decode_result()
    do rung = 1, size(COHERENT_LENGTHS)
      call decode_rung(correlations, rung, decoded_rung)
      if (.not.decoded_rung%accepted) cycle
      call accept_rung(decoded_rung, rung, .false., payload, success, result)
      return
    end do

    ! Half-symbol energies discard phase, so only L1 is meaningful here.
    call decode_rung(half_correlations, 1, decoded_rung)
    if (decoded_rung%accepted) then
      call accept_rung(decoded_rung, 4, .true., payload, success, result)
    else
      result%evaluated_rung_count = 4_int32
    end if
  end subroutine decode_ladder

  subroutine decode_rung(correlations, coherent_index, rung)
    complex(real32), intent(in) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer, intent(in) :: coherent_index
    type(jtty_tbcc_rung_result), intent(out) :: rung
    integer(int32) :: start_states(JTTY_TBCC_MAX_HYPOTHESES)
    real(real64) :: path_metrics(JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: candidate_count, pool_count, rank

    rung = jtty_tbcc_rung_result()
    call jtty_tbcc_list_wava_optimized(plans(coherent_index), &
         workspaces(coherent_index), correlations, rung%candidates, rung%identities, &
         rung%clean_metrics, path_metrics, start_states, rung%crc_valid, &
         candidate_count, pool_count, prune_reserved_zero=.true.)

    rung%result = jtty_tbcc_decode_result( &
         exported_candidate_count=candidate_count, &
         circular_candidate_count=pool_count, &
         coherent_block_length=COHERENT_LENGTHS(coherent_index))
    do rank = 1, candidate_count
      if (.not.rung%crc_valid(rank)) cycle
      ! Do not expand the fixed false-accept budget past the all-zero sentinel.
      if (all(rung%candidates(1:PAYLOAD_BITS, rank) == 0_int32)) exit
      rung%accepted = .true.
      rung%result%accepted_hypothesis_rank = rank
      rung%result%accepted_identity = rung%identities(rank)
      rung%result%accepted_metric = rung%clean_metrics(rank)
      exit
    end do
  end subroutine decode_rung

  subroutine accept_rung(rung, evaluated_rung_count, half_symbol, payload, success, result)
    type(jtty_tbcc_rung_result), intent(in) :: rung
    integer, intent(in) :: evaluated_rung_count
    logical, intent(in) :: half_symbol
    integer(int32), intent(out) :: payload(PAYLOAD_BITS)
    logical, intent(out) :: success
    type(jtty_tbcc_decode_result), intent(out) :: result
    integer :: rank

    rank = rung%result%accepted_hypothesis_rank
    payload = rung%candidates(1:PAYLOAD_BITS, rank)
    success = .true.
    result = rung%result
    result%evaluated_rung_count = evaluated_rung_count
    result%used_half_symbol_observation = half_symbol
  end subroutine accept_rung

end module jtty_tbcc_decoder
