module decode_completion_module
  implicit none

  private
  public :: decode_completion_result
  public :: reset_decode_completion
  public :: set_decode_completion
  public :: write_decode_completion

  type :: decode_completion_result
     logical :: available = .false.
     integer :: synchronized = 0
     integer :: decoded = 0
     integer :: average = 0
  end type decode_completion_result

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

end module decode_completion_module
