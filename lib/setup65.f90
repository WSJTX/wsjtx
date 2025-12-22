subroutine setup65

! Defines arrays related to the JT65 pseudo-random synchronizing pattern.
! Executed at program start.

  use jt65_mod
  integer nprc(126)

! JT65
  data nprc/                                   &
      1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0, &
      0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1, &
      0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1, &
      0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1, &
      1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1, &
      0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1, &
      1,1,1,1,1,1/
  data mr2/0/                !Silence compiler warning

! Put the appropriate pseudo-random sequence into pr
  nsym=126
  do i=1,nsym
     pr(i)=2*nprc(i)-1
  enddo

! Determine locations of data and reference symbols
  k=0
  mr1=0
  do i=1,nsym
     if(pr(i).lt.0.0) then
        k=k+1
        mdat(k)=i
     else
        mr2=i
        if(mr1.eq.0) mr1=i
     endif
  enddo
  nsig=k

! Determine the reference symbols for each data symbol.
  do k=1,nsig
     m=mdat(k)
     mref(k,1)=mr1
     do n=1,10                     !Get ref symbol before data
        if((m-n).gt.0) then
           if (pr(m-n).gt.0.0) go to 10
        endif
     enddo
     go to 12
10   mref(k,1)=m-n
12   mref(k,2)=mr2
     do n=1,10                     !Get ref symbol after data
        if((m+n).le.nsym) then
           if (pr(m+n).gt.0.0) go to 20
        endif
     enddo
     cycle
20   mref(k,2)=m+n
  enddo

! Now do it all again, using opposite logic on pr(i)
  k=0
  mr1=0
  do i=1,nsym
     if(pr(i).gt.0.0) then
        k=k+1
        mdat2(k)=i
     else
        mr2=i
        if(mr1.eq.0) mr1=i
     endif
  enddo
  nsig=k

  do k=1,nsig
     m=mdat2(k)
     mref2(k,1)=mr1
     do n=1,10
        if((m-n).gt.0) then
           if (pr(m-n).lt.0.0) go to 110
        endif
     enddo
     go to 112
110  mref2(k,1)=m-n
112  mref2(k,2)=mr2
     do n=1,10
        if((m+n).le.nsym) then
           if (pr(m+n).lt.0.0) go to 120
        endif
     enddo
     cycle
120  mref2(k,2)=m+n
  enddo

  return
end subroutine setup65
