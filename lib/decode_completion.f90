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
  public :: set_progress_receiver
  public :: set_packed_pass_receiver
  public :: report_packed_pass

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

  ! Called, when set, with each report's generation.
  abstract interface
     subroutine progress_receiver(generation)
       integer, intent(in) :: generation
     end subroutine progress_receiver
  end interface
  procedure(progress_receiver), pointer, save :: receive_progress => null()

  ! Called, when set, as the OSD decoder enters (entering .true.) and leaves
  ! (.false.) its packed pair-table pass.  Does nothing unless a receiver is
  ! set, which must happen before any decoding thread starts.
  abstract interface
     subroutine packed_pass_receiver(entering)
       logical, intent(in) :: entering
     end subroutine packed_pass_receiver
  end interface
  procedure(packed_pass_receiver), pointer, save :: receive_packed_pass => null()

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
    if (associated(receive_progress)) call receive_progress(generation)
  end subroutine write_decode_progress

  subroutine set_progress_receiver(receiver)
    procedure(progress_receiver) :: receiver
    receive_progress => receiver
  end subroutine set_progress_receiver

  subroutine set_packed_pass_receiver(receiver)
    procedure(packed_pass_receiver) :: receiver
    receive_packed_pass => receiver
  end subroutine set_packed_pass_receiver

  subroutine report_packed_pass(entering)
    logical, intent(in) :: entering
    if (associated(receive_packed_pass)) call receive_packed_pass(entering)
  end subroutine report_packed_pass

end module decode_completion_module
