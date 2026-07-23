function stdmsg(msg0)

  ! Returns .true. if msg0 a standard "JT-style" message
  
  ! i3.n3
  !  0.0   Free text
  !  0.1   DXpeditiion mode
  !  0.2   EU VHF Contest
  !  0.3   ARRL Field Day <=16 transmitters
  !  0.4   ARRL Field Day >16 transmitters
  !  0.5   telemetry
  !  0.6
  !  0.7
  !  1     Standard 77-bit structured message (optional /R)
  !  2     EU VHF Contest (optional /P)
  !  3     ARRL RTTY Contest
  !  4     Nonstandard calls

  use iso_c_binding, only: c_bool
  use packjt
  use packjt77

  character*37 msg0,msg1
  character*77 c77
  logical(c_bool) :: stdmsg

  msg1=msg0
  i3=-1
  n3=-1
  call pack77(msg1,i3,n3,c77)
  stdmsg=(i3.gt.0 .or. n3.gt.0)

!###
!  rewind 82
!  do i=1,nzhash
!     write(82,3082) i,nzhash,callsign(i),ihash10(i),ihash12(i),ihash22(i)
!3082 format(2i5,2x,a13,3i10)
!  enddo
!  flush(82)
!###
  
  return
end function stdmsg

function stdmsg72(msg0,is_jt65)

  use iso_c_binding, only: c_bool
  use packjt, only: packmsg, unpackmsg

  implicit none

  character(len=37), intent(in) :: msg0
  logical(c_bool), intent(in) :: is_jt65
  logical(c_bool) :: stdmsg72
  character(len=22) :: msg72,decoded72
  integer :: dat(12),itype,n

  stdmsg72=.false.
  msg72=msg0(1:22)

  if(msg0(23:24).eq.' d' .and. msg72(22:22).eq.'?') msg72(22:22)=' '

  if(is_jt65) then
     n=len_trim(msg72)
     if(n.ge.4) then
        if(msg72(n-3:n).eq.' OOO') msg72(n-3:)=' '
     endif
  endif

  call packmsg(msg72,dat,itype)
  if(itype.lt.1 .or. itype.gt.5) return

  call unpackmsg(dat,decoded72)
  stdmsg72=(decoded72.eq.msg72)

  return
end function stdmsg72
