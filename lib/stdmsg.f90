function stdmsg(msg0)

  ! Classify received structured messages, including unresolved callsign hashes.

  use iso_c_binding, only: c_bool
  use packjt77, only: pack77
  use packjt77_grammar, only: pack77_type12_source,pack77_type4_source, &
       pack77_type5_source,pack77_dxpedition_source,pack77_parse_type12_source, &
       pack77_parse_type4_source,pack77_parse_type5_source,pack77_parse_dxpedition_source

  implicit none

  character(len=37), intent(in) :: msg0
  character*37 msg1
  character*77 c77
  logical(c_bool) :: stdmsg
  type(pack77_type12_source) :: type12
  type(pack77_type4_source) :: type4
  type(pack77_type5_source) :: type5
  type(pack77_dxpedition_source) :: dxpedition
  logical :: ok,matched_shape
  integer :: i3,n3

  ! An unresolved received hash is structured but cannot be encoded for transmission.
  if(index(msg0,'<...>').gt.0) then
     call pack77_parse_type12_source(msg0,type12,ok)
     if(.not.ok) call pack77_parse_type4_source(msg0,type4,ok)
     if(.not.ok) call pack77_parse_type5_source(msg0,type5,ok,matched_shape)
     if(.not.ok) call pack77_parse_dxpedition_source(msg0,dxpedition,ok,matched_shape,allow_hash_calls=.true.)
     stdmsg=ok
     return
  endif

  msg1=msg0
  i3=-1
  n3=-1
  call pack77(msg1,i3,n3,c77)
  stdmsg=(i3.gt.0 .or. n3.gt.0)

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
