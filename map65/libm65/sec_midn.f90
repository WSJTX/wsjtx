module sec_midn_mod
  implicit none
contains

  real function sec_midn()
    implicit none
    integer :: values(8)

    call date_and_time(values=values)

    ! values(5) = hour
    ! values(6) = minute
    ! values(7) = second
    ! values(8) = milliseconds

    sec_midn = values(5)*3600.0 + values(6)*60.0 + values(7) + values(8)/1000.0
  end function sec_midn

end module sec_midn_mod

