subroutine badmsg(irc,dat,nc1,nc2,ng2)

! Get rid of a few QRA64 false decodes that cannot be correct messages.  

  integer dat(12)                           !Decoded message (as 12 integers)

  ic1=ishft(dat(1),22) + ishft(dat(2),16) + ishft(dat(3),10)+         &
       ishft(dat(4),4) + iand(ishft(dat(5),-2),15)

! Test for "......" or "CQ 000"
  if(ic1.eq.262177560 .or. ic1.eq.262177563) then
     irc=-1
     return
  endif

  ic2=ishft(iand(dat(5),3),26) + ishft(dat(6),20) +                   &
       ishft(dat(7),14) + ishft(dat(8),8) + ishft(dat(9),2) +         &
       iand(ishft(dat(10),-4),3)

  ig=ishft(iand(dat(10),15),12) + ishft(dat(11),6) + dat(12)
  
! Test for blank, -01 to -30, R-01 to R-30, RO, RRR, 73
  if(ig.ge.32401 .and. ig.le.32464) return

  if(ig.ge.14220 .and. ig.le.14229) return  !-41 to -50
  if(ig.ge.14040 .and. ig.le.14049) return  !-31 to -40

  if(ig.ge.13320 .and. ig.le.13329) return  !+00 to +09
  if(ig.ge.13140 .and. ig.le.13149) return  !+10 to +19
  if(ig.ge.12960 .and. ig.le.12969) return  !+20 to +29
  if(ig.ge.12780 .and. ig.le.12789) return  !+30 to +39
  if(ig.ge.12600 .and. ig.le.12609) return  !+40 to +49

  if(ig.ge.12420 .and. ig.le.12429) return  !R-41 to R-50
  if(ig.ge.12240 .and. ig.le.12249) return  !R-31 to R-40

  if(ig.ge.11520 .and. ig.le.11529) return  !R+00 to R+09
  if(ig.ge.11340 .and. ig.le.11349) return  !R+10 to R+19
  if(ig.ge.11160 .and. ig.le.11169) return  !R+20 to R+29
  if(ig.ge.10980 .and. ig.le.10989) return  !R+30 to R+39
  if(ig.ge.10800 .and. ig.le.10809) return  !R+40 to R+49

  if(ic1.eq.nc1 .and. ic2.eq.nc2 .and. ng2.ne.32401 .and. ig.ne.ng2) irc=-1

  return
end subroutine badmsg
