program test_jtty_tbcc_decoder
  use, intrinsic :: iso_fortran_env, only: int32, int64, real32
  use jtty_tbcc_code_profiles, only: JTTY_TBCC_PROFILE_1167_1545_80F
  use jtty_tbcc_decoder, only: jtty_tbcc_decode, jtty_tbcc_decode_result
  use tbcc, only: PAYLOAD_BITS, TOTAL_K, tbcc_encode
  implicit none

  complex(real32) :: correlations(0:3, TOTAL_K), halves(0:3, TOTAL_K)
  integer(int32) :: payload(PAYLOAD_BITS), decoded(PAYLOAD_BITS), tones(TOTAL_K)
  type(jtty_tbcc_decode_result) :: result
  logical :: success
  integer(int32) :: bit_index

  do bit_index = 1, PAYLOAD_BITS
    payload(bit_index) = modulo(bit_index + bit_index/3, 2)
  end do
  payload(33) = 0_int32
  halves = cmplx(0.0_real32, 0.0_real32, real32)
  call tbcc_encode(payload, tones, JTTY_TBCC_PROFILE_1167_1545_80F)
  call make_noiseless_correlations(tones, correlations)

  call jtty_tbcc_decode(correlations, halves, decoded, success, result)
  call require(success, 'coherence ladder rejected a noiseless payload')
  call require(all(decoded == payload), 'coherence ladder changed the payload')
  call require(result%accepted_hypothesis_rank == 1_int32, &
       'noiseless payload was not the first hypothesis')
  call require(result%coherent_block_length == 1_int32, &
       'noiseless payload did not stop at one-symbol coherence')
  call require(result%evaluated_rung_count == 1_int32, &
       'noiseless payload evaluated unnecessary coherence rungs')
  call require(.not.result%used_half_symbol_observation, &
       'rank-one payload unexpectedly evaluated half-symbol fallback')
  call require(result%exported_candidate_count <= 4_int32, &
       'coherence ladder exposed more than four hypotheses per rung')

  payload(33) = 1_int32
  call tbcc_encode(payload, tones, JTTY_TBCC_PROFILE_1167_1545_80F)
  call make_noiseless_correlations(tones, correlations)
  decoded = huge(0_int32)
  call jtty_tbcc_decode(correlations, halves, decoded, success, result)
  call require(.not.success, 'coherence ladder accepted reserved bit one')
  call require(all(decoded == 0_int32), &
       'reserved-bit rejection left a stale payload')

  halves = correlations
  correlations = cmplx(0.0_real32, 0.0_real32, real32)
  decoded = huge(0_int32)
  call jtty_tbcc_decode(correlations, halves, decoded, success, result)
  call require(.not.success, 'half-symbol fallback accepted reserved bit one')
  call require(all(decoded == 0_int32), &
       'half-symbol reserved-bit rejection left a stale payload')

  payload(33) = 0_int32
  call tbcc_encode(payload, tones, JTTY_TBCC_PROFILE_1167_1545_80F)
  call make_noiseless_correlations(tones, halves)
  decoded = huge(0_int32)
  call jtty_tbcc_decode(correlations, halves, decoded, success, result)
  call require(success .and. all(decoded == payload), 'half-symbol fallback failed to rescue payload')
  call require(result%used_half_symbol_observation .and. result%coherent_block_length == 1, &
       'half-symbol fallback used the wrong observation or coherence')
  call require(result%evaluated_rung_count == 4, 'fallback skipped an M1 rung')
  halves = cmplx(0.0_real32, 0.0_real32, real32)

  payload = 0_int32
  call tbcc_encode(payload, tones, JTTY_TBCC_PROFILE_1167_1545_80F)
  call make_noiseless_correlations(tones, correlations)
  decoded = huge(0_int32)
  call jtty_tbcc_decode(correlations, halves, decoded, success, result)
  call require(.not.success, 'coherence ladder accepted the all-zero payload')
  call require(all(decoded == 0_int32), 'failed ladder decode left stale payload')
  call require(result%evaluated_rung_count == 4_int32, &
       'failed decode did not report all evaluated rungs')

  correlations = cmplx(0.0_real32, 0.0_real32, real32)
  decoded = huge(0_int32)
  call jtty_tbcc_decode(correlations, halves, decoded, success, result)
  call require(.not.success, 'coherence ladder accepted flat correlations')
  call require(all(decoded == 0_int32), 'flat-correlation decode left stale payload')
  call require(result%evaluated_rung_count == 4_int32, &
       'flat-correlation decode did not report all evaluated rungs')

  call expect_coherent_decodes()

  print *, 'test_jtty_tbcc_decoder: all checks passed'

contains

  subroutine expect_coherent_decodes()
    ! Seed 6 verifies that reserved-bit pruning retains the valid L1 path.
    integer(int32), parameter :: seeds(2) = [2,6], lengths(2) = [2,1], &
         ranks(2) = [2,1], evaluated_rungs(2) = [2,1]
    integer(int64) :: state
    integer :: fixture, symbol, tone, bit
    real(real32) :: in_phase, quadrature

    do bit = 1, PAYLOAD_BITS
      payload(bit) = modulo(bit + bit/3,2)
    end do
    payload(33) = 0
    call tbcc_encode(payload,tones,JTTY_TBCC_PROFILE_1167_1545_80F)
    halves = cmplx(0.0_real32,0.0_real32,real32)
    do fixture = 1, size(seeds)
      state = seeds(fixture)
      do symbol = 1, TOTAL_K
        do tone = 0, 3
          state = modulo(48271_int64*state,2147483647_int64)
          in_phase = real(state,real32)/2147483647.0_real32 - 0.5_real32
          state = modulo(48271_int64*state,2147483647_int64)
          quadrature = real(state,real32)/2147483647.0_real32 - 0.5_real32
          correlations(tone,symbol) = cmplx(in_phase,quadrature,real32)
        end do
        correlations(tones(symbol),symbol) = correlations(tones(symbol),symbol) + &
             cmplx(0.55_real32,0.0_real32,real32)
      end do
      call jtty_tbcc_decode(correlations,halves,decoded,success,result)
      call require(success .and. all(decoded == payload),'coherent rung failed to decode fixed payload')
      call require(result%coherent_block_length == lengths(fixture), 'coherent decode used wrong rung')
      call require(result%accepted_hypothesis_rank == ranks(fixture), 'coherent decode candidate rank changed')
      call require(result%evaluated_rung_count == evaluated_rungs(fixture), &
           'accepted candidate triggered unnecessary confirmation or fallback')
      call require(.not.result%used_half_symbol_observation, 'coherent decode fell through to M2')
    end do
  end subroutine expect_coherent_decodes

  subroutine make_noiseless_correlations(symbols, values)
    integer(int32), intent(in) :: symbols(TOTAL_K)
    complex(real32), intent(out) :: values(0:3, TOTAL_K)
    integer(int32) :: symbol

    values = cmplx(0.0_real32, 0.0_real32, real32)
    do symbol = 1, TOTAL_K
      values(symbols(symbol), symbol) = cmplx(100.0_real32, 0.0_real32, real32)
    end do
  end subroutine make_noiseless_correlations

  subroutine require(condition, message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if (.not.condition) error stop message
  end subroutine require

end program test_jtty_tbcc_decoder
