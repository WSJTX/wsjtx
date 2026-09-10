module jtty_tbcc_list_decoder
  use, intrinsic :: iso_fortran_env, only: int32, int64, real32, real64
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use jtty_tbcc_code_profiles, only: jtty_tbcc_code_profile, &
       jtty_tbcc_code_profile_is_supported, JTTY_TBCC_INFORMATION_BITS
  implicit none
  private

  ! This module keeps decoder breadth and admission breadth separate.  The
  ! shorthand P1/P4 means retaining one/four distinct paths at every trellis
  ! merge.  H1/H4 means returning at most one/four unique, closed codewords to
  ! the CRC admission layer.  A wider path search therefore does not silently
  ! increase the number of CRC hypotheses tested.

  public :: JTTY_TBCC_INFORMATION_BITS
  integer(int32), parameter, public :: JTTY_TBCC_RESERVED_BIT = 33_int32
  integer(int32), parameter, public :: JTTY_TBCC_MAX_HYPOTHESES = 4_int32
  real(real64), parameter :: NEGATIVE_METRIC = -huge(0.0_real64)

  type :: survivor_t
    logical :: valid = .false.
    real(real64) :: metric = NEGATIVE_METRIC
    integer(int32) :: origin_state = -1_int32
    integer(int32) :: bits(JTTY_TBCC_INFORMATION_BITS) = 0_int32
  end type survivor_t

  type :: pool_candidate_t
    integer(int32) :: bits(JTTY_TBCC_INFORMATION_BITS) = 0_int32
    integer(int32) :: start_state = -1_int32
    integer(int64) :: identity = 0_int64
    real(real64) :: clean_metric = NEGATIVE_METRIC
    real(real64) :: wava_metric = NEGATIVE_METRIC
    logical :: crc_valid = .false.
  end type pool_candidate_t

  integer(int32), parameter :: PACKED_ORIGIN_BITS = 11_int32
  integer(int32), parameter :: PACKED_VALID_BIT = JTTY_TBCC_INFORMATION_BITS + PACKED_ORIGIN_BITS

  type :: packed_survivor_t
    real(real64) :: metric = NEGATIVE_METRIC
    ! Low bits hold the input word in lexical order, then origin and validity.
    ! Unwritten input bits stay zero, so integer order also orders every prefix.
    integer(int64) :: key = 0_int64
  end type packed_survivor_t

  type, public :: jtty_tbcc_decoder_plan
    private
    ! Immutable trellis geometry and coherent branch tables shared by decodes.
    logical :: initialized = .false.
    type(jtty_tbcc_code_profile) :: code
    integer(int32) :: coherent_length = 0_int32
    integer(int32) :: per_state_width = 0_int32
    integer(int32) :: wraps = 0_int32
    integer(int32) :: hypothesis_count = 0_int32
    integer(int32) :: state_count = 0_int32
    integer(int32) :: block_count = 0_int32
    integer(int32) :: max_branch_count = 0_int32
    integer(int32) :: pool_capacity = 0_int32
    integer(int32) :: hash_capacity = 0_int32
    integer(int32) :: energy_count = 0_int32
    integer(int32), allocatable :: next_state(:,:), tone(:,:)
    integer(int32), allocatable :: block_starts(:), block_lengths(:)
    integer(int32), allocatable :: branch_counts(:), energy_offsets(:)
    integer(int32), allocatable :: branch_end_states(:,:,:)
    integer(int32), allocatable :: branch_sequence_indices(:,:,:)
    integer(int64), allocatable :: branch_identities(:,:)
  end type jtty_tbcc_decoder_plan

  type, public :: jtty_tbcc_decoder_workspace
    private
    ! Reusable scratch storage keeps allocation outside the receiver hot path.
    logical :: initialized = .false.
    type(packed_survivor_t), allocatable :: previous(:,:), current(:,:)
    type(pool_candidate_t), allocatable :: pool(:)
    integer(int32), allocatable :: order(:), sort_scratch(:), hash_indices(:)
    integer(int64), allocatable :: hash_keys(:)
    real(real64), allocatable :: sequence_energies(:)
    logical, allocatable :: hash_used(:)
  end type jtty_tbcc_decoder_workspace

  public :: jtty_tbcc_transition
  public :: jtty_tbcc_supertransition
  public :: jtty_tbcc_coherent_metric
  public :: jtty_tbcc_score_candidate
  public :: jtty_tbcc_tailbiting_state
  public :: jtty_tbcc_candidate_identity
  public :: jtty_tbcc_crc_valid
  public :: jtty_tbcc_list_wava
  public :: jtty_tbcc_list_wava_reference
  public :: jtty_tbcc_init_decoder_plan
  public :: jtty_tbcc_init_decoder_workspace
  public :: jtty_tbcc_list_wava_optimized

