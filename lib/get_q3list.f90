subroutine get_q3list(fname,bDiskData,nlist,list)

  use jpl_ephemeris_status, only: EPHEMERIS_INVALID_INPUT,           &
       EPHEMERIS_UNAVAILABLE

  type q3list
     character*6 call
     character*4 grid
     integer nsec
     integer nfreq
     integer moonel
  end type q3list

  parameter (MAX_CALLERS=50)
  character*(*) fname
  character*36 list(MAX_CALLERS)
  character*8 grid6
  logical*1 bDiskData
  integer time
  integer nt(8)
  integer utc_year,utc_month,utc_day
  integer ephemeris_result
  integer indx(MAX_CALLERS)
  type(q3list) ctmp(MAX_CALLERS),callers(MAX_CALLERS)
  character*256 jpleph_file_name,file24name
  common/jplcom/jpleph_file_name
  common/lu24com/file24name

  nhist2=0
 open(24,file=fname,status='unknown',form='unformatted')
  read(24,end=1) nhist2
  if(nhist2.ge.1 .and. nhist2.le.MAX_CALLERS) then
     read(24,end=1) ctmp(1:nhist2)
  else
     nhist2=0
  endif
1 rewind 24
  if(nhist2.eq.0) go to 900

  now=time()
  call date_and_time(values=nt)
  call normalize_utc_calendar(nt,utc_year,utc_month,utc_day,uth)
  j=0
  
  do i=1,nhist2
     age=(now - ctmp(i)%nsec)/3600.0
     if(age.gt.24.0) cycle
     grid6=ctmp(i)%grid//'mm'
     call grid2deg(grid6,xlon,xlat)
     call sun(utc_year,utc_month,utc_day,uth,-xlon,xlat,RASun,DecSun,    &
          xLST,AzSun,ElSun,mjd,day)
     call moondopjpl(utc_year,utc_month,utc_day,uth,-xlon,xlat,RAMoon,   &
          DecMoon,                                                     &
          xLST,HA,AzMoon,ElMoon,vr,techo,ephemeris_result)
     if(ephemeris_result.eq.EPHEMERIS_INVALID_INPUT .or.             &
          ephemeris_result.eq.EPHEMERIS_UNAVAILABLE) cycle
     if(ElMoon.lt.-5.0 .and. (.not.bDiskData)) cycle
     j=j+1                                   !Keep this one...
     callers(j)=ctmp(i)
     callers(j)%moonel=nint(ElMoon)          !... and save its current moonel
  enddo

  nhist2=j
  write(24) nhist2
  write(24) callers(1:nhist2)

  call indexx(callers(1:nhist2)%nfreq,nhist2,indx)
  do i=1,nhist2
     j=indx(i)
     moon_el=nint(ElMoon)
     age=(now - callers(j)%nsec)/3600.0
     write(list(i),1000) i,callers(j)%nfreq,callers(j)%call,    &
          callers(j)%grid,callers(j)%moonel,age,char(0)
1000 format(i2,'.',i6,2x,a6,2x,a4,i5,f7.1,a1)

!     h1=mod(now,86400)/3600.0
!     h2=mod(callers(i)%nsec,86400)/3600.0
!     hd=h1-h2
!     if(hd.lt.0.0) hd=hd+24.0
!     write(*,3301) i,callers(i)%call,now,callers(i)%nsec,h1,h2,hd
!3301 format(i3,2x,a6,2i12,3f10.6)

  enddo

900 close(24)
  nlist=nhist2
  file24name=fname

  return
end subroutine get_q3list

subroutine normalize_utc_calendar(values,year,month,day,uth)

  implicit none

  integer, intent(in) :: values(8)
  integer, intent(out) :: year,month,day
  real, intent(out) :: uth
  real :: utc_seconds

  year=values(1)
  month=values(2)
  day=values(3)
  utc_seconds=3600.0*values(5) + 60.0*values(6) + values(7) +         &
       0.001*values(8) - 60.0*values(4)

  if(utc_seconds.lt.0.0) then
     utc_seconds=utc_seconds + 86400.0
     day=day - 1
     if(day.lt.1) then
        month=month - 1
        if(month.lt.1) then
           year=year - 1
           month=12
        endif
        day=days_in_month(year,month)
     endif
  else if(utc_seconds.ge.86400.0) then
     utc_seconds=utc_seconds - 86400.0
     day=day + 1
     if(day.gt.days_in_month(year,month)) then
        day=1
        month=month + 1
        if(month.gt.12) then
           year=year + 1
           month=1
        endif
     endif
  endif
  uth=utc_seconds/3600.0

contains

  integer function days_in_month(calendar_year,calendar_month)
    integer, intent(in) :: calendar_year,calendar_month
    logical :: leap_year

    select case(calendar_month)
    case(4,6,9,11)
       days_in_month=30
    case(2)
       leap_year=mod(calendar_year,400).eq.0 .or.                    &
            (mod(calendar_year,4).eq.0 .and. mod(calendar_year,100).ne.0)
       days_in_month=28
       if(leap_year) days_in_month=29
    case default
       days_in_month=31
    end select
  end function days_in_month

end subroutine normalize_utc_calendar

subroutine rm_q3list(dxcall0)

  parameter (MAX_CALLERS=50)
  type q3list
     character*6 call
     character*4 grid
     integer nsec
     integer nfreq
     integer moonel
  end type q3list
  character*(*) dxcall0
  character*6 dxcall
  character*256 file24name
  type(q3list) callers(MAX_CALLERS)
  common/lu24com/file24name

  dxcall=dxcall0
  open(24,file=trim(file24name),status='unknown',form='unformatted')
  read(24) nhist2
  read(24) callers(1:nhist2)

  if(nhist2.eq.MAX_CALLERS .and. dxcall.eq.callers(nhist2)%call) then
     nhist2=MAX_CALLERS - 1
     go to 10
  endif

  iz=nhist2
  do i=1,iz
     if(callers(i)%call .eq. dxcall) then
        nhist2=nhist2-1
        callers(i:nhist2)=callers(i+1:nhist2+1)    !Remove dxcall from q3list
        exit
     endif
  enddo

10 rewind 24
  write(24) nhist2
  write(24) callers(1:nhist2)
  close(24)

  return
end subroutine rm_q3list

subroutine jpl_setup(fname)
  use jpl_ephemeris_status, only: reset_jpl_reader
  character*256 fname,jpleph_file_name
  common/jplcom/jpleph_file_name
  j = index(fname,char(0))
  if(j.eq.0) then
     jpleph_file_name=fname
  else
     jpleph_file_name(1:j)=fname(1:j)
     jpleph_file_name(j:)=' '
  endif
  call reset_jpl_reader()
  return
end subroutine jpl_setup
