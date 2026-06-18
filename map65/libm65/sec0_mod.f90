module sec0_mod
  use sec_midn_mod
  implicit none
contains

  subroutine sec0(mode, t)
    integer, intent(in) :: mode
    real, intent(out) :: t
    real, save :: t0 = 0.0

    if (mode == 1) then
       t0 = sec_midn()
       t = t0
    else
       t = sec_midn() - t0
    end if
  end subroutine sec0

end module sec0_mod
