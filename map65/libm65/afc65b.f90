module afc65b_mod
  implicit none
contains  

subroutine afc65b(cx,cy,npts,fsample,nflip,ipol,xpol,ndphi,a,ccfbest,dtbest)
  use fchisq_mod
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  
  implicit none

  integer, parameter :: MAX_STEP_TRIES     = 200
  integer, parameter :: MAX_DOWNHILL_STEPS = 200
  real, parameter :: MIN_STEP = 1.0e-4
  real, parameter :: OBJECTIVE_REL_TOL = 1.0e-6

  logical, intent(in) :: xpol
  integer, intent(in) :: npts, nflip, ndphi
  integer, intent(inout) :: ipol
  complex, intent(in) :: cx(npts), cy(npts)
  real, intent(inout) :: a(5)
  real               :: deltaa(5)
  integer            :: iter, j, nterms, step, downhill_steps
  real, intent(out)  :: ccfbest, dtbest
  real               :: chisq1, chisq2, chisqr, chisqr0
  real               :: delta, dtmax
  real, intent(in)   :: fsample
  real               :: ccfmax
  real               :: x0, xlast, xprev, xbest, xnext, candidate
  real               :: flast, fprev, fbest, fnext, fcandidate, curvature, tolerance
  logical            :: bracketed, improved, have_previous

  ! Initial parameter guesses
  a(1) = 0.0
  a(2) = 0.0
  a(3) = 0.0
  if (ipol < 1 .or. ipol > 4) ipol = 1
  a(4) = 45.0*(ipol - 1.0)

  deltaa(1) = 2.0
  deltaa(2) = 2.0
  deltaa(3) = 2.0
  deltaa(4) = 22.5
  deltaa(5) = 0.05

  nterms = 3
  if (xpol) nterms = 4
  if (ndphi /= 0) nterms = 3   ! don't fit pol when solving for dphi

  ccfbest = 0.0
  dtbest  = 0.0
  chisqr0 = 0.0
  have_previous = .false.

  if (npts < 1 .or. fsample <= 0.0 .or. .not. ieee_is_finite(fsample)) return
  
  do iter = 1, 3
     do j = 1, nterms
        x0 = a(j)
        chisq1 = fchisq(cx,cy,npts,fsample,nflip,a,ccfmax,dtmax)
        if (.not. ieee_is_finite(chisq1)) then
           a(j) = x0
           cycle
        endif

        delta = abs(deltaa(j))
        if (.not. ieee_is_finite(delta) .or. delta < MIN_STEP) delta = MIN_STEP
        improved = .false.
        xlast = x0
        flast = chisq1
        xnext = xlast

        do step = 1, MAX_STEP_TRIES
           xnext = xlast + delta
           a(j) = xnext
           chisq2 = fchisq(cx,cy,npts,fsample,nflip,a,ccfmax,dtmax)
           if (.not. ieee_is_finite(chisq2)) exit
           tolerance = OBJECTIVE_REL_TOL * max(1.0, abs(chisq1), abs(chisq2))
           if (abs(chisq2 - chisq1) > tolerance) then
              xprev = xlast
              fprev = flast
              improved = .true.
              exit
           endif
           xlast = xnext
           flast = chisq2
        end do

        if (.not. improved) then
           a(j) = x0
           deltaa(j) = max(MIN_STEP, 0.5*delta)
           cycle
        endif

        if (chisq2 > chisq1) then
           delta = -delta
           xnext = x0 + delta
           a(j) = xnext
           chisq2 = fchisq(cx,cy,npts,fsample,nflip,a,ccfmax,dtmax)
           tolerance = OBJECTIVE_REL_TOL * max(1.0, abs(chisq1), abs(chisq2))
           if (.not. ieee_is_finite(chisq2) .or. chisq2 >= chisq1 - tolerance) then
              a(j) = x0
              deltaa(j) = max(MIN_STEP, 0.5*abs(delta))
              cycle
           endif
           xprev = x0
           fprev = chisq1
        endif

        xbest = xnext
        fbest = chisq2
        downhill_steps = 0
        bracketed = .false.

        do step = 1, MAX_DOWNHILL_STEPS
           xnext = xbest + delta
           a(j) = xnext
           fnext = fchisq(cx,cy,npts,fsample,nflip,a,ccfmax,dtmax)
           if (.not. ieee_is_finite(fnext)) then
              a(j) = xbest
              exit
           endif

           tolerance = OBJECTIVE_REL_TOL * max(1.0, abs(fbest), abs(fnext))
           if (fnext < fbest - tolerance) then
              xprev = xbest
              fprev = fbest
              xbest = xnext
              fbest = fnext
              downhill_steps = downhill_steps + 1
           else
              a(j) = xbest
              bracketed = .true.
              exit
           endif
        end do

        if (bracketed) then
           curvature = fprev - 2.0*fbest + fnext
           tolerance = OBJECTIVE_REL_TOL * max(1.0, abs(fprev), abs(fbest), abs(fnext))
           if (curvature > tolerance) then
              candidate = xbest + 0.5*delta*(fprev - fnext)/curvature
              if (ieee_is_finite(candidate) .and. candidate > min(xprev, xnext) .and. &
                   candidate < max(xprev, xnext)) then
                 a(j) = candidate
                 fcandidate = fchisq(cx,cy,npts,fsample,nflip,a,ccfmax,dtmax)
                 if (ieee_is_finite(fcandidate)) then
                    tolerance = OBJECTIVE_REL_TOL * max(1.0, abs(fbest), abs(fcandidate))
                    if (fcandidate < fbest - tolerance) then
                       xbest = candidate
                       fbest = fcandidate
                    endif
                 endif
              endif
           endif
        endif

        a(j) = xbest
        if (downhill_steps < 3) then
           deltaa(j) = max(MIN_STEP, abs(delta)*max(1, downhill_steps)/3.0)
        else
           deltaa(j) = abs(delta)
        endif
     enddo

     chisqr = fchisq(cx,cy,npts,fsample,nflip,a,ccfmax,dtmax)
     if (.not. ieee_is_finite(chisqr)) exit
     if (have_previous) then
        tolerance = OBJECTIVE_REL_TOL * max(1.0, abs(chisqr), abs(chisqr0))
        if (abs(chisqr - chisqr0) <= tolerance) exit
     endif
     chisqr0 = chisqr
     have_previous = .true.
  enddo

  if (ieee_is_finite(ccfmax)) ccfbest = max(0.0, ccfmax * (1378.125/fsample)**2)
  if (ieee_is_finite(dtmax)) dtbest = dtmax
  if (.not. ieee_is_finite(ccfbest)) ccfbest = 0.0
  if (.not. ieee_is_finite(dtbest)) dtbest = 0.0

  if (.not. ieee_is_finite(a(4))) a(4) = 0.0
  a(4) = modulo(a(4), 180.0)

  ipol = modulo(nint(a(4)/45.0), 4) + 1
  
end subroutine afc65b

end module afc65b_mod
