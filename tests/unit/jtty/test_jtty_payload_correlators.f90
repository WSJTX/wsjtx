program test_jtty_payload_correlators
  use, intrinsic :: iso_fortran_env, only: int32, int64, real32, real64
  use jtty_payload_correlators
  implicit none

  integer(int32), parameter :: lengths(4) = [120,160,192,240]
  type(jtty_payload_correlator) :: correlator
  complex(real32), allocatable :: samples(:)
  complex(real32) :: whole(0:3,46), halves(0:3,46), expected(0:3,46)
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
      call separate_reference(samples,7,nss,segment_count,expected)
      if (segment_count == 1) then
        call require(identical(whole,expected),'shared M1 differs from separate original-order accumulation')
      else
        call require(identical(halves,expected),'shared M2 differs from separate half-symbol accumulation')
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

  subroutine separate_reference(samples,start,nss,segments,values)
    complex(real32), intent(in) :: samples(0:)
    integer, intent(in) :: start,nss,segments
    complex(real32), intent(out) :: values(0:3,46)
    real(real32) :: reference_real(0:nss-1),reference_imaginary(0:nss-1)
    real(real32) :: phase,step,sum_real,sum_imaginary,sample_real,sample_imaginary
    real(real64) :: energy
    integer :: tone,offset,symbol,segment,first,segment_length

    segment_length = nss/segments
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
        do segment = 0, segments-1
          sum_real = 0.0_real32
          sum_imaginary = 0.0_real32
          do offset = segment*segment_length, (segment+1)*segment_length-1
            sample_real = real(samples(first+offset),real32)
            sample_imaginary = aimag(samples(first+offset))
            sum_real = sum_real + reference_real(offset)*sample_real + &
                 reference_imaginary(offset)*sample_imaginary
            sum_imaginary = sum_imaginary + reference_real(offset)*sample_imaginary - &
                 reference_imaginary(offset)*sample_real
          end do
          energy = energy + real(sum_real*sum_real + sum_imaginary*sum_imaginary,real64)
        end do
        if (segments == 1) then
          values(tone,symbol) = cmplx(sum_real,sum_imaginary,real32)
        else
          values(tone,symbol) = cmplx(real(sqrt(max(0.0_real64,energy)),real32),0.0_real32,real32)
        end if
      end do
    end do
  end subroutine separate_reference

  logical function identical(left,right)
    complex(real32), intent(in) :: left(:,:),right(:,:)
    identical = all(transfer(left,[0_int64],size(left)) == transfer(right,[0_int64],size(right)))
  end function identical

  subroutine require(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message
    if (.not.condition) error stop message
  end subroutine require
end program test_jtty_payload_correlators
