program emedop

  use jpl_ephemeris_status, only: EPHEMERIS_INVALID_INPUT,           &
       EPHEMERIS_UNAVAILABLE
  real*8 txfreq8
  real*8 rxfreq8
  real*4 LST
  real*4 lat_a
  real*4 lat_b
  integer start_year,start_month,start_day
  integer stop_year,stop_month,stop_day
  integer ephemeris_result_a,ephemeris_result_b
  character*80 infile
  character*256 jpleph_file_name
  common/jplcom/jpleph_file_name
  data jpleph_file_name/'JPLEPH'/

  nargs=iargc()
  if(nargs.ne.1) then
     print*,'Usage: emedop <infile>'
     go to 999
  endif

  call getarg(1,infile)
  open(10,file=infile,status='old',err=900)
  read(10,1001) lat_a
1001 format(10x,f12.0)
  read(10,1001) wlon_a
  read(10,1001) lat_b
  read(10,1001) wlon_b
  read(10,1001) txfreq8
  read(10,1002) start_year,start_month,start_day,ih,im,is
1002 format(10x,i4,2i2,1x,i2,1x,i2,1x,i2)
  sec_start=3600.0*ih + 60.0*im + is
  read(10,1002) stop_year,stop_month,stop_day,ih,im,is
  sec_stop=3600.0*ih + 60.0*im + is
  read(10,1001) sec_step

  if(start_year.ne.stop_year .or. start_month.ne.stop_month .or.       &
       start_day.ne.stop_day) then
     print*,'Start and stop dates must be the same'
     go to 999
  endif
  if(sec_stop.lt.sec_start) then
     print*,'Stop time must not precede start time'
     go to 999
  endif
  if(sec_step.le.0.0) then
     print*,'Time step must be positive'
     go to 999
  endif

  write(*,1005)
1005 format('  Date       UTC      Tx Freq      Rx Freq    Doppler'/    &
            '------------------------------------------------------')
  
  sec=sec_start
  ncalc=(sec_stop - sec_start)/sec_step

  do icalc=1,ncalc
     uth=sec/3600.0
     call MoonDopJPL(start_year,start_month,start_day,uth,-wlon_a,lat_a,  &
          RAMoon,DecMoon,                                                &
          LST,HA,AzMoon,ElMoon,vr_a,techo,ephemeris_result_a)

     call MoonDopJPL(start_year,start_month,start_day,uth,-wlon_b,lat_b,  &
          RAMoon,DecMoon,                                                &
          LST,HA,AzMoon,ElMoon,vr_b,techo,ephemeris_result_b)
     if(ephemeris_result_a.eq.EPHEMERIS_INVALID_INPUT .or.                 &
          ephemeris_result_b.eq.EPHEMERIS_INVALID_INPUT) then
        print*,'Invalid date or time in input file'
        go to 999
     endif
     if(ephemeris_result_a.eq.EPHEMERIS_UNAVAILABLE .or.                   &
          ephemeris_result_b.eq.EPHEMERIS_UNAVAILABLE) then
        print*,'Ephemeris calculation failed'
        go to 999
     endif
  
     dop_a=-txfreq8*vr_a/2.99792458e5                 !One-way Doppler from a
     dop_b=-txfreq8*vr_b/2.99792458e5                 !One-way Doppler to b
     doppler=1.e6*(dop_a + dop_b)
     rxfreq8=txfreq8 + dop_a + dop_b

     ih=sec/3600.0
     im=(sec-ih*3600.0)/60.0
     is=nint(mod(sec,60.0))
     write(*,1010) start_year,start_month,start_day,ih,im,is,txFreq8,     &
          rxFreq8,doppler
1010 format(i4,2i2.2,2x,i2.2,':',i2.2,':',i2.2,2f13.7,f8.1)

     sec=sec + sec_step
  enddo
  go to 999
900 print*,'Cannot open file ',trim(infile)
999 end program emedop

  
