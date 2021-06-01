program synctst

! Tests JT65B2 sync patterns

  parameter (LAGMAX=20)
  real ccf0(0:LAGMAX),ccf2(0:LAGMAX),ccf3(0:LAGMAX)
  character*12 arg
  integer npr(126),np0(126),np1(126),npr2(126)
  data npr/1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0,  &
           0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1,  &
           0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1,  &
           0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,  &
           1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1,  &
           0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1,  &
           1,1,1,1,1,1/

  nargs=iargc()
  if(nargs.ne.1) then
     print*,'Usage: synctst iters'
     go to 999
  endif
  call getarg(1,arg)
  read(arg,*) iters

  worst=0.
  do lag=0,LAGMAX
     nsum=0
     do i=1,126-lag
        nsum=nsum + npr(i)*npr(lag+i)
     enddo
     ccf0(lag)=2.0*nsum/(126.0-lag)
     if(lag.ge.1 .and. ccf0(lag).gt.worst) worst=ccf0(lag)
  enddo

  best2=1.0
  do iter=1,iters

10   np0=0
     np1=0
     n0=0
     do i=1,126
        if(npr(i).eq.1) then
           call random_number(r)
           if(r.lt.0.5) then
              np0(i)=1
              n0=n0+1
           else
              np1(i)=1
           endif
        endif
     enddo
     if(n0.ne.31 .and. n0.ne.32) go to 10

     worst2=0.
     do lag=0,LAGMAX
        nsum=0
        do i=1,126-lag
           nsum=nsum + np0(i)*np0(lag+i) + np1(i)*np1(lag+i)
        enddo
        ccf2(lag)=2.0*nsum/(126.0-lag)
        if(lag.ge.1 .and. ccf2(lag).gt.worst2) then
           worst2=ccf2(lag)
           lagbad=lag
        endif
     enddo
     if(worst2.lt.best2) then
        best2=worst2
        lagbest=lagbad
        n0best=n0
        ccf3=ccf2
        npr2=np0 + 2*np1           
     endif
  enddo

  do lag=0,LAGMAX
     write(13,1100) lag,ccf0(lag),ccf3(lag)
1100 format(i3,2f10.3)
  enddo

  print*,worst,best2,n0best,lagbest
  write(*,1110) npr2
1110 format((8x,20(i1,',')))

999 end program synctst
