module afc65b_mod
  implicit none
contains  

subroutine afc65b(cx,cy,npts,fsample,nflip,ipol,xpol,ndphi,a,ccfbest,dtbest)
  use debug_log
  use fchisq_mod
  
  implicit none
 
  logical            :: xpol
  integer            :: npts, nflip, ipol, ndphi
  complex            :: cx(npts), cy(npts)
  real               :: a(5), deltaa(5)
  integer            :: iter, j, nterms
  real               :: ccfbest, dtbest
  real               :: chisq1, chisq2, chisq3, chisqr, chisqr0
  real               :: delta, dtmax, fn, tmp
  real               :: fsample, ccfmax

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

  ! Don't fit polarization when solving for dphi
  if (ndphi /= 0) nterms = 3

  ! Start the iteration
  chisqr  = 0.0
  chisqr0 = 1.0e6
  
  do iter = 1, 3                      ! One iteration is enough?
     do j = 1, nterms
        chisq1 = fchisq(cx,cy,npts,fsample,nflip,a,ccfmax,dtmax)
        fn     = 0.0
        delta  = deltaa(j)

        ! Find first step where chisq changes
        do
           a(j)   = a(j) + delta
           chisq2 = fchisq(cx,cy,npts,fsample,nflip,a,ccfmax,dtmax)
           if (chisq2 /= chisq1) exit
        end do

        ! If we stepped uphill, reverse direction and swap labels
        if (chisq2 > chisq1) then
           delta  = -delta
           a(j)   = a(j) + delta
           tmp    = chisq1
           chisq1 = chisq2
           chisq2 = tmp
        endif

        ! Walk while chisq keeps improving
        do
           fn    = fn + 1.0
           a(j)  = a(j) + delta
           chisq3 = fchisq(cx,cy,npts,fsample,nflip,a,ccfmax,dtmax)
           if (chisq3 < chisq2) then
              chisq1 = chisq2
              chisq2 = chisq3
           else
              exit
           endif
        end do

        ! Guard against division by zero in flat regions
        if (chisq3 == chisq2) cycle

        ! Find minimum of parabola defined by last three points
        delta     = delta*(1.0/(1.0 + (chisq1 - chisq2)/(chisq3 - chisq2)) + 0.5)
        a(j)      = a(j) - delta
        deltaa(j) = deltaa(j)*fn/3.0
     enddo

     chisqr = fchisq(cx,cy,npts,fsample,nflip,a,ccfmax,dtmax)
     if (chisqr/chisqr0 > 0.9999) exit
     chisqr0 = chisqr
  enddo

  ccfbest = ccfmax * (1378.125/fsample)**2
  dtbest  = dtmax

  ! Wrap polarization angle into [0,180)
  if (a(4) < 0.0)   a(4) = a(4) + 180.0
  if (a(4) >= 180.) a(4) = a(4) - 180.0
  if (nint(a(4)) == 180) a(4) = 0.0

  if (a(4) < 0.0 .or. a(4) >= 180.0) then
     ! print *, 'afc65b: bad a(4) after wrap, set to 0. was ', a(4)
     a(4) = 0.0
  endif

  ipol = nint(a(4)/45.0) + 1
  if (ipol > 4) ipol = ipol - 4
  
end subroutine afc65b

end module afc65b_mod
