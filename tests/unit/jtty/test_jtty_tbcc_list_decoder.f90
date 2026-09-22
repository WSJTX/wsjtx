program test_jtty_tbcc_list_decoder
  use, intrinsic :: iso_fortran_env, only: int32, int64, real32, real64
  use jtty_tbcc_code_profiles, only: jtty_tbcc_code_profile, &
       JTTY_TBCC_PROFILE_1167_1545_80F
  use tbcc, only: tbcc_encode, encode_crc12
  use jtty_tbcc_list_decoder
  implicit none

  call expect_optimized_reference_parity()
  call expect_noiseless_tailbiting_decode()
  call expect_reserved_bit_pruning()
  call expect_deterministic_hypothesis_budget()
  call expect_coherent_normalization()

  print *, 'test_jtty_tbcc_list_decoder: all checks passed'

contains

  subroutine expect_optimized_reference_parity()
    integer(int32), parameter :: coherent_lengths(3) = [1_int32, 2_int32, 4_int32]
    integer(int32), parameter :: survivor_widths(2) = [1_int32, 4_int32]
    type(jtty_tbcc_decoder_plan) :: plan
    type(jtty_tbcc_decoder_workspace) :: workspace
    complex(real32) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: coherent_index, width_index, symbol, tone, wraps

    do symbol = 1, JTTY_TBCC_INFORMATION_BITS
      do tone = 0, 3
        correlations(tone, symbol) = cmplx( &
             real(modulo(17*tone + 11*symbol, 29) - 14, real32)/8.0_real32, &
             real(modulo(7*tone + 19*symbol, 31) - 15, real32)/9.0_real32, &
             real32)
      end do
    end do

    do wraps = 1, 2
      do coherent_index = 1, size(coherent_lengths)
        do width_index = 1, size(survivor_widths)
          call jtty_tbcc_init_decoder_plan(plan, JTTY_TBCC_PROFILE_1167_1545_80F, &
               coherent_lengths(coherent_index), survivor_widths(width_index), wraps, &
               JTTY_TBCC_MAX_HYPOTHESES)
          call jtty_tbcc_init_decoder_workspace(workspace, plan)
          call expect_decode_parity(JTTY_TBCC_PROFILE_1167_1545_80F, plan, workspace, correlations, &
               coherent_lengths(coherent_index), survivor_widths(width_index), wraps, .false.)
          call expect_decode_parity(JTTY_TBCC_PROFILE_1167_1545_80F, plan, workspace, correlations, &
               coherent_lengths(coherent_index), survivor_widths(width_index), wraps, .true.)
        end do
      end do
    end do

    ! Equal branch energies exercise deterministic ties and wrap deduplication.
    correlations = cmplx(0.0_real32, 0.0_real32, real32)
    do width_index = 1, 4
      call jtty_tbcc_init_decoder_plan(plan, JTTY_TBCC_PROFILE_1167_1545_80F, 4_int32, &
           width_index, 2_int32, JTTY_TBCC_MAX_HYPOTHESES)
      call jtty_tbcc_init_decoder_workspace(workspace, plan)
      call expect_decode_parity(JTTY_TBCC_PROFILE_1167_1545_80F, plan, workspace, correlations, &
           4_int32, width_index, 2_int32, .false.)
    end do

    ! A common large branch collapses distinct accumulated metrics into ties.
    do symbol = 1, JTTY_TBCC_INFORMATION_BITS
      do tone = 0, 3
        correlations(tone, symbol) = scale(1.0_real32, -18)*cmplx( &
             real(modulo(17*tone + 11*symbol, 29) - 14, real32)/8.0_real32, &
             real(modulo(7*tone + 19*symbol, 31) - 15, real32)/9.0_real32, real32)
      end do
    end do
    correlations(:, 13) = cmplx(scale(1.0_real32, 20), 0.0_real32, real32)
    do coherent_index = 1, size(coherent_lengths)
      call jtty_tbcc_init_decoder_plan(plan, JTTY_TBCC_PROFILE_1167_1545_80F, &
           coherent_lengths(coherent_index), 4_int32, 2_int32, JTTY_TBCC_MAX_HYPOTHESES)
      call jtty_tbcc_init_decoder_workspace(workspace, plan)
      call expect_decode_parity(JTTY_TBCC_PROFILE_1167_1545_80F, plan, workspace, correlations, &
           coherent_lengths(coherent_index), 4_int32, 2_int32, .false.)
    end do
  end subroutine expect_optimized_reference_parity

  subroutine expect_decode_parity(code, plan, workspace, correlations, &
       coherent_length, survivor_width, wraps, prune)
    type(jtty_tbcc_code_profile), intent(in) :: code
    type(jtty_tbcc_decoder_plan), intent(in) :: plan
    type(jtty_tbcc_decoder_workspace), intent(inout) :: workspace
    complex(real32), intent(in) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32), intent(in) :: coherent_length, survivor_width, wraps
    logical, intent(in) :: prune
    integer(int32) :: reference_bits(JTTY_TBCC_INFORMATION_BITS, JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: optimized_bits(JTTY_TBCC_INFORMATION_BITS, JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: reference_states(JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: optimized_states(JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: reference_count, optimized_count, reference_pool, optimized_pool
    integer(int64) :: reference_ids(JTTY_TBCC_MAX_HYPOTHESES)
    integer(int64) :: optimized_ids(JTTY_TBCC_MAX_HYPOTHESES)
    real(real64) :: reference_clean(JTTY_TBCC_MAX_HYPOTHESES)
    real(real64) :: optimized_clean(JTTY_TBCC_MAX_HYPOTHESES)
    real(real64) :: reference_wava(JTTY_TBCC_MAX_HYPOTHESES)
    real(real64) :: optimized_wava(JTTY_TBCC_MAX_HYPOTHESES)
    logical :: reference_crc(JTTY_TBCC_MAX_HYPOTHESES)
    logical :: optimized_crc(JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: tracked_bits(JTTY_TBCC_INFORMATION_BITS) = 0_int32
    integer(int32) :: reference_pool_rank, optimized_pool_rank, reference_h_rank, optimized_h_rank
    logical :: reference_present, optimized_present

    call jtty_tbcc_list_wava_reference(correlations, code, coherent_length, &
         survivor_width, wraps, JTTY_TBCC_MAX_HYPOTHESES, reference_bits, &
         reference_ids, reference_clean, reference_wava, reference_states, &
         reference_crc, reference_count, reference_pool, prune_reserved_zero=prune, &
         reference_bits=tracked_bits, reference_present=reference_present, &
         reference_pool_rank=reference_pool_rank, reference_h_rank=reference_h_rank)
    call jtty_tbcc_list_wava_optimized(plan, workspace, correlations, optimized_bits, &
         optimized_ids, optimized_clean, optimized_wava, optimized_states, &
         optimized_crc, optimized_count, optimized_pool, prune_reserved_zero=prune, &
         reference_bits=tracked_bits, reference_present=optimized_present, &
         reference_pool_rank=optimized_pool_rank, reference_h_rank=optimized_h_rank)

    if (reference_count /= optimized_count .or. reference_pool /= optimized_pool .or. &
         any(reference_bits /= optimized_bits) .or. any(reference_ids /= optimized_ids) .or. &
         .not.real_arrays_agree(reference_clean, optimized_clean) .or. &
         .not.real_arrays_agree(reference_wava, optimized_wava) .or. &
         any(reference_states /= optimized_states) .or. &
         any(reference_crc .neqv. optimized_crc) .or. &
         (reference_present .neqv. optimized_present) .or. &
         reference_pool_rank /= optimized_pool_rank .or. reference_h_rank /= optimized_h_rank) &
         error stop 'TBCC list decoder optimized/reference mismatch'
  end subroutine expect_decode_parity

  subroutine expect_noiseless_tailbiting_decode()
    type(jtty_tbcc_code_profile) :: code
    complex(real32) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: information_bits(JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: tones(JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: candidates(JTTY_TBCC_INFORMATION_BITS, JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: states(JTTY_TBCC_MAX_HYPOTHESES), candidate_count, pool_count
    integer(int64) :: identities(JTTY_TBCC_MAX_HYPOTHESES)
    real(real64) :: clean_metrics(JTTY_TBCC_MAX_HYPOTHESES)
    real(real64) :: path_metrics(JTTY_TBCC_MAX_HYPOTHESES), rescored_metric
    logical :: crc_valid(JTTY_TBCC_MAX_HYPOTHESES), closed

    code = JTTY_TBCC_PROFILE_1167_1545_80F
    call make_codeword(.false., information_bits, tones)
    call make_noiseless_correlations(tones, correlations)
    call jtty_tbcc_list_wava(correlations, code, 4_int32, 4_int32, 2_int32, &
         JTTY_TBCC_MAX_HYPOTHESES, candidates, identities, clean_metrics, path_metrics, &
         states, crc_valid, candidate_count, pool_count, prune_reserved_zero=.true.)

    if (candidate_count < 1_int32 .or. pool_count < candidate_count) &
         error stop 'TBCC list decoder returned no noiseless candidate'
    if (any(candidates(:, 1) /= information_bits) .or. .not.crc_valid(1)) &
         error stop 'TBCC list decoder noiseless candidate mismatch'
    if (states(1) /= jtty_tbcc_tailbiting_state(code, information_bits)) &
         error stop 'TBCC list decoder tail-biting state mismatch'
    call jtty_tbcc_score_candidate(code, correlations, 4_int32, candidates(:, 1), &
         rescored_metric, closed)
    if (.not.closed .or. rescored_metric /= clean_metrics(1) .or. &
         abs(clean_metrics(1) - 46.0_real64) > 1.0e-12_real64) &
         error stop 'TBCC list decoder final L2 block or clean rescore mismatch'
  end subroutine expect_noiseless_tailbiting_decode

  subroutine expect_reserved_bit_pruning()
    type(jtty_tbcc_code_profile) :: code
    complex(real32) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: information_bits(JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: tones(JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: candidates(JTTY_TBCC_INFORMATION_BITS, JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: states(JTTY_TBCC_MAX_HYPOTHESES), candidate_count, pool_count
    integer(int64) :: identities(JTTY_TBCC_MAX_HYPOTHESES), forbidden_identity
    real(real64) :: clean_metrics(JTTY_TBCC_MAX_HYPOTHESES)
    real(real64) :: path_metrics(JTTY_TBCC_MAX_HYPOTHESES)
    logical :: crc_valid(JTTY_TBCC_MAX_HYPOTHESES)

    code = JTTY_TBCC_PROFILE_1167_1545_80F
    call make_codeword(.true., information_bits, tones)
    call make_noiseless_correlations(tones, correlations)
    forbidden_identity = jtty_tbcc_candidate_identity(information_bits)
    call jtty_tbcc_list_wava(correlations, code, 4_int32, 4_int32, 2_int32, &
         JTTY_TBCC_MAX_HYPOTHESES, candidates, identities, clean_metrics, path_metrics, &
         states, crc_valid, candidate_count, pool_count, prune_reserved_zero=.false.)
    if (candidate_count < 1_int32 .or. identities(1) /= forbidden_identity) &
         error stop 'TBCC list decoder unpruned control mismatch'

    call jtty_tbcc_list_wava(correlations, code, 4_int32, 4_int32, 2_int32, &
         JTTY_TBCC_MAX_HYPOTHESES, candidates, identities, clean_metrics, path_metrics, &
         states, crc_valid, candidate_count, pool_count, prune_reserved_zero=.true.)
    if (candidate_count > 0_int32) then
      if (any(candidates(JTTY_TBCC_RESERVED_BIT, 1:candidate_count) /= 0_int32)) &
           error stop 'TBCC list decoder reserved-bit pruning failed'
      if (any(identities(1:candidate_count) == forbidden_identity)) &
           error stop 'TBCC list decoder retained reserved-invalid identity'
    end if
  end subroutine expect_reserved_bit_pruning

  subroutine expect_deterministic_hypothesis_budget()
    type(jtty_tbcc_code_profile) :: code
    complex(real32) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: bits_a(JTTY_TBCC_INFORMATION_BITS, JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: bits_b(JTTY_TBCC_INFORMATION_BITS, JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: states_a(JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: states_b(JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: count_a, count_b, pool_a, pool_b, rank
    integer(int64) :: ids_a(JTTY_TBCC_MAX_HYPOTHESES), ids_b(JTTY_TBCC_MAX_HYPOTHESES)
    real(real64) :: clean_a(JTTY_TBCC_MAX_HYPOTHESES), clean_b(JTTY_TBCC_MAX_HYPOTHESES)
    real(real64) :: path_a(JTTY_TBCC_MAX_HYPOTHESES), path_b(JTTY_TBCC_MAX_HYPOTHESES)
    logical :: crc_a(JTTY_TBCC_MAX_HYPOTHESES), crc_b(JTTY_TBCC_MAX_HYPOTHESES)
    integer(int32) :: one_bit(JTTY_TBCC_INFORMATION_BITS, 1), one_state(1)
    integer(int32) :: one_count, one_pool
    integer(int64) :: one_id(1)
    real(real64) :: one_clean(1), one_path(1)
    logical :: one_crc(1)

    code = JTTY_TBCC_PROFILE_1167_1545_80F
    correlations = cmplx(0.0_real32, 0.0_real32, real32)
    call jtty_tbcc_list_wava(correlations, code, 4_int32, 4_int32, 2_int32, &
         JTTY_TBCC_MAX_HYPOTHESES, bits_a, ids_a, clean_a, path_a, states_a, &
         crc_a, count_a, pool_a, prune_reserved_zero=.true.)
    call jtty_tbcc_list_wava(correlations, code, 4_int32, 4_int32, 2_int32, &
         JTTY_TBCC_MAX_HYPOTHESES, bits_b, ids_b, clean_b, path_b, states_b, &
         crc_b, count_b, pool_b, prune_reserved_zero=.true.)

    if (count_a /= JTTY_TBCC_MAX_HYPOTHESES .or. count_b /= count_a .or. &
         pool_a < count_a .or. pool_b /= pool_a) &
         error stop 'TBCC list decoder did not enforce the H4 budget'
    if (any(bits_a /= bits_b) .or. any(ids_a /= ids_b) .or. &
         any(states_a /= states_b) .or. .not.real_arrays_agree(clean_a, clean_b) .or. &
         .not.real_arrays_agree(path_a, path_b) .or. any(crc_a .neqv. crc_b)) &
         error stop 'TBCC list decoder tie order is not deterministic'
    do rank = 2, count_a
      if (ids_a(rank) <= ids_a(rank - 1)) &
           error stop 'TBCC list decoder did not deduplicate tied identities'
    end do

    call jtty_tbcc_list_wava(correlations, code, 4_int32, 4_int32, 2_int32, &
         1_int32, one_bit, one_id, one_clean, one_path, one_state, one_crc, &
         one_count, one_pool, prune_reserved_zero=.true.)
    if (one_count /= 1_int32 .or. one_id(1) /= ids_a(1) .or. one_pool /= pool_a) &
         error stop 'TBCC list decoder did not enforce the H1 budget'
  end subroutine expect_deterministic_hypothesis_budget

  subroutine expect_coherent_normalization()
    complex(real32) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: tones4(4), tones2(2)
    real(real64) :: metric

    correlations = cmplx(0.0_real32, 0.0_real32, real32)
    tones4 = [0_int32, 1_int32, 2_int32, 3_int32]
    correlations(0, 3) = cmplx(1.0_real32, 0.0_real32, real32)
    correlations(1, 4) = cmplx(2.0_real32, 0.0_real32, real32)
    correlations(2, 5) = cmplx(3.0_real32, 0.0_real32, real32)
    correlations(3, 6) = cmplx(4.0_real32, 0.0_real32, real32)
    metric = jtty_tbcc_coherent_metric(correlations, 3_int32, tones4)
    if (metric /= 25.0_real64) error stop 'TBCC L4 normalization mismatch'

    tones2 = [1_int32, 3_int32]
    correlations(1, 45) = cmplx(2.0_real32, 0.0_real32, real32)
    correlations(3, 46) = cmplx(4.0_real32, 0.0_real32, real32)
    metric = jtty_tbcc_coherent_metric(correlations, 45_int32, tones2)
    if (metric /= 18.0_real64) error stop 'TBCC final L2 normalization mismatch'
  end subroutine expect_coherent_normalization

  subroutine make_codeword(set_reserved_bit, information_bits, tones)
    logical, intent(in) :: set_reserved_bit
    integer(int32), intent(out) :: information_bits(JTTY_TBCC_INFORMATION_BITS)
    integer(int32), intent(out) :: tones(JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: payload(34), encoded(2, JTTY_TBCC_INFORMATION_BITS), bit_index

    do bit_index = 1, size(payload)
      payload(bit_index) = modulo(5*bit_index + bit_index/3, 2)
    end do
    payload(JTTY_TBCC_RESERVED_BIT) = merge(1_int32, 0_int32, set_reserved_bit)
    call encode_crc12(payload, encoded, JTTY_TBCC_PROFILE_1167_1545_80F)
    call tbcc_encode(payload, tones, JTTY_TBCC_PROFILE_1167_1545_80F)
    information_bits = encoded(1, :)
  end subroutine make_codeword

  subroutine make_noiseless_correlations(tones, correlations)
    integer(int32), intent(in) :: tones(JTTY_TBCC_INFORMATION_BITS)
    complex(real32), intent(out) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: symbol

    correlations = cmplx(0.0_real32, 0.0_real32, real32)
    do symbol = 1, JTTY_TBCC_INFORMATION_BITS
      correlations(tones(symbol), symbol) = cmplx(1.0_real32, 0.0_real32, real32)
    end do
  end subroutine make_noiseless_correlations

  pure logical function real_arrays_agree(left, right) result(agree)
    real(real64), intent(in) :: left(:), right(:)
    integer(int32) :: element

    agree = size(left) == size(right)
    if (.not.agree) return
    do element = 1, size(left)
      if (left(element) == right(element)) cycle
      if (abs(left(element)-right(element)) <= 64.0_real64*epsilon(0.0_real64)* &
           max(1.0_real64, abs(left(element)), abs(right(element)))) cycle
      agree = .false.
      return
    end do
  end function real_arrays_agree

end program test_jtty_tbcc_list_decoder
