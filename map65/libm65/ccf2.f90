module ccf2_mod
  implicit none
contains

subroutine ccf2(ss, nz, nflip, ccfbest, lagpk)
  implicit none

  !==== Arguments ============================================================
  integer, intent(in)  :: nz
  real,    intent(in)  :: ss(nz)
  integer, intent(in)  :: nflip
  real,    intent(out) :: ccfbest
  integer, intent(out) :: lagpk

  !==== Parameters ===========================================================
  integer, parameter :: LAGMAX = 200

  !==== Local variables ======================================================
  real    :: ccf(-LAGMAX:LAGMAX)
  real    :: s0, s1, x
  integer :: npr(126)
  integer :: i, j, lag, lag1, lag2
    
  !==== JT65 pseudo-random sync pattern =====================================
  data npr / &
    1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0, &
    0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1, &
    0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1, &
    0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1, &
    1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1, &
    0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1, &
    1,1,1,1,1,1 /

  !==== Initialize outputs ===================================================
  ccfbest = 0.0
  lagpk   = 0

  !==== Main correlation loop ===============================================
  lag1 = -LAGMAX
  lag2 =  LAGMAX

  do lag = lag1, lag2
     s0 = 0.0
     s1 = 0.0

     do i = 1, 126
        j = 2*(8*i + 43) + lag
        if (j >= 1 .and. j <= nz-8) then
           x = ss(j) + ss(j+8)
           if (npr(i) == 0) then
              s0 = s0 + x
           else
              s1 = s1 + x
           end if
        end if
     end do

     ccf(lag) = nflip * (s1 - s0)

     if (ccf(lag) > ccfbest) then
        ccfbest = ccf(lag)
        lagpk   = lag
     end if
  end do

end subroutine ccf2

end module ccf2_mod