contains

  pure subroutine jtty_tbcc_transition(code, state, input_bit, next_state, tone)
    type(jtty_tbcc_code_profile), intent(in) :: code
    integer(int32), intent(in) :: state, input_bit
    integer(int32), intent(out) :: next_state, tone
    integer(int32) :: output_register, out_b0, out_b1, state_mask

    state_mask = code%state_mask
    output_register = iand(ior(shiftl(state, 1), input_bit), &
         code%register_mask)
    out_b0 = modulo(popcnt(iand(output_register, code%generator_0)), 2)
    out_b1 = modulo(popcnt(iand(output_register, code%generator_1)), 2)
    tone = 2_int32*out_b0 + ieor(out_b0, out_b1)
    next_state = iand(output_register, state_mask)
  end subroutine jtty_tbcc_transition

  pure subroutine jtty_tbcc_supertransition(code, start_state, input_bits, &
       end_state, tones)
    type(jtty_tbcc_code_profile), intent(in) :: code
    integer(int32), intent(in) :: start_state
    integer(int32), intent(in) :: input_bits(:)
    integer(int32), intent(out) :: end_state
    integer(int32), intent(out) :: tones(size(input_bits))
    integer(int32) :: offset, state

    state = start_state
    do offset = 1, size(input_bits)
      call jtty_tbcc_transition(code, state, input_bits(offset), &
           end_state, tones(offset))
      state = end_state
    end do
    end_state = state
  end subroutine jtty_tbcc_supertransition

  pure real(real64) function jtty_tbcc_coherent_metric(correlations, &
       first_symbol, tones) result(metric)
    complex(real32), intent(in) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32), intent(in) :: first_symbol
    integer(int32), intent(in) :: tones(:)
    complex(real32) :: coherent_sum
    integer(int32) :: offset

    coherent_sum = cmplx(0.0_real32, 0.0_real32, real32)
    do offset = 1, size(tones)
      coherent_sum = coherent_sum + &
           correlations(tones(offset), first_symbol + offset - 1)
    end do
    metric = real(coherent_sum*conjg(coherent_sum), real64) / &
         real(size(tones), real64)
  end function jtty_tbcc_coherent_metric

  pure integer(int32) function jtty_tbcc_tailbiting_state(code, bits) &
       result(state)
    type(jtty_tbcc_code_profile), intent(in) :: code
    integer(int32), intent(in) :: bits(JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: bit_index, state_mask

    state = 0_int32
    state_mask = code%state_mask
    do bit_index = JTTY_TBCC_INFORMATION_BITS - code%memory_nu + 1, &
         JTTY_TBCC_INFORMATION_BITS
      state = iand(ior(shiftl(state, 1), bits(bit_index)), state_mask)
    end do
  end function jtty_tbcc_tailbiting_state

  pure subroutine jtty_tbcc_score_candidate(code, correlations, &
       coherent_length, bits, metric, closed)
    type(jtty_tbcc_code_profile), intent(in) :: code
    complex(real32), intent(in) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32), intent(in) :: coherent_length
    integer(int32), intent(in) :: bits(JTTY_TBCC_INFORMATION_BITS)
    real(real64), intent(out) :: metric
    logical, intent(out) :: closed
    integer(int32) :: block_length, block_start, end_state, next_state, start_state
    integer(int32) :: tones(4)

    start_state = jtty_tbcc_tailbiting_state(code, bits)
    end_state = start_state
    metric = 0.0_real64
    do block_start = 1, JTTY_TBCC_INFORMATION_BITS, coherent_length
      block_length = min(coherent_length, &
           JTTY_TBCC_INFORMATION_BITS - block_start + 1_int32)
      call jtty_tbcc_supertransition(code, end_state, &
           bits(block_start:block_start + block_length - 1), next_state, &
           tones(1:block_length))
      metric = metric + jtty_tbcc_coherent_metric(correlations, &
           block_start, tones(1:block_length))
      end_state = next_state
    end do
    closed = end_state == start_state
  end subroutine jtty_tbcc_score_candidate

  pure integer(int64) function jtty_tbcc_candidate_identity(bits) &
       result(identity)
    integer(int32), intent(in) :: bits(JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: bit_index

    identity = 0_int64
    do bit_index = 1, JTTY_TBCC_INFORMATION_BITS
      if (bits(bit_index) /= 0_int32) identity = ibset(identity, bit_index - 1)
    end do
  end function jtty_tbcc_candidate_identity

  pure logical function jtty_tbcc_crc_valid(code, bits) result(valid)
    type(jtty_tbcc_code_profile), intent(in) :: code
    integer(int32), intent(in) :: bits(JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: bit_index, crc_register

    crc_register = 0_int32
    do bit_index = 1, JTTY_TBCC_INFORMATION_BITS
      crc_register = ieor(crc_register, &
           shiftl(bits(bit_index), code%outer_check_bits - 1_int32))
      if (iand(crc_register, code%outer_top_bit_mask) /= 0_int32) then
        crc_register = ieor(shiftl(crc_register, 1), code%outer_polynomial)
      else
        crc_register = shiftl(crc_register, 1)
      end if
      crc_register = iand(crc_register, code%outer_register_mask)
    end do
    valid = crc_register == 0_int32
  end function jtty_tbcc_crc_valid

  subroutine jtty_tbcc_init_decoder_plan(plan, code, coherent_length, &
       per_state_width, wraps, hypothesis_count)
    type(jtty_tbcc_decoder_plan), intent(out) :: plan
    type(jtty_tbcc_code_profile), intent(in) :: code
    integer(int32), intent(in) :: coherent_length, per_state_width, wraps
    integer(int32), intent(in) :: hypothesis_count
    integer(int32) :: block_index, block_length, block_start, branch_word
    integer(int32) :: end_state, input_bit, offset, state, tone_word
    integer(int32) :: input_bits(4), tones(4)
    integer(int64) :: branch_identity

    call validate_decoder_arguments(code, coherent_length, per_state_width, &
         wraps, hypothesis_count)
    if (code%memory_nu > PACKED_ORIGIN_BITS) error stop 'TBCC state does not fit packed survivor'
    plan%code = code
    plan%coherent_length = coherent_length
    plan%per_state_width = per_state_width
    plan%wraps = wraps
    plan%hypothesis_count = hypothesis_count
    plan%state_count = code%state_count
    plan%block_count = (JTTY_TBCC_INFORMATION_BITS + coherent_length - 1_int32) / &
         coherent_length
    plan%max_branch_count = shiftl(1_int32, coherent_length)
    plan%pool_capacity = plan%state_count*per_state_width
    plan%hash_capacity = 1_int32
    do while (plan%hash_capacity < 2_int32*plan%pool_capacity)
      plan%hash_capacity = 2_int32*plan%hash_capacity
    end do

    allocate(plan%next_state(0:plan%state_count - 1, 0:1))
    allocate(plan%tone(0:plan%state_count - 1, 0:1))
    do input_bit = 0, 1
      do state = 0, plan%state_count - 1
        call jtty_tbcc_transition(code, state, input_bit, &
             plan%next_state(state, input_bit), plan%tone(state, input_bit))
      end do
    end do

    allocate(plan%block_starts(plan%block_count), plan%block_lengths(plan%block_count))
    allocate(plan%branch_counts(plan%block_count), plan%energy_offsets(plan%block_count))
    allocate(plan%branch_identities(0:plan%max_branch_count - 1, plan%block_count))
    allocate(plan%branch_end_states(0:plan%state_count - 1, &
         0:plan%max_branch_count - 1, plan%block_count))
    allocate(plan%branch_sequence_indices(0:plan%state_count - 1, &
         0:plan%max_branch_count - 1, plan%block_count))
    plan%branch_identities = 0_int64
    plan%branch_end_states = 0_int32
    plan%branch_sequence_indices = 0_int32
    plan%energy_count = 0_int32
    do block_index = 1, plan%block_count
      block_start = 1_int32 + (block_index - 1_int32)*coherent_length
      block_length = min(coherent_length, &
           JTTY_TBCC_INFORMATION_BITS - block_start + 1_int32)
      plan%block_starts(block_index) = block_start
      plan%block_lengths(block_index) = block_length
      plan%branch_counts(block_index) = shiftl(1_int32, block_length)
      plan%energy_offsets(block_index) = plan%energy_count + 1_int32
      plan%energy_count = plan%energy_count + shiftl(1_int32, 2_int32*block_length)
      do branch_word = 0, plan%branch_counts(block_index) - 1
        call unpack_branch_word(branch_word, block_length, input_bits)
        branch_identity = 0_int64
        do offset = 1, block_length
          if (input_bits(offset) /= 0_int32) &
               branch_identity = ibset(branch_identity, JTTY_TBCC_INFORMATION_BITS - block_start - offset + 1_int32)
        end do
        plan%branch_identities(branch_word, block_index) = branch_identity
        do state = 0, plan%state_count - 1
          call planned_supertransition(plan, state, input_bits, block_length, &
               end_state, tones, tone_word)
          plan%branch_end_states(state, branch_word, block_index) = end_state
          plan%branch_sequence_indices(state, branch_word, block_index) = &
               plan%energy_offsets(block_index) + tone_word
        end do
      end do
    end do
    plan%initialized = .true.
  end subroutine jtty_tbcc_init_decoder_plan

  subroutine jtty_tbcc_init_decoder_workspace(workspace, plan)
    type(jtty_tbcc_decoder_workspace), intent(out) :: workspace
    type(jtty_tbcc_decoder_plan), intent(in) :: plan

    if (.not.plan%initialized) error stop 'TBCC list decoder plan is not initialized'
    allocate(workspace%previous(0:plan%state_count - 1, plan%per_state_width))
    allocate(workspace%current(0:plan%state_count - 1, plan%per_state_width))
    allocate(workspace%pool(max(1_int32, plan%pool_capacity)))
    allocate(workspace%order(max(1_int32, plan%pool_capacity)))
    allocate(workspace%sort_scratch(max(1_int32, plan%pool_capacity)))
    allocate(workspace%hash_keys(plan%hash_capacity))
    allocate(workspace%hash_indices(plan%hash_capacity))
    allocate(workspace%hash_used(plan%hash_capacity))
    allocate(workspace%sequence_energies(plan%energy_count))
    workspace%initialized = .true.
  end subroutine jtty_tbcc_init_decoder_workspace

  subroutine jtty_tbcc_list_wava(correlations, code, coherent_length, &
       per_state_width, wraps, hypothesis_count, candidate_bits, &
       candidate_identities, candidate_clean_metrics, candidate_wava_metrics, &
       candidate_start_states, candidate_crc_valid, candidate_count, pool_count, &
       prune_reserved_zero, reference_bits, reference_present, &
       reference_pool_rank, reference_h_rank)
    complex(real32), intent(in) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    type(jtty_tbcc_code_profile), intent(in) :: code
    integer(int32), intent(in) :: coherent_length, per_state_width, wraps
    integer(int32), intent(in) :: hypothesis_count
    integer(int32), intent(out) :: candidate_bits(JTTY_TBCC_INFORMATION_BITS, hypothesis_count)
    integer(int64), intent(out) :: candidate_identities(hypothesis_count)
    real(real64), intent(out) :: candidate_clean_metrics(hypothesis_count)
    real(real64), intent(out) :: candidate_wava_metrics(hypothesis_count)
    integer(int32), intent(out) :: candidate_start_states(hypothesis_count)
    logical, intent(out) :: candidate_crc_valid(hypothesis_count)
    integer(int32), intent(out) :: candidate_count, pool_count
    logical, intent(in), optional :: prune_reserved_zero
    integer(int32), intent(in), optional :: reference_bits(JTTY_TBCC_INFORMATION_BITS)
    logical, intent(out), optional :: reference_present
    integer(int32), intent(out), optional :: reference_pool_rank, reference_h_rank
    type(jtty_tbcc_decoder_plan) :: plan
    type(jtty_tbcc_decoder_workspace) :: workspace

    call jtty_tbcc_init_decoder_plan(plan, code, coherent_length, &
         per_state_width, wraps, hypothesis_count)
    call jtty_tbcc_init_decoder_workspace(workspace, plan)
    call jtty_tbcc_list_wava_optimized(plan, workspace, correlations, &
         candidate_bits, candidate_identities, candidate_clean_metrics, &
         candidate_wava_metrics, candidate_start_states, candidate_crc_valid, &
         candidate_count, pool_count, prune_reserved_zero, reference_bits, &
         reference_present, reference_pool_rank, reference_h_rank)
  end subroutine jtty_tbcc_list_wava

  subroutine jtty_tbcc_list_wava_optimized(plan, workspace, correlations, &
       candidate_bits, candidate_identities, candidate_clean_metrics, &
       candidate_wava_metrics, candidate_start_states, candidate_crc_valid, &
       candidate_count, pool_count, prune_reserved_zero, reference_bits, &
       reference_present, reference_pool_rank, reference_h_rank)
    type(jtty_tbcc_decoder_plan), intent(in) :: plan
    type(jtty_tbcc_decoder_workspace), intent(inout) :: workspace
    complex(real32), intent(in) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    integer(int32), intent(out) :: candidate_bits(:, :)
    integer(int64), intent(out) :: candidate_identities(:)
    real(real64), intent(out) :: candidate_clean_metrics(:), candidate_wava_metrics(:)
    integer(int32), intent(out) :: candidate_start_states(:)
    logical, intent(out) :: candidate_crc_valid(:)
    integer(int32), intent(out) :: candidate_count, pool_count
    logical, intent(in), optional :: prune_reserved_zero
    integer(int32), intent(in), optional :: reference_bits(JTTY_TBCC_INFORMATION_BITS)
    logical, intent(out), optional :: reference_present
    integer(int32), intent(out), optional :: reference_pool_rank, reference_h_rank
    type(packed_survivor_t), allocatable :: swap_survivors(:,:)
    integer(int32) :: block_index, hash_slot
    integer(int32) :: output_rank, pool_index, rank, state, wrap
    integer(int64) :: identity, reference_identity
    real(real64) :: clean_metric
    logical :: closed, found, prune, ordered_metrics

    call validate_optimized_storage(plan, workspace, candidate_bits, &
         candidate_identities, candidate_clean_metrics, candidate_wava_metrics, &
         candidate_start_states, candidate_crc_valid)
    call precompute_sequence_energies(plan, correlations, workspace%sequence_energies)
    ! Nonfinite metrics do not provide the monotone rank order needed by the cutoff.
    ordered_metrics = all(ieee_is_finite(workspace%sequence_energies))
    call clear_packed_survivors(workspace%previous)
    do state = 0, plan%state_count - 1
      workspace%previous(state, 1)%key = ibset(shiftl(int(state, int64), JTTY_TBCC_INFORMATION_BITS), PACKED_VALID_BIT)
      workspace%previous(state, 1)%metric = 0.0_real64
    end do

    prune = .false.
    if (present(prune_reserved_zero)) prune = prune_reserved_zero
    do wrap = 1, plan%wraps
      call reset_packed_frame_identity(workspace%previous)
      do block_index = 1, plan%block_count
        call advance_packed_survivors(plan, block_index, prune, workspace%sequence_energies, &
             workspace%previous, workspace%current, ordered_metrics)
        call move_alloc(workspace%previous, swap_survivors)
        call move_alloc(workspace%current, workspace%previous)
        call move_alloc(swap_survivors, workspace%current)
      end do
    end do

    pool_count = 0_int32
    workspace%hash_used = .false.
    do state = 0, plan%state_count - 1
      do rank = 1, plan%per_state_width
        if (.not.btest(workspace%previous(state, rank)%key, PACKED_VALID_BIT)) cycle
        ! Tail-biting paths are eligible only when their final and remembered
        ! initial states agree after the last wrap.
        if (ibits(workspace%previous(state, rank)%key, &
             JTTY_TBCC_INFORMATION_BITS, PACKED_ORIGIN_BITS) /= state) cycle
        identity = reverse_identity(workspace%previous(state, rank)%key)
        ! The same complete word can close through more than one retained path;
        ! hash by all 46 bits before applying the global hypothesis limit.
        call locate_identity(identity, workspace%hash_keys, workspace%hash_indices, &
             workspace%hash_used, hash_slot, pool_index, found)
        if (found) then
          if (workspace%previous(state, rank)%metric > &
               workspace%pool(pool_index)%wava_metric .or. &
               (.not.(workspace%previous(state, rank)%metric < &
               workspace%pool(pool_index)%wava_metric) .and. &
               state < workspace%pool(pool_index)%start_state)) then
            workspace%pool(pool_index)%wava_metric = &
                 workspace%previous(state, rank)%metric
            workspace%pool(pool_index)%start_state = state
          end if
          cycle
        end if
        pool_count = pool_count + 1_int32
        pool_index = pool_count
        workspace%hash_used(hash_slot) = .true.
        workspace%hash_keys(hash_slot) = identity
        workspace%hash_indices(hash_slot) = pool_index
        workspace%pool(pool_index)%identity = identity
        workspace%pool(pool_index)%start_state = state
        workspace%pool(pool_index)%wava_metric = workspace%previous(state, rank)%metric
        call unpack_identity(identity, workspace%pool(pool_index)%bits)
        ! WAVA survivor metrics include history from earlier wraps.  Admission
        ! order is based on a clean, single-frame score of each complete word.
        call score_planned_identity(plan, workspace%sequence_energies, identity, &
             clean_metric, closed)
        if (.not.closed) error stop 'closed TBCC survivor failed clean scoring closure'
        workspace%pool(pool_index)%clean_metric = clean_metric
        ! This flag reports the polynomial remainder only.  Message admission,
        ! including the protocol's all-zero rejection, remains with the caller.
        workspace%pool(pool_index)%crc_valid = &
             jtty_tbcc_crc_valid(plan%code, workspace%pool(pool_index)%bits)
      end do
    end do

    do pool_index = 1, pool_count
      workspace%order(pool_index) = pool_index
    end do
    call sort_candidate_order_with_scratch(workspace%pool, workspace%order, &
         workspace%sort_scratch, pool_count)
    candidate_bits = 0_int32
    candidate_identities = 0_int64
    candidate_clean_metrics = NEGATIVE_METRIC
    candidate_wava_metrics = NEGATIVE_METRIC
    candidate_start_states = -1_int32
    candidate_crc_valid = .false.
    candidate_count = min(plan%hypothesis_count, pool_count)
    do output_rank = 1, candidate_count
      pool_index = workspace%order(output_rank)
      candidate_bits(:, output_rank) = workspace%pool(pool_index)%bits
      candidate_identities(output_rank) = workspace%pool(pool_index)%identity
      candidate_clean_metrics(output_rank) = workspace%pool(pool_index)%clean_metric
      candidate_wava_metrics(output_rank) = workspace%pool(pool_index)%wava_metric
      candidate_start_states(output_rank) = workspace%pool(pool_index)%start_state
      candidate_crc_valid(output_rank) = workspace%pool(pool_index)%crc_valid
    end do

    if (present(reference_present)) reference_present = .false.
    if (present(reference_pool_rank)) reference_pool_rank = 0_int32
    if (present(reference_h_rank)) reference_h_rank = 0_int32
    if (present(reference_bits)) then
      reference_identity = jtty_tbcc_candidate_identity(reference_bits)
      do output_rank = 1, pool_count
        pool_index = workspace%order(output_rank)
        if (workspace%pool(pool_index)%identity /= reference_identity) cycle
        if (present(reference_present)) reference_present = .true.
        if (present(reference_pool_rank)) reference_pool_rank = output_rank
        if (present(reference_h_rank)) then
          if (output_rank <= candidate_count) reference_h_rank = output_rank
        end if
        exit
      end do
    end if
  end subroutine jtty_tbcc_list_wava_optimized

  subroutine jtty_tbcc_list_wava_reference(correlations, code, coherent_length, &
       per_state_width, wraps, hypothesis_count, candidate_bits, &
       candidate_identities, candidate_clean_metrics, candidate_wava_metrics, &
       candidate_start_states, candidate_crc_valid, candidate_count, pool_count, &
       prune_reserved_zero, reference_bits, reference_present, &
       reference_pool_rank, reference_h_rank)
    complex(real32), intent(in) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    type(jtty_tbcc_code_profile), intent(in) :: code
    integer(int32), intent(in) :: coherent_length, per_state_width, wraps
    integer(int32), intent(in) :: hypothesis_count
    integer(int32), intent(out) :: candidate_bits(JTTY_TBCC_INFORMATION_BITS, hypothesis_count)
    integer(int64), intent(out) :: candidate_identities(hypothesis_count)
    real(real64), intent(out) :: candidate_clean_metrics(hypothesis_count)
    real(real64), intent(out) :: candidate_wava_metrics(hypothesis_count)
    integer(int32), intent(out) :: candidate_start_states(hypothesis_count)
    logical, intent(out) :: candidate_crc_valid(hypothesis_count)
    integer(int32), intent(out) :: candidate_count, pool_count
    logical, intent(in), optional :: prune_reserved_zero
    integer(int32), intent(in), optional :: reference_bits(JTTY_TBCC_INFORMATION_BITS)
    logical, intent(out), optional :: reference_present
    integer(int32), intent(out), optional :: reference_pool_rank
    integer(int32), intent(out), optional :: reference_h_rank
    type(survivor_t), allocatable :: previous(:,:), current(:,:)
    type(pool_candidate_t), allocatable :: pool(:)
    integer(int32), allocatable :: order(:), hash_indices(:)
    integer(int32), allocatable :: block_starts(:), block_lengths(:)
    integer(int32), allocatable :: branch_bits(:,:,:), branch_end_states(:,:,:)
    integer(int64), allocatable :: hash_keys(:)
    real(real64), allocatable :: branch_metrics(:,:,:)
    logical, allocatable :: hash_used(:)
    integer(int32) :: block_length, block_start, branch_count, branch_word
    integer(int32) :: block_count, block_index, max_branch_count
    integer(int32) :: state_count, state, rank, wrap, end_state, output_rank
    integer(int32) :: input_bits(4), tones(4), pool_capacity, hash_capacity
    integer(int32) :: pool_index, hash_slot
    integer(int64) :: identity, reference_identity
    real(real64) :: branch_metric, clean_metric
    logical :: prune, closed, found
    type(survivor_t) :: extension

    call validate_decoder_arguments(code, coherent_length, per_state_width, &
         wraps, hypothesis_count)
    state_count = code%state_count
    block_count = (JTTY_TBCC_INFORMATION_BITS + coherent_length - 1_int32) / coherent_length
    max_branch_count = shiftl(1_int32, coherent_length)
    pool_capacity = state_count*per_state_width
    hash_capacity = 1_int32
    do while (hash_capacity < 2_int32*pool_capacity)
      hash_capacity = 2_int32*hash_capacity
    end do

    allocate(previous(0:state_count - 1, per_state_width))
    allocate(current(0:state_count - 1, per_state_width))
    allocate(pool(max(1_int32, pool_capacity)))
    allocate(order(max(1_int32, pool_capacity)))
    allocate(hash_keys(hash_capacity), hash_indices(hash_capacity))
    allocate(hash_used(hash_capacity))
    allocate(block_starts(block_count), block_lengths(block_count))
    allocate(branch_bits(4, 0:max_branch_count - 1, block_count))
    allocate(branch_end_states(0:state_count - 1, 0:max_branch_count - 1, block_count))
    allocate(branch_metrics(0:state_count - 1, 0:max_branch_count - 1, block_count))
    branch_bits = 0_int32
    branch_end_states = 0_int32
    branch_metrics = 0.0_real64
    do block_index = 1, block_count
      block_start = 1_int32 + (block_index - 1_int32)*coherent_length
      block_length = min(coherent_length, &
           JTTY_TBCC_INFORMATION_BITS - block_start + 1_int32)
      block_starts(block_index) = block_start
      block_lengths(block_index) = block_length
      branch_count = shiftl(1_int32, block_length)
      do branch_word = 0, branch_count - 1
        call unpack_branch_word(branch_word, block_length, input_bits)
        branch_bits(:, branch_word, block_index) = input_bits
        do state = 0, state_count - 1
          call jtty_tbcc_supertransition(code, state, &
               input_bits(1:block_length), end_state, tones(1:block_length))
          branch_end_states(state, branch_word, block_index) = end_state
          branch_metrics(state, branch_word, block_index) = &
               jtty_tbcc_coherent_metric(correlations, block_start, &
               tones(1:block_length))
        end do
      end do
    end do
    call clear_survivors(previous)
    do state = 0, state_count - 1
      previous(state, 1)%valid = .true.
      previous(state, 1)%metric = 0.0_real64
      previous(state, 1)%origin_state = state
    end do

    prune = .false.
    if (present(prune_reserved_zero)) prune = prune_reserved_zero
    do wrap = 1, wraps
      call reset_frame_identity(previous)
      do block_index = 1, block_count
        block_start = block_starts(block_index)
        block_length = block_lengths(block_index)
        branch_count = shiftl(1_int32, block_length)
        call clear_survivors(current)
        do state = 0, state_count - 1
          do rank = 1, per_state_width
            if (.not.previous(state, rank)%valid) cycle
            do branch_word = 0, branch_count - 1
              if (prune .and. block_contains_reserved(block_start, block_length)) then
                if (branch_bits(JTTY_TBCC_RESERVED_BIT - block_start + 1, &
                     branch_word, block_index) /= &
                     0_int32) cycle
              end if
              end_state = branch_end_states(state, branch_word, block_index)
              branch_metric = branch_metrics(state, branch_word, block_index)
              extension = previous(state, rank)
              extension%metric = extension%metric + branch_metric
              extension%bits(block_start:block_start + block_length - 1) = &
                   branch_bits(1:block_length, branch_word, block_index)
              call insert_survivor(current(end_state, :), extension, &
                   block_start + block_length - 1_int32)
            end do
          end do
        end do
        previous = current
      end do
    end do

    pool_count = 0_int32
    hash_used = .false.
    hash_keys = 0_int64
    hash_indices = 0_int32
    do state = 0, state_count - 1
      do rank = 1, per_state_width
        if (.not.previous(state, rank)%valid) cycle
        if (previous(state, rank)%origin_state /= state) cycle
        identity = jtty_tbcc_candidate_identity(previous(state, rank)%bits)
        call locate_identity(identity, hash_keys, hash_indices, hash_used, &
             hash_slot, pool_index, found)
        if (found) then
          if (previous(state, rank)%metric > pool(pool_index)%wava_metric .or. &
               (.not.(previous(state, rank)%metric < pool(pool_index)%wava_metric) .and. &
                state < pool(pool_index)%start_state)) then
            pool(pool_index)%wava_metric = previous(state, rank)%metric
            pool(pool_index)%start_state = state
          end if
          cycle
        end if
        pool_count = pool_count + 1_int32
        pool_index = pool_count
        hash_used(hash_slot) = .true.
        hash_keys(hash_slot) = identity
        hash_indices(hash_slot) = pool_index
        pool(pool_index)%bits = previous(state, rank)%bits
        pool(pool_index)%identity = identity
        pool(pool_index)%start_state = state
        pool(pool_index)%wava_metric = previous(state, rank)%metric
        call jtty_tbcc_score_candidate(code, correlations, coherent_length, &
             pool(pool_index)%bits, clean_metric, closed)
        if (.not.closed) error stop 'closed TBCC survivor failed clean scoring closure'
        pool(pool_index)%clean_metric = clean_metric
        pool(pool_index)%crc_valid = jtty_tbcc_crc_valid(code, pool(pool_index)%bits)
      end do
    end do

    do pool_index = 1, pool_count
      order(pool_index) = pool_index
    end do
    call sort_candidate_order(pool, order, pool_count)
    candidate_bits = 0_int32
    candidate_identities = 0_int64
    candidate_clean_metrics = NEGATIVE_METRIC
    candidate_wava_metrics = NEGATIVE_METRIC
    candidate_start_states = -1_int32
    candidate_crc_valid = .false.
    candidate_count = min(hypothesis_count, pool_count)
    do output_rank = 1, candidate_count
      pool_index = order(output_rank)
      candidate_bits(:, output_rank) = pool(pool_index)%bits
      candidate_identities(output_rank) = pool(pool_index)%identity
      candidate_clean_metrics(output_rank) = pool(pool_index)%clean_metric
      candidate_wava_metrics(output_rank) = pool(pool_index)%wava_metric
      candidate_start_states(output_rank) = pool(pool_index)%start_state
      candidate_crc_valid(output_rank) = pool(pool_index)%crc_valid
    end do

    if (present(reference_present)) reference_present = .false.
    if (present(reference_pool_rank)) reference_pool_rank = 0_int32
    if (present(reference_h_rank)) reference_h_rank = 0_int32
    if (present(reference_bits)) then
      reference_identity = jtty_tbcc_candidate_identity(reference_bits)
      do output_rank = 1, pool_count
        pool_index = order(output_rank)
        if (pool(pool_index)%identity /= reference_identity) cycle
        if (.not.all(pool(pool_index)%bits == reference_bits)) cycle
        if (present(reference_present)) reference_present = .true.
        if (present(reference_pool_rank)) reference_pool_rank = output_rank
        if (present(reference_h_rank)) then
          if (output_rank <= candidate_count) reference_h_rank = output_rank
        end if
        exit
      end do
    end if

    deallocate(previous, current, pool, order, hash_keys, hash_indices, hash_used)
    deallocate(block_starts, block_lengths, branch_bits, branch_end_states, branch_metrics)
  end subroutine jtty_tbcc_list_wava_reference


  pure subroutine planned_supertransition(plan, start_state, input_bits, &
       block_length, end_state, tones, tone_word)
    type(jtty_tbcc_decoder_plan), intent(in) :: plan
    integer(int32), intent(in) :: start_state, input_bits(4), block_length
    integer(int32), intent(out) :: end_state, tones(4), tone_word
    integer(int32) :: offset, state

    state = start_state
    tones = 0_int32
    tone_word = 0_int32
    do offset = 1, block_length
      tones(offset) = plan%tone(state, input_bits(offset))
      tone_word = 4_int32*tone_word + tones(offset)
      state = plan%next_state(state, input_bits(offset))
    end do
    end_state = state
  end subroutine planned_supertransition

  subroutine precompute_sequence_energies(plan, correlations, energies)
    type(jtty_tbcc_decoder_plan), intent(in) :: plan
    complex(real32), intent(in) :: correlations(0:3, JTTY_TBCC_INFORMATION_BITS)
    real(real64), intent(out) :: energies(:)
    complex(real32) :: coherent_sum
    integer(int32) :: block_index, block_length, block_start
    integer(int32) :: offset, sequence_count, sequence_word, tone

    do block_index = 1, plan%block_count
      block_start = plan%block_starts(block_index)
      block_length = plan%block_lengths(block_index)
      sequence_count = shiftl(1_int32, 2_int32*block_length)
      do sequence_word = 0, sequence_count - 1
        coherent_sum = cmplx(0.0_real32, 0.0_real32, real32)
        do offset = 1, block_length
          tone = ibits(sequence_word, 2_int32*(block_length - offset), 2)
          coherent_sum = coherent_sum + correlations(tone, block_start + offset - 1_int32)
        end do
        energies(plan%energy_offsets(block_index) + sequence_word) = &
             real(coherent_sum*conjg(coherent_sum), real64) / real(block_length, real64)
      end do
    end do
  end subroutine precompute_sequence_energies

  subroutine score_planned_identity(plan, energies, identity, metric, closed)
    type(jtty_tbcc_decoder_plan), intent(in) :: plan
    real(real64), intent(in) :: energies(:)
    integer(int64), intent(in) :: identity
    real(real64), intent(out) :: metric
    logical, intent(out) :: closed
    integer(int32) :: bit_index, block_index, branch_word, offset, state, start_state

    start_state = 0_int32
    do bit_index = JTTY_TBCC_INFORMATION_BITS - plan%code%memory_nu + 1, &
         JTTY_TBCC_INFORMATION_BITS
      start_state = iand(ior(shiftl(start_state, 1), &
           merge(1_int32, 0_int32, btest(identity, bit_index - 1))), &
           plan%state_count - 1_int32)
    end do
    state = start_state
    metric = 0.0_real64
    do block_index = 1, plan%block_count
      branch_word = 0_int32
      do offset = 0, plan%block_lengths(block_index) - 1
        branch_word = 2_int32*branch_word + merge(1_int32, 0_int32, &
             btest(identity, plan%block_starts(block_index) + offset - 1_int32))
      end do
      metric = metric + energies( &
           plan%branch_sequence_indices(state, branch_word, block_index))
      state = plan%branch_end_states(state, branch_word, block_index)
    end do
    closed = state == start_state
  end subroutine score_planned_identity

  pure integer(int64) function reverse_identity(key) result(identity)
    integer(int64), intent(in) :: key
    integer(int32) :: bit_index

    identity = 0_int64
    do bit_index = 0, JTTY_TBCC_INFORMATION_BITS - 1
      if (btest(key, bit_index)) identity = ibset(identity, JTTY_TBCC_INFORMATION_BITS - bit_index - 1)
    end do
  end function reverse_identity

  pure subroutine unpack_identity(identity, bits)
    integer(int64), intent(in) :: identity
    integer(int32), intent(out) :: bits(JTTY_TBCC_INFORMATION_BITS)
    integer(int32) :: bit_index

    do bit_index = 1, JTTY_TBCC_INFORMATION_BITS
      bits(bit_index) = merge(1_int32, 0_int32, btest(identity, bit_index - 1))
    end do
  end subroutine unpack_identity

  subroutine clear_packed_survivors(survivors)
    type(packed_survivor_t), intent(inout) :: survivors(0:, :)
    integer(int32) :: rank, state

    do rank = 1, size(survivors, 2)
      do state = 0, size(survivors, 1) - 1
        survivors(state, rank)%key = 0_int64
        survivors(state, rank)%metric = NEGATIVE_METRIC
      end do
    end do
  end subroutine clear_packed_survivors

  subroutine reset_packed_frame_identity(survivors)
    type(packed_survivor_t), intent(inout) :: survivors(0:, :)
    integer(int32) :: rank, state

    do rank = 1, size(survivors, 2)
      do state = 0, size(survivors, 1) - 1
        if (.not.btest(survivors(state, rank)%key, PACKED_VALID_BIT)) cycle
        survivors(state, rank)%key = ibset(shiftl(int(state, int64), JTTY_TBCC_INFORMATION_BITS), PACKED_VALID_BIT)
      end do
    end do
  end subroutine reset_packed_frame_identity

  subroutine advance_packed_survivors(plan, block_index, prune, energies, previous, current, ordered_metrics)
    type(jtty_tbcc_decoder_plan), intent(in) :: plan
    integer(int32), intent(in) :: block_index
    logical, intent(in) :: prune, ordered_metrics
    real(real64), intent(in) :: energies(:)
    type(packed_survivor_t), intent(in) :: previous(0:, :)
    type(packed_survivor_t), intent(inout) :: current(0:, :)
    type(packed_survivor_t) :: selected(4), extension
    integer(int32) :: branch_count, branch_word, end_state, state, state_stride
    integer(int32) :: rank, width
    integer(int64) :: branch_identity
    real(real64) :: branch_metric, metric
    logical :: prune_block

    width = plan%per_state_width
    branch_count = plan%branch_counts(block_index)
    state_stride = plan%state_count / branch_count
    prune_block = prune .and. block_contains_reserved( &
         plan%block_starts(block_index), plan%block_lengths(block_index))
    do end_state = 0, plan%state_count - 1
      selected%key = 0_int64
      branch_word = iand(end_state, branch_count - 1_int32)
      branch_identity = plan%branch_identities(branch_word, block_index)
      if (.not.(prune_block .and. btest(branch_identity, JTTY_TBCC_INFORMATION_BITS - JTTY_TBCC_RESERVED_BIT))) then
        ! The low L destination bits are the input word; the discarded high
        ! L source bits enumerate every predecessor in the original order.
        do state = end_state / branch_count, plan%state_count - 1, state_stride
          branch_metric = energies(plan%branch_sequence_indices(state, branch_word, block_index))
          do rank = 1, width
            if (.not.btest(previous(state, rank)%key, PACKED_VALID_BIT)) cycle
            metric = previous(state, rank)%metric + branch_metric
            if (btest(selected(width)%key, PACKED_VALID_BIT)) then
              if (metric < selected(width)%metric) then
                ! Lower ranks receive the same branch metric and cannot recover.
                if (ordered_metrics) exit
                cycle
              end if
            end if
            extension = previous(state, rank)
            extension%metric = metric
            extension%key = ior(extension%key, branch_identity)
            call insert_packed_survivor(selected, width, extension, block_index == 1)
          end do
        end do
      end if
      do rank = 1, width
        current(end_state, rank) = selected(rank)
      end do
    end do
  end subroutine advance_packed_survivors

  subroutine insert_packed_survivor(survivors, width, candidate, deduplicate)
    type(packed_survivor_t), intent(inout) :: survivors(4)
    type(packed_survivor_t), intent(in) :: candidate
    integer(int32), intent(in) :: width
    logical, intent(in) :: deduplicate
    integer(int32) :: insertion_slot, slot

    ! Resetting a wrap's origins and identities can duplicate a state's ranks.
    ! After the first block, an origin and prefix determine exactly one path,
    ! so extending distinct retained paths cannot create another duplicate.
    if (deduplicate) then
      do slot = 1, width
        if (.not.btest(survivors(slot)%key, PACKED_VALID_BIT)) cycle
        if (survivors(slot)%key == candidate%key) then
          if (candidate%metric <= survivors(slot)%metric) return
          do insertion_slot = 1, 3
            if (insertion_slot >= slot .and. insertion_slot < width) &
                 survivors(insertion_slot) = survivors(insertion_slot + 1)
          end do
          survivors(width)%key = 0_int64
          exit
        end if
      end do
    end if

    insertion_slot = width + 1_int32
    do slot = 1, width
      if (.not.btest(survivors(slot)%key, PACKED_VALID_BIT) .or. &
           packed_survivor_precedes(candidate, survivors(slot))) then
        insertion_slot = slot
        exit
      end if
    end do
    if (insertion_slot > width) return
    do slot = 4, 2, -1
      if (slot <= width .and. slot > insertion_slot) survivors(slot) = survivors(slot - 1)
    end do
    survivors(insertion_slot) = candidate
  end subroutine insert_packed_survivor

  pure logical function packed_survivor_precedes(left, right) &
       result(precedes)
    type(packed_survivor_t), intent(in) :: left, right

    if (left%metric > right%metric) then
      precedes = .true.
      return
    else if (left%metric < right%metric) then
      precedes = .false.
      return
    end if
    precedes = left%key < right%key
  end function packed_survivor_precedes

  subroutine validate_optimized_storage(plan, workspace, candidate_bits, &
       candidate_identities, candidate_clean_metrics, candidate_wava_metrics, &
       candidate_start_states, candidate_crc_valid)
    type(jtty_tbcc_decoder_plan), intent(in) :: plan
    type(jtty_tbcc_decoder_workspace), intent(in) :: workspace
    integer(int32), intent(in) :: candidate_bits(:, :)
    integer(int64), intent(in) :: candidate_identities(:)
    real(real64), intent(in) :: candidate_clean_metrics(:), candidate_wava_metrics(:)
    integer(int32), intent(in) :: candidate_start_states(:)
    logical, intent(in) :: candidate_crc_valid(:)

    if (.not.plan%initialized) error stop 'TBCC list decoder plan is not initialized'
    if (.not.workspace%initialized) &
         error stop 'TBCC list decoder workspace is not initialized'
    if (size(workspace%previous, 1) /= plan%state_count .or. &
         size(workspace%previous, 2) /= plan%per_state_width .or. &
         size(workspace%sequence_energies) /= plan%energy_count) &
         error stop 'TBCC list decoder workspace does not match plan'
    if (size(candidate_bits, 1) /= JTTY_TBCC_INFORMATION_BITS .or. &
         size(candidate_bits, 2) < plan%hypothesis_count .or. &
         size(candidate_identities) < plan%hypothesis_count .or. &
         size(candidate_clean_metrics) < plan%hypothesis_count .or. &
         size(candidate_wava_metrics) < plan%hypothesis_count .or. &
         size(candidate_start_states) < plan%hypothesis_count .or. &
         size(candidate_crc_valid) < plan%hypothesis_count) &
         error stop 'TBCC list decoder output storage is too small'
  end subroutine validate_optimized_storage


  subroutine validate_decoder_arguments(code, coherent_length, per_state_width, &
       wraps, hypothesis_count)
    type(jtty_tbcc_code_profile), intent(in) :: code
    integer(int32), intent(in) :: coherent_length, per_state_width, wraps
    integer(int32), intent(in) :: hypothesis_count
    if (.not.jtty_tbcc_code_profile_is_supported(code)) &
         error stop 'TBCC list decoder requires a supported code profile'
    if (coherent_length /= 1_int32 .and. coherent_length /= 2_int32 .and. &
         coherent_length /= 4_int32) &
         error stop 'TBCC list decoder supports coherent lengths 1, 2, and 4'
    if (per_state_width < 1_int32 .or. per_state_width > 4_int32) &
         error stop 'TBCC list decoder per-state width must be between 1 and 4'
    if (wraps < 1_int32 .or. wraps > 2_int32) &
         error stop 'TBCC list decoder supports one or two wraps'
    if (hypothesis_count < 1_int32 .or. &
         hypothesis_count > JTTY_TBCC_MAX_HYPOTHESES) &
         error stop 'TBCC list decoder hypothesis count must be between 1 and 4'
  end subroutine validate_decoder_arguments

  subroutine clear_survivors(survivors)
    type(survivor_t), intent(inout) :: survivors(0:, :)
    integer(int32) :: rank, state

    do rank = 1, size(survivors, 2)
      do state = 0, size(survivors, 1) - 1
        survivors(state, rank)%valid = .false.
        survivors(state, rank)%metric = NEGATIVE_METRIC
        survivors(state, rank)%origin_state = -1_int32
        survivors(state, rank)%bits = 0_int32
      end do
    end do
  end subroutine clear_survivors

  subroutine reset_frame_identity(survivors)
    type(survivor_t), intent(inout) :: survivors(0:, :)
    integer(int32) :: rank, state

    do rank = 1, size(survivors, 2)
      do state = 0, size(survivors, 1) - 1
        if (.not.survivors(state, rank)%valid) cycle
        survivors(state, rank)%origin_state = state
        survivors(state, rank)%bits = 0_int32
      end do
    end do
  end subroutine reset_frame_identity

  pure subroutine unpack_branch_word(branch_word, block_length, input_bits)
    integer(int32), intent(in) :: branch_word, block_length
    integer(int32), intent(out) :: input_bits(4)
    integer(int32) :: offset

    input_bits = 0_int32
    do offset = 1, block_length
      input_bits(offset) = ibits(branch_word, block_length - offset, 1)
    end do
  end subroutine unpack_branch_word

  pure logical function block_contains_reserved(block_start, block_length) &
       result(contains_reserved)
    integer(int32), intent(in) :: block_start, block_length

    contains_reserved = block_start <= JTTY_TBCC_RESERVED_BIT .and. &
         block_start + block_length > JTTY_TBCC_RESERVED_BIT
  end function block_contains_reserved

  subroutine insert_survivor(survivors, candidate, prefix_length)
    type(survivor_t), intent(inout) :: survivors(:)
    type(survivor_t), intent(in) :: candidate
    integer(int32), intent(in) :: prefix_length
    integer(int32) :: slot, insertion_slot

    do slot = 1, size(survivors)
      if (.not.survivors(slot)%valid) cycle
      if (survivors(slot)%origin_state /= candidate%origin_state) cycle
      if (.not.all(survivors(slot)%bits(1:prefix_length) == &
           candidate%bits(1:prefix_length))) cycle
      if (candidate%metric <= survivors(slot)%metric) return
      do insertion_slot = slot, size(survivors) - 1
        survivors(insertion_slot) = survivors(insertion_slot + 1)
      end do
      survivors(size(survivors))%valid = .false.
      survivors(size(survivors))%metric = NEGATIVE_METRIC
      survivors(size(survivors))%origin_state = -1_int32
      survivors(size(survivors))%bits = 0_int32
      exit
    end do

    insertion_slot = size(survivors) + 1
    do slot = 1, size(survivors)
      if (.not.survivors(slot)%valid .or. &
           survivor_precedes(candidate, survivors(slot), prefix_length)) then
        insertion_slot = slot
        exit
      end if
    end do
    if (insertion_slot > size(survivors)) return
    do slot = size(survivors), insertion_slot + 1, -1
      survivors(slot) = survivors(slot - 1)
    end do
    survivors(insertion_slot) = candidate
  end subroutine insert_survivor

  pure logical function survivor_precedes(left, right, prefix_length) result(precedes)
    type(survivor_t), intent(in) :: left, right
    integer(int32), intent(in) :: prefix_length
    integer(int32) :: bit_index

    if (left%metric > right%metric) then
      precedes = .true.
      return
    else if (left%metric < right%metric) then
      precedes = .false.
      return
    else
      precedes = left%metric > right%metric
    end if
    if (left%origin_state /= right%origin_state) then
      precedes = left%origin_state < right%origin_state
      return
    end if
    do bit_index = 1, prefix_length
      if (left%bits(bit_index) == right%bits(bit_index)) cycle
      precedes = left%bits(bit_index) < right%bits(bit_index)
      return
    end do
    precedes = .false.
  end function survivor_precedes

  subroutine locate_identity(identity, hash_keys, hash_indices, hash_used, &
       hash_slot, pool_index, found)
    integer(int64), intent(in) :: identity
    integer(int64), intent(in) :: hash_keys(:)
    integer(int32), intent(in) :: hash_indices(:)
    logical, intent(in) :: hash_used(:)
    integer(int32), intent(out) :: hash_slot, pool_index
    logical, intent(out) :: found

    hash_slot = 1_int32 + int(modulo(identity, int(size(hash_keys), int64)), int32)
    do
      if (.not.hash_used(hash_slot)) then
        pool_index = 0_int32
        found = .false.
        return
      end if
      if (hash_keys(hash_slot) == identity) then
        pool_index = hash_indices(hash_slot)
        found = .true.
        return
      end if
      hash_slot = hash_slot + 1_int32
      if (hash_slot > size(hash_keys)) hash_slot = 1_int32
    end do
  end subroutine locate_identity

  subroutine sort_candidate_order(pool, order, count)
    type(pool_candidate_t), intent(in) :: pool(:)
    integer(int32), intent(inout) :: order(:)
    integer(int32), intent(in) :: count
    integer(int32), allocatable :: scratch(:)

    if (count <= 1_int32) return
    allocate(scratch(count))
    call sort_candidate_order_with_scratch(pool, order, scratch, count)
    deallocate(scratch)
  end subroutine sort_candidate_order

  subroutine sort_candidate_order_with_scratch(pool, order, scratch, count)
    type(pool_candidate_t), intent(in) :: pool(:)
    integer(int32), intent(inout) :: order(:), scratch(:)
    integer(int32), intent(in) :: count
    integer(int32) :: left, middle, right, width

    if (count <= 1_int32) return
    width = 1_int32
    do while (width < count)
      left = 1_int32
      do while (left <= count)
        middle = min(left + width, count + 1_int32)
        right = min(left + 2_int32*width - 1_int32, count)
        call merge_candidate_order(pool, order, scratch, left, middle, right)
        left = left + 2_int32*width
      end do
      order(1:count) = scratch(1:count)
      width = 2_int32*width
    end do
  end subroutine sort_candidate_order_with_scratch

  subroutine merge_candidate_order(pool, order, scratch, left, middle, right)
    type(pool_candidate_t), intent(in) :: pool(:)
    integer(int32), intent(in) :: order(:), left, middle, right
    integer(int32), intent(inout) :: scratch(:)
    integer(int32) :: left_cursor, right_cursor, output

    left_cursor = left
    right_cursor = middle
    do output = left, right
      if (left_cursor >= middle) then
        scratch(output) = order(right_cursor)
        right_cursor = right_cursor + 1_int32
      else if (right_cursor > right) then
        scratch(output) = order(left_cursor)
        left_cursor = left_cursor + 1_int32
      else if (candidate_precedes(pool(order(left_cursor)), &
           pool(order(right_cursor)))) then
        scratch(output) = order(left_cursor)
        left_cursor = left_cursor + 1_int32
      else
        scratch(output) = order(right_cursor)
        right_cursor = right_cursor + 1_int32
      end if
    end do
  end subroutine merge_candidate_order

  pure logical function candidate_precedes(left, right) result(precedes)
    type(pool_candidate_t), intent(in) :: left, right

    if (left%clean_metric > right%clean_metric) then
      precedes = .true.
    else if (left%clean_metric < right%clean_metric) then
      precedes = .false.
    else if (left%identity /= right%identity) then
      precedes = left%identity < right%identity
    else if (left%start_state /= right%start_state) then
      precedes = left%start_state < right%start_state
    else
      precedes = left%wava_metric > right%wava_metric
    end if
  end function candidate_precedes

end module jtty_tbcc_list_decoder
