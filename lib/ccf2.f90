subroutine ccf2(ss,nz,nflip,ccfbest,xlagpk)

!  parameter (LAGMIN=-86,LAGMAX=258)
  parameter (LAGMIN=-112,LAGMAX=258)  ! Look for DT from -3.6s to +5.0s
  real ss(nz)
  real ccf(-LAGMAX:LAGMAX)
  integer npr(126)

! The JT65 pseudo-random sync pattern:
  data npr/                                    &
      1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0, &
      0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1, &
      0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1, &
      0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1, &
      1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1, &
      0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1, &
      1,1,1,1,1,1/
  save

  ccfbest=0.
  lag1=LAGMIN
  lag2=LAGMAX
  do lag=lag1,lag2
     s0=0.
     s1=0.
     do i=1,126
        j=16*(i-1)+1 + lag
        if(j.ge.1 .and. j.le.nz-8) then
           x=ss(j)
           if(npr(i).eq.0) then
              s0=s0 + x
           else
              s1=s1 + x
           endif
        endif
     enddo
     ccf(lag)=nflip*(s1-s0)
     if(ccf(lag).gt.ccfbest) then
        ccfbest=ccf(lag)
        lagpk=lag
        xlagpk=lagpk
     endif
  enddo
  if( lagpk.gt.-LAGMAX .and. lagpk.lt.LAGMAX) then
     call peakup(ccf(lagpk-1),ccf(lagpk),ccf(lagpk+1),dx)
     xlagpk=lagpk+dx
  endif
  return
end subroutine ccf2
