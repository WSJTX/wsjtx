module env_module
  implicit none
  integer, parameter :: max_path_length = 4096

contains
  logical function is_windows()
    implicit none
    character(len=256) :: appData
    integer :: length, status
    integer :: i

    ! Initialize the function result to false
    is_windows = .false.

    ! Retrieve the APPDATA environment variable
    call get_environment_variable('APPDATA', appData, length, status)

    ! Check if the environment variable was successfully retrieved
    if (status == 0) then
      ! Look for a backslash in the APPDATA variable
      do i = 1, length
        if (appData(i:i) == '\') then
          is_windows = .true.
          exit
        end if
      end do
    end if
  end function is_windows

  subroutine get_executable_directory(executable_directory)
    implicit none
    character(len=max_path_length), intent(out) :: executable_directory
    character(len=max_path_length) :: full_path, temp_directory
    integer :: length, status, last_sep
    integer :: i, j
    logical :: win

    ! Retrieve the command argument 0, which is the path to the executable
    call get_command_argument(0, full_path, length, status)
    if (status /= 0) then
      executable_directory = 'Unknown'
    else
      full_path = full_path(1:length)

      ! Find the last directory separator
      last_sep = 0
      do i = length, 1, -1
        if (full_path(i:i) == '/' .or. full_path(i:i) == '\') then
          last_sep = i
          exit
        end if
      end do

      ! Extract the directory part
      if (last_sep > 0) then
        executable_directory = full_path(1:last_sep-1)
      else
        executable_directory = '.'
      end if

      ! Check if the OS is not Windows and escape spaces if needed
      win = is_windows()
      if (.not. win) then
        temp_directory = ''
        j = 1
        do i = 1, len_trim(executable_directory)
          if (executable_directory(i:i) == ' ') then
            temp_directory(j:j+1) = '\ '
            j = j + 2
          else
            temp_directory(j:j) = executable_directory(i:i)
            j = j + 1
          end if
        end do
        executable_directory = temp_directory
      end if
    end if
  end subroutine get_executable_directory
end module env_module

