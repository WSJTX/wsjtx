module sleep_msec_mod
  implicit none

  interface
     subroutine usleep(usec)
       ! Fortran-only interface to the C function usleep_
       integer(kind=8), intent(in) :: usec
     end subroutine usleep
  end interface

contains

  subroutine sleep_msec(n)
    integer, intent(in) :: n
    integer(kind=8) :: usec

    usec = int(n, kind=8) * 1000_8
    call usleep(usec)
  end subroutine sleep_msec

end module sleep_msec_mod

