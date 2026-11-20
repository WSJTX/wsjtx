program test_jtty_payload_correlators
  use, intrinsic :: iso_fortran_env, only: int32, real32, real64
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use jtty_payload_correlators
  implicit none

  integer(int32), parameter :: lengths(4) = [120,160,192,240]
  type(jtty_payload_correlator) :: correlator
  complex(real32), allocatable :: samples(:)
  complex(real32) :: whole(0:3,46), halves(0:3,46)
  complex(real64) :: expected(0:3,46)
  real(real64) :: error_bound(0:3,46)
  integer :: length_index, nss, offset, segment_count

  do length_index = 1, size(lengths)
    nss = lengths(length_index)
    allocate(samples(0:7+46*nss-1))
    do offset = 0, size(samples)-1
      samples(offset) = cmplx(real(modulo(offset*17,113)-56,real32)/19.0_real32, &
           real(modulo(offset*31,127)-63,real32)/23.0_real32,real32)
    end do
    call jtty_payload_correlator_prepare(correlator,int(nss,int32))
    call jtty_correlate_payload_symbols(correlator,samples,7_int32,whole,halves)
    do segment_count = 1, 2
      call separate_reference(samples,7,nss,segment_count,expected,error_bound)
      if (segment_count == 1) then
        call require(numerically_agree(whole,expected,error_bound),'M1 exceeds real32 accumulation error bound')
      else
        call require(numerically_agree(halves,expected,error_bound),'M2 exceeds real32 accumulation error bound')
      end if
    end do

    samples = cmplx(0.0_real32,0.0_real32,real32)
    samples(7:7+nss/2-1) = cmplx(1.0_real32,0.0_real32,real32)
    samples(7+nss/2:7+nss-1) = cmplx(-1.0_real32,0.0_real32,real32)
    call jtty_correlate_payload_symbols(correlator,samples,7_int32,whole,halves)
    call require(abs(whole(0,1)) == 0.0_real32,'opposite halves did not cancel in M1')
    call require(abs(real(halves(0,1))-real(nss,real32)/sqrt(2.0_real32)) < 0.0001_real32, &
         'M2 failed to retain opposite-phase half energy')
    call require(all(abs(whole(:,2:)) == 0.0_real32) .and. &
         all(abs(halves(:,2:)) == 0.0_real32),'payload symbol windows overlap')

    samples = cmplx(1.0_real32,0.0_real32,real32)
    call jtty_correlate_payload_symbols(correlator,samples(0:7+2*nss-2),7_int32,whole,halves)
    call require(abs(whole(0,1)) == real(nss,real32),'complete payload symbol was lost')
    call require(all(abs(whole(:,2:)) == 0.0_real32) .and. &
         all(abs(halves(:,2:)) == 0.0_real32),'partial symbol was decoded or left stale data')
    call jtty_correlate_payload_symbols(correlator,samples,int(size(samples),int32),whole,halves)
    call require(all(abs(whole) == 0.0_real32) .and. all(abs(halves) == 0.0_real32), &
         'out-of-range payload left stale observations')
    deallocate(samples)
  end do
  print *, 'test_jtty_payload_correlators: all checks passed'

contains

  subroutine separate_reference(samples,start,nss,segments,values,error_bound)
    complex(real32), intent(in) :: samples(0:)
    integer, intent(in) :: start,nss,segments
    complex(real64), intent(out) :: values(0:3,46)
    real(real64), intent(out) :: error_bound(0:3,46)
    real(real32) :: reference_real(0:nss-1),reference_imaginary(0:nss-1)
    real(real32) :: phase,step
    real(real64) :: sum_real,sum_imaginary,sample_real,sample_imaginary,energy
    real(real64) :: rr,ii,ri,ir,absolute_real,absolute_imaginary,squared_error,gamma
    real(real64), parameter :: unit_roundoff = real(epsilon(0.0_real32),real64)/2.0_real64
    integer :: tone,offset,symbol,segment,first,segment_length

    segment_length = nss/segments
    ! Each component sums 2*m rounded products; gamma bounds error by their
    ! absolute sum, including when the correlation nearly cancels.
    gamma = real(2*segment_length+2,real64)*unit_roundoff
    gamma = gamma/(1.0_real64-gamma)
    do tone = 0, 3
      phase = 0.0_real32
      step = 6.2831853071795864769_real32*real(tone,real32)/real(nss,real32)
      do offset = 0, nss-1
        reference_real(offset) = cos(phase)
        reference_imaginary(offset) = sin(phase)
        phase = phase + step
      end do
      do symbol = 1, 46
        first = start + (symbol-1)*nss
        energy = 0.0_real64
        squared_error = 0.0_real64
        do segment = 0, segments-1
          sum_real = 0.0_real64
          sum_imaginary = 0.0_real64
          absolute_real = 0.0_real64
          absolute_imaginary = 0.0_real64
          do offset = segment*segment_length, (segment+1)*segment_length-1
            sample_real = real(samples(first+offset),real64)
            sample_imaginary = real(aimag(samples(first+offset)),real64)
            rr = real(reference_real(offset),real64)*sample_real
            ii = real(reference_imaginary(offset),real64)*sample_imaginary
            ri = real(reference_real(offset),real64)*sample_imaginary
            ir = real(reference_imaginary(offset),real64)*sample_real
            sum_real = sum_real + rr + ii
            sum_imaginary = sum_imaginary + ri - ir
            absolute_real = absolute_real + abs(rr) + abs(ii)
            absolute_imaginary = absolute_imaginary + abs(ri) + abs(ir)
          end do
          energy = energy + sum_real*sum_real + sum_imaginary*sum_imaginary
          squared_error = squared_error + (gamma*absolute_real)**2 + (gamma*absolute_imaginary)**2
        end do
        if (segments == 1) then
          values(tone,symbol) = cmplx(sum_real,sum_imaginary,real64)
          error_bound(tone,symbol) = sqrt(squared_error)
        else
          values(tone,symbol) = cmplx(sqrt(energy),0.0_real64,real64)
          ! The norm propagates segment errors without amplification; allow
          ! rounding in real32 squares, their sum, and the final real32 value.
          error_bound(tone,symbol) = sqrt(squared_error) + &
               4.0_real64*unit_roundoff*(sqrt(energy)+sqrt(squared_error))
        end if
      end do
    end do
  end subroutine separate_reference

  logical function numerically_agree(actual,expected,error_bound) result(agree)
    complex(real32), intent(in) :: actual(:,:)
    complex(real64), intent(in) :: expected(:,:)
    real(real64), intent(in) :: error_bound(:,:)

    agree = .false.
    if (.not.all(ieee_is_finite(real(actual))) .or. .not.all(ieee_is_finite(aimag(actual)))) return
    if (.not.all(ieee_is_finite(real(expected))) .or. .not.all(ieee_is_finite(aimag(expected)))) return
    if (.not.all(ieee_is_finite(error_bound))) return
    agree = all(abs(cmplx(actual,kind=real64)-expected) <= error_bound)
  end function numerically_agree

  subroutine require(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message
    if (.not.condition) error stop message
  end subroutine require
end program test_jtty_payload_correlators
