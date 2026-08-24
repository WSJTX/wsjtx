module decode_completion_module
  use iso_fortran_env, only: int64
  use decoder_ipc_atomic, only: decoder_ipc_progress_report
  implicit none

  private
  public :: decode_completion_result
  public :: reset_decode_completion
  public :: set_decode_completion
  public :: write_decode_completion
  public :: write_decode_progress

  type :: decode_completion_result
     logical :: available = .false.
     integer :: synchronized = 0
     integer :: decoded = 0
     integer :: average = 0
  end type decode_completion_result

  integer, save :: progress_thread_generation = 0
  integer, save :: progress_thread_calls = 0
  integer(int64), save :: progress_thread_count = 0_int64
  integer(int64), save :: progress_thread_rate = 0_int64
!$omp threadprivate(progress_thread_generation, progress_thread_calls, &
!$omp& progress_thread_count, progress_thread_rate)

contains

  subroutine reset_decode_completion(completion)
    type(decode_completion_result), intent(out) :: completion

    completion%available = .false.
    completion%synchronized = 0
    completion%decoded = 0
    completion%average = 0
  end subroutine reset_decode_completion

  subroutine set_decode_completion(completion, synchronized, decoded, average)
    type(decode_completion_result), intent(out) :: completion
    integer, intent(in) :: synchronized, decoded, average

    completion%available = .true.
    completion%synchronized = synchronized
    completion%decoded = decoded
    completion%average = average
  end subroutine set_decode_completion

  subroutine write_decode_completion(completion, generation)
    type(decode_completion_result), intent(in) :: completion
    integer, intent(in), optional :: generation

    if (.not. completion%available) return
    if (present(generation)) then
       write(*,1001) completion%synchronized, completion%decoded, &
            completion%average, generation
1001   format('<DecodeFinished>',2i4,i9,' gen=',i0)
    else
       write(*,1000) completion%synchronized, completion%decoded, &
            completion%average
1000   format('<DecodeFinished>',2i4,i9)
    end if
  end subroutine write_decode_completion

  subroutine write_decode_progress(generation)
    integer, intent(in) :: generation
    integer(int64) :: count, count_max, elapsed, rate

    if (generation <= 0) return
    if (generation == progress_thread_generation) then
       progress_thread_calls = progress_thread_calls + 1
       if (progress_thread_calls < 32) return
    end if
    progress_thread_calls = 0
    call system_clock(count, rate, count_max)
    if (generation == progress_thread_generation .and. &
         rate == progress_thread_rate .and. rate > 0_int64) then
       if (count >= progress_thread_count) then
          elapsed = count - progress_thread_count
       else
          elapsed = count_max - progress_thread_count + count + 1_int64
       end if
       if (elapsed < max(1_int64, rate)) return
    end if
    progress_thread_generation = generation
    progress_thread_count = count
    progress_thread_rate = rate
    call decoder_ipc_progress_report(generation)
  end subroutine write_decode_progress

end module decode_completion_module
