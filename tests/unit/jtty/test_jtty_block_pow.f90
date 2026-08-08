program test_jtty_block_pow

  ! Focused tests for coherent block detection (lib/jtty/jtty_block_pow.f90),
  ! used as a fallback by jtty_mdecode.f90's decode_and_merge when ordinary
  ! blocksize-1 (single-symbol) detection fails to decode a candidate.

  use, intrinsic :: iso_fortran_env, only: real32, int32
  use jtty_fec   ! re-exports tbcc's public API: TOTAL_K, PAYLOAD_BITS,
                 ! tbcc_init, tbcc_encode, tbcc_wava_fsk_decode
  implicit none

  call expect_block_roundtrip_and_phase_invariance()
  call expect_block_detection_rescues_a_misleading_symbol()
  call expect_clean_signal_unaffected_by_block_detection()

  print *, 'test_jtty_block_pow: all checks passed'

contains

  subroutine expect_block_roundtrip_and_phase_invariance()
    ! Noiseless round trip through jtty_block_pow at iblock=2 and iblock=4,
    ! then through tbcc_wava_fsk_decode, at an arbitrary global carrier
    ! phase -- block detection must not need to know absolute phase, only
    ! stay coherent within each short block, so the result must not depend
    ! on this offset.
    integer(int32) :: payload(PAYLOAD_BITS), decoded(PAYLOAD_BITS)
    integer(int32) :: tone_symbols(TOTAL_K)
    complex(real32) :: z(0:3, TOTAL_K), phase
    real(real32)   :: pow_block(0:3, TOTAL_K)
    logical        :: success
    integer        :: i, t, iblk

    call tbcc_init(9)

    do i = 1, PAYLOAD_BITS
       payload(i) = mod(i, 2)
    end do
    payload(JTTY_RESERVED_BIT) = 0   ! reserved_zero_bit requires this below
    call tbcc_encode(payload, tone_symbols)

    phase = cmplx(cos(0.7_real32), sin(0.7_real32), real32)   ! arbitrary global carrier phase
    z = cmplx(0.0_real32, 0.0_real32, real32)
    do t = 1, TOTAL_K
       z(tone_symbols(t), t) = phase
    end do

    do iblk = 2, 4, 2
       call jtty_block_pow(z, TOTAL_K, iblk, pow_block)
       call tbcc_wava_fsk_decode(pow_block, JTTY_WAVA_L, JTTY_WAVA_ITERS, &
            decoded, success, reserved_zero_bit=JTTY_RESERVED_BIT)
       if (.not. success) then
          print *, 'test_jtty_block_pow: noiseless block decode failed, iblock=', iblk
          error stop 1
       end if
       if (any(decoded /= payload)) then
          print *, 'test_jtty_block_pow: noiseless block decode mismatch, iblock=', iblk
          error stop 1
       end if
    end do
  end subroutine expect_block_roundtrip_and_phase_invariance

  subroutine expect_block_detection_rescues_a_misleading_symbol()
    ! A minimal, hand-verified 2-symbol block where blocksize-1 detection
    ! picks the WRONG tone at position 1 (tone 1's raw |z|^2 = 1.1025
    ! exceeds tone 0's |z|^2 = 1.0 there), but tone 1's value is phase-
    ! rotated 120 degrees away from the block's coherent reference (the
    ! phase shared by position 1's correct tone and position 2's tone).
    ! Coherently summing across the block must favor the true sequence
    ! (tone 0 @ position 1, tone 2 @ position 2) despite tone 1's larger
    ! single-symbol magnitude, because it doesn't add constructively:
    !   |z(0,1)+z(2,2)|^2 = |1.0+1.0|^2          = 4.0
    !   |z(1,1)+z(2,2)|^2 = |1.05*exp(i*2pi/3)+1.0|^2 ~= 1.05
    ! This is the actual mechanism block detection is meant to exploit --
    ! not merely averaging out noise, but that a misleading single-symbol
    ! reading doesn't survive being coherently combined with its neighbor.
    complex(real32) :: z(0:3, 2)
    real(real32)    :: pow_block(0:3, 2)
    real(real32)    :: twopi, pow1_correct, pow1_wrong

    twopi = 8.0_real32*atan(1.0_real32)

    z = cmplx(0.0_real32, 0.0_real32, real32)
    z(0,1) = cmplx(1.0_real32, 0.0_real32, real32)                     ! correct tone, position 1
    z(1,1) = 1.05_real32 * cmplx(cos(twopi/3.0_real32), &
                                  sin(twopi/3.0_real32), real32)        ! misleading tone, position 1
    z(2,2) = cmplx(1.0_real32, 0.0_real32, real32)                     ! correct tone, position 2

    pow1_correct = real(z(0,1)*conjg(z(0,1)))
    pow1_wrong   = real(z(1,1)*conjg(z(1,1)))
    if (.not. (pow1_wrong > pow1_correct)) then
       print *, 'test_jtty_block_pow: test setup error -- expected tone 1 to look', &
            ' stronger than tone 0 at blocksize 1'
       error stop 1
    end if

    call jtty_block_pow(z, 2, 2, pow_block)

    if (.not. (pow_block(0,1) > pow_block(1,1))) then
       print *, 'test_jtty_block_pow: block detection failed to rescue the', &
            ' misleading symbol at position 1'
       error stop 1
    end if
    if (maxloc(pow_block(:,1),dim=1)-1 /= 0) then
       print *, 'test_jtty_block_pow: block-detected tone at position 1 is not the correct one'
       error stop 1
    end if
    if (maxloc(pow_block(:,2),dim=1)-1 /= 2) then
       print *, 'test_jtty_block_pow: block-detected tone at position 2 is not the correct one'
       error stop 1
    end if
  end subroutine expect_block_detection_rescues_a_misleading_symbol

  subroutine expect_clean_signal_unaffected_by_block_detection()
    ! Non-regression: on an unambiguous, noiseless signal, blocksize-2 and
    ! blocksize-4 detection must pick exactly the same tone at every
    ! position as blocksize-1 already does -- block detection is a fallback
    ! that should never second-guess an already-clean reading.
    integer(int32) :: tone_symbols(TOTAL_K)
    complex(real32) :: z(0:3, TOTAL_K)
    real(real32)   :: pow_block(0:3, TOTAL_K)
    integer        :: i, t, iblk

    do t = 1, TOTAL_K
       tone_symbols(t) = mod(t, 4)   ! deterministic, exercises all 4 tones
    end do

    z = cmplx(0.0_real32, 0.0_real32, real32)
    do t = 1, TOTAL_K
       z(tone_symbols(t), t) = cmplx(1.0_real32, 0.0_real32, real32)
    end do

    do iblk = 2, 4, 2
       call jtty_block_pow(z, TOTAL_K, iblk, pow_block)
       do i = 1, TOTAL_K
          if (maxloc(pow_block(:,i),dim=1)-1 /= tone_symbols(i)) then
             print *, 'test_jtty_block_pow: clean-signal regression at position', i, &
                  ' iblock=', iblk
             error stop 1
          end if
       end do
    end do
  end subroutine expect_clean_signal_unaffected_by_block_detection

end program test_jtty_block_pow
