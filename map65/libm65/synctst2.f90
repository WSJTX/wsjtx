program synctst2

! Tests JT65B2 sync patterns

  parameter (LAGMAX=20)
  real ccf0(0:LAGMAX),ccf1(0:LAGMAX),ccf2(0:LAGMAX),ccf3(0:LAGMAX)
  character arg*12,line*64
  integer*8 n8
  integer npr(126),np0(126),np1(126),npr1(126),npr2(126)
  data npr/1,0,0,1,1,0,0,0,1,1,1,1,1,1,0,1,0,1,0,0,  &
           0,1,0,1,1,0,0,1,0,0,0,1,1,1,0,0,1,1,1,1,  &
           0,1,1,0,1,1,1,1,0,0,0,1,1,0,1,0,1,0,1,1,  &
           0,0,1,1,0,1,0,1,0,1,0,0,1,0,0,0,0,0,0,1,  &
           1,0,0,0,0,0,0,0,1,1,0,1,0,0,1,0,1,1,0,1,  &
           0,1,0,1,0,0,1,1,0,0,1,0,0,1,0,0,0,0,1,1,  &
           1,1,1,1,1,1/

  data npr2/1,0,0,1,2,0,0,0,2,1,1,2,2,2,0,2,0,2,0,0,  &
            0,1,0,2,1,0,0,1,0,0,0,2,1,1,0,0,1,1,2,2,  &
            0,2,2,0,2,1,1,1,0,0,0,1,2,0,1,0,2,0,1,1,  &
            0,0,2,2,0,1,0,1,0,2,0,0,2,0,0,0,0,0,0,1,  &
            1,0,0,0,0,0,0,0,1,2,0,2,0,0,2,0,2,1,0,1,  &
            0,2,0,1,0,0,2,2,0,0,1,0,0,2,0,0,0,0,1,1,  &
            1,2,1,2,1,2/

  data n8/x'4314f4725bb357e0'/

  write(*,1102) n8
  write(line,1102) n8
1102 format(b63)
  read(line,1104) npr1(1:63)
1104 format(63i1)
  npr1(64:126)=npr1(1:63)

  worst=0.
  do lag=0,LAGMAX
     nsum=0
     do i=1,126-lag
        nsum=nsum + npr(i)*npr(lag+i)
     enddo
     ccf0(lag)=2.0*nsum/(126.0-lag)
     if(lag.ge.1 .and. ccf0(lag).gt.worst) worst=ccf0(lag)
  enddo
  

  worst1=0.
  do lag=0,LAGMAX
     nsum=0
     do i=1,126-lag
        nsum=nsum + npr1(i)*npr1(lag+i)
     enddo
     ccf1(lag)=(63.0/64.0)*2.0*nsum/(126.0-lag)
     if(lag.ge.1 .and. ccf1(lag).gt.worst1) worst1=ccf1(lag)
  enddo
  ccf1=ccf1/ccf1(0)
  worst1=worst1/ccf1(0)

  np0=0
  np1=0
  n0=0
  do i=1,126
     if(npr2(i).eq.1) then
        np0(i)=1
        n0=n0+1
     else if(npr2(i).eq.2) then
        np1(i)=1
     endif
  enddo
  
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

  do lag=0,LAGMAX
     write(13,1100) lag,ccf0(lag),ccf1(lag),ccf2(lag)
1100 format(i3,3f10.3)
  enddo

  print*,worst,worst1,worst2,n0,lagbad


999 end program synctst2
