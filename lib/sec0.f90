subroutine sec0(n,t)

  ! Simple execution timer.
  ! call sec0(0,t)
  ! ... statements to be timed ...
  ! call sec0(1,t)
  ! print*,'Execution time:',t

  integer*8 count0,count1,clkfreq
  save count0

  call system_clock(count1,clkfreq)
  if(n.eq.0) then
     count0=count1
     return
  else
     t=float(count1-count0)/float(clkfreq)
  endif

  return
end subroutine sec0
