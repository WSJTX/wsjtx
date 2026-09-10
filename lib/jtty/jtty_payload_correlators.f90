module jtty_payload_correlators
  use, intrinsic :: iso_fortran_env, only: int32, real32, real64
  implicit none
  private

  real(real32), parameter :: TWO_PI = 6.2831853071795864769_real32

  type, public :: jtty_payload_correlator
    private
    integer(int32) :: samples_per_symbol = 0_int32
    real(real32), allocatable :: reference_real(:,:)
    real(real32), allocatable :: reference_imaginary(:,:)
  end type jtty_payload_correlator

  public :: jtty_payload_correlator_prepare
  public :: jtty_correlate_payload_symbols

contains

  subroutine jtty_payload_correlator_prepare(correlator, samples_per_symbol)
    type(jtty_payload_correlator), intent(inout) :: correlator
    integer(int32), intent(in) :: samples_per_symbol
    real(real32) :: phase, phase_step
    integer :: sample, tone

    if (samples_per_symbol <= 0_int32 .or. modulo(samples_per_symbol,2_int32) /= 0_int32) &
         error stop 'invalid JTTY payload symbol length'
    if (correlator%samples_per_symbol == samples_per_symbol .and. &
         allocated(correlator%reference_real) .and. &
         allocated(correlator%reference_imaginary)) return

    correlator%samples_per_symbol = samples_per_symbol
    if (allocated(correlator%reference_real)) deallocate(correlator%reference_real)
    if (allocated(correlator%reference_imaginary)) &
         deallocate(correlator%reference_imaginary)
    allocate(correlator%reference_real(0:3,0:samples_per_symbol-1))
    allocate(correlator%reference_imaginary(0:3,0:samples_per_symbol-1))

    do tone = 0, 3
      phase = 0.0_real32
      phase_step = TWO_PI*real(tone,real32)/real(samples_per_symbol,real32)
      do sample = 0, samples_per_symbol - 1
        correlator%reference_real(tone,sample) = cos(phase)
        correlator%reference_imaginary(tone,sample) = sin(phase)
        phase = phase + phase_step
      end do
    end do
  end subroutine jtty_payload_correlator_prepare

  subroutine jtty_correlate_payload_symbols(correlator, samples, payload_start, &
       correlations, half_correlations)
    ! Samples are candidate-centered analytic baseband; payload_start is zero-based.
    type(jtty_payload_correlator), intent(in) :: correlator
    complex(real32), intent(in) :: samples(0:)
    integer(int32), intent(in) :: payload_start
    complex(real32), intent(out) :: correlations(0:,:), half_correlations(0:,:)
    real(real32) :: sample_real, sample_imaginary
    real(real32) :: full_real(0:3), full_imaginary(0:3)
    real(real32) :: half_real(0:3), half_imaginary(0:3)
    real(real32) :: product_rr(0:3), product_ii(0:3), product_ri(0:3), product_ir(0:3)
    real(real64) :: energy(0:3)
    integer :: available_symbols, first, offset, segment, half_length, symbol, tone

    if (.not.allocated(correlator%reference_real) .or. &
         correlator%samples_per_symbol <= 0_int32) &
         error stop 'unprepared JTTY payload correlator'
    if (size(correlations,1) /= 4 .or. any(shape(correlations) /= shape(half_correlations))) &
         error stop 'invalid JTTY payload correlation shape'
    if (payload_start < 0_int32) error stop 'invalid JTTY payload offset'
    correlations = cmplx(0.0_real32,0.0_real32,real32)
    half_correlations = cmplx(0.0_real32,0.0_real32,real32)
    if (payload_start >= size(samples)) return
    available_symbols = min(size(correlations,2), &
         (size(samples)-payload_start)/correlator%samples_per_symbol)
    half_length = correlator%samples_per_symbol/2

    do symbol = 1, available_symbols
      first = payload_start + (symbol - 1)*correlator%samples_per_symbol
      full_real = 0.0_real32
      full_imaginary = 0.0_real32
      energy = 0.0_real64
      do segment = 0, 1
        half_real = 0.0_real32
        half_imaginary = 0.0_real32
        do offset = segment*half_length, (segment+1)*half_length-1
          sample_real = real(samples(first+offset),real32)
          sample_imaginary = aimag(samples(first+offset))
          product_rr = correlator%reference_real(:,offset)*sample_real
          product_ii = correlator%reference_imaginary(:,offset)*sample_imaginary
          product_ri = correlator%reference_real(:,offset)*sample_imaginary
          product_ir = correlator%reference_imaginary(:,offset)*sample_real
          ! Preserve each observation's accumulation order while sharing products.
          full_real = full_real + product_rr + product_ii
          full_imaginary = full_imaginary + product_ri - product_ir
          half_real = half_real + product_rr + product_ii
          half_imaginary = half_imaginary + product_ri - product_ir
        end do
        energy = energy + real(half_real*half_real + half_imaginary*half_imaginary,real64)
      end do
      do tone = 0, 3
        correlations(tone,symbol) = cmplx(full_real(tone),full_imaginary(tone),real32)
        half_correlations(tone,symbol) = &
             cmplx(real(sqrt(max(0.0_real64,energy(tone))),real32),0.0_real32,real32)
      end do
    end do
  end subroutine jtty_correlate_payload_symbols

end module jtty_payload_correlators
