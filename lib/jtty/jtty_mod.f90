module jtty_mod

  use jtty_source_codec

  parameter (MAX_FRAMES=16)             !Max frames for the encoded message
  character(len=*), parameter :: JTTY_ALPHABET = ALPHABET

contains

subroutine pack_jtty(message,c32,nframes)

! Input:   character*80   message     !JTTY message, as it appears to a user
! Output:  character*34   c32         !34-bit payload: 32 bits of existing
!                                      !grammar (unchanged) + 1 "last frame
!                                      !of this message" flag (c32(:)(34:34))
!                                      !+ 1 reserved bit, always '0'
!                                      !(c32(:)(33:33)), that the decoder
!                                      !uses as a free extra check beyond
!                                      !the CRC.
!          integer        nframes     !Frames in this message (max = 16)
!
! Minimize frames without changing normalized text or guessing contest types.

  use packjt77_grammar, only: pack77_arrl_section_index
  implicit none
  character*80 message,msg
  character*34 c32(MAX_FRAMES)
  integer, parameter :: INF=999
  type(jtty_source_atom) :: choice(80),atoms(MAX_FRAMES)
  integer :: dp(81),successor(80),n,ipos,inext,natoms,nframes
  logical valid

  call normalize_jtty_message(message,msg)
  message=msg
  n=len_trim(msg)
  nframes=0
  c32=''
  if(n.le.0) return

  ! dp(i) is the minimum frame count for msg(i:n).
  dp=INF
  successor=0
  dp(n+1)=0
  do ipos=n,1,-1
     inext=min(n+1,ipos+5)
     call consider(jtty_text5_atom(msg(ipos:inext-1)),inext)
     if(ipos.gt.1) then
        if(msg(ipos-1:ipos-1).ne.' ') cycle
     endif
     call try_compact()
  enddo

  if(dp(1).gt.MAX_FRAMES) then
     nframes=-1
     return
  endif
  ipos=1
  natoms=0
  do while(ipos.le.n)
     natoms=natoms+1
     atoms(natoms)=choice(ipos)
     ipos=successor(ipos)
  enddo
  call pack_jtty_atoms(atoms,natoms,c32,nframes,valid)
  if(.not.valid) then
     c32=''
     nframes=-1
  endif

contains

  subroutine consider(atom,next)
    type(jtty_source_atom), intent(in) :: atom
    integer, intent(in) :: next
    integer :: cost,rank,best_rank,key,best_key

    cost=1+dp(next)
    if(cost.gt.MAX_FRAMES .or. cost.gt.dp(ipos)) return
    rank=merge(1,0,atom%kind.eq.JTTY_ATOM_TEXT5)
    best_rank=merge(1,0,choice(ipos)%kind.eq.JTTY_ATOM_TEXT5)
    key=100*atom%kind+2*atom%subtype+atom%role
    best_key=100*choice(ipos)%kind+2*choice(ipos)%subtype+choice(ipos)%role
    if(cost.eq.dp(ipos)) then
       if(rank.gt.best_rank) return
       if(rank.eq.best_rank) then
          if(next.lt.successor(ipos)) return
          if(next.eq.successor(ipos) .and. key.ge.best_key) return
       endif
    endif
    dp(ipos)=cost
    successor(ipos)=next
    choice(ipos)=atom
  end subroutine consider

  subroutine offer(atom)
    type(jtty_source_atom), intent(in) :: atom
    type(jtty_source_atom) :: decoded
    character(len=34) :: frame
    character(len=80) :: rendered
    logical :: ok,eom
    integer :: length,last,next

    call pack_jtty_atom(atom,frame,.false.,ok)
    if(.not.ok) return
    call unpack_jtty_atom(frame,decoded,ok,eom)
    if(.not.ok) return
    call render_jtty_atom(decoded,rendered,ok)
    if(.not.ok) return
    length=len_trim(rendered)
    if(length.eq.0) return
    last=ipos+length-1
    if(last.gt.n) return
    if(msg(ipos:last).ne.rendered(1:length)) return
    next=n+1
    if(last.lt.n) then
       if(msg(last+1:last+1).ne.' ') return
       ! Structured frames supply exactly one following separator column.
       next=last+2
    endif
    call consider(atom,next)
  end subroutine offer

  subroutine try_compact()
    character(len=80) :: words(3)
    integer :: first,last,nwords,j,action,role,field,value,length,section_index
    logical :: numeric

    words=''
    first=ipos
    nwords=0
    do j=1,size(words)
       if(first.gt.n) exit
       last=index(msg(first:n),' ')
       if(last.eq.0) then
          last=n
       else
          last=first+last-2
       endif
       nwords=j
       words(j)=msg(first:last)
       first=last+2
    enddo

    do j=1,nwords
       if(len_trim(words(j)).gt.len(choice(1)%text)) cycle
       do action=JTTY_CALL_CQ,JTTY_CALL_TU_NOW
          call offer(jtty_call_atom(action,trim(words(j))))
       enddo
    enddo
    do j=lbound(CONTROL_TEXT,1),ubound(CONTROL_TEXT,1)
       call offer(jtty_control_atom(j))
    enddo

    do role=JTTY_ROLE_FIELD_ONLY,JTTY_ROLE_FULL
       field=1
       if(role.eq.JTTY_ROLE_FULL) then
          if(words(1).ne.'599' .or. nwords.lt.2) cycle
          field=2
       endif
       length=len_trim(words(field))
       call decimal_value(trim(words(field)),value,numeric)
       if(numeric) call offer(jtty_exch_num_atom(role,JTTY_NUM_GENERIC,value))
       if(length.eq.4) call offer(jtty_grid4_atom(role,words(field)(1:4)))
       if(role.ne.JTTY_ROLE_FULL .or. length.lt.2 .or. length.gt.3) cycle
       if(scan(trim(words(field)),'ABCDEFGHIJKLMNOPQRSTUVWXYZ').eq.0) cycle
       call offer(jtty_exch_loc_atom(role,JTTY_LOC_QTH,trim(words(field))))
    enddo

    length=len_trim(words(1))
    if(nwords.lt.2 .or. length.lt.2 .or. length.gt.3) return
    call decimal_value(words(1)(1:length-1),value,numeric)
    if(.not.numeric) return
    section_index=pack77_arrl_section_index(trim(words(2)))
    call offer(jtty_class_section_atom(value,words(1)(length:length),section_index))
  end subroutine try_compact

  subroutine decimal_value(text,value,valid)
    character(len=*), intent(in) :: text
    integer, intent(out) :: value
    logical, intent(out) :: valid
    integer :: j

    value=0
    valid=.false.
    if(len(text).lt.1 .or. len(text).gt.6) return
    if(verify(text,'0123456789').ne.0) return
    do j=1,len(text)
       value=10*value+ichar(text(j:j))-ichar('0')
    enddo
    valid=value.le.131071
  end subroutine decimal_value

end subroutine pack_jtty

logical function jtty_standard_call(c13)

! True only for tokens safe to carry in a 28-bit structured callsign field.

  use packjt77
  character*13 c13,c13a
  character*13 unpacked
  character*6 bcall_1
  logical ok1,success
  integer*4 n28

  jtty_standard_call=.false.
  c13a=c13
  ! chkcall is a syntax filter; pack28/unpack28 round-trip defines what this
  ! protocol field can actually carry.
  call chkcall(c13a,bcall_1,ok1)
  if(.not.ok1) return
  if(index(c13a,'/').gt.0) return
  if(c13a(1:1).eq.'Q') return
  if(verify(trim(c13a),'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789').ne.0) return

  call pack28(c13a,n28)
  call unpack28(n28,unpacked,success)
  if(.not.success) return
  jtty_standard_call=trim(unpacked).eq.trim(c13a)

end function jtty_standard_call

subroutine normalize_jtty_message(raw,normalized)

! Fold operator text into the source alphabet used by the JTTY encoder.

  character*80 raw,normalized
  character*1 c
  logical last_space

  normalized=''
  last_space=.true.
  j=0

  do i=1,len(raw)
     c=raw(i:i)
     if(ichar(c).eq.0) c=' '
     ! The decoder uses '~' as a display marker for space, not a source symbol.
     if(c.eq.'~') c=' '
     if(c.ge.'a' .and. c.le.'z') c=char(ichar(c)-32)
     if(jchar(c).lt.0) c='#'
     if(c.eq.' ') then
        if(last_space) cycle
        j=j+1
        normalized(j:j)=' '
        last_space=.true.
     else
        j=j+1
        normalized(j:j)=c
        last_space=.false.
     endif
  enddo

end subroutine normalize_jtty_message

subroutine unpack_jtty(c32,nframes,message,trailing_sep,is_last_frame,source_valid)

! Input:   character*34   c32         !34-bit payload: 32 bits of existing
!                                      !grammar + 1 reserved bit (33) + 1
!                                      !"last frame of this message" flag (34)
!          integer        nframes     !Frames in this message (max = 16)
! Output:  character*80   message     !JTTY message, as it appears to a user
!          logical        trailing_sep (optional) !True if the last frame
!                          processed appended an implicit separator column.
!          logical        is_last_frame (optional) !True if the last frame
!                          processed had its "last frame of message" flag
!                          set -- for callers (e.g. jtty_mdecode's slot
!                          accumulation) that decode one frame at a time and
!                          need to know when a message is complete.
!
! Frame decoding flow:
!   1. Read the class bits once for each 32-bit payload.
!   2. Dispatch only assigned frame forms.
!   3. Append through one bounds-safe path.

  character*80 message
  character*34 c32(MAX_FRAMES)
  logical, intent(out), optional :: trailing_sep
  logical, intent(out), optional :: is_last_frame
  logical, intent(out), optional :: source_valid

  character*80 rendered
  type(jtty_source_atom) atom
  logical last_frame_sep
  logical last_frame_flag
  logical frame_valid,render_valid,frame_eom,all_valid

  message=''
  k=1
  last_frame_sep=.false.
  last_frame_flag=.false.
  all_valid=nframes.ge.1 .and. nframes.le.MAX_FRAMES
  do iframe=1,max(0,min(nframes,MAX_FRAMES))
     last_frame_sep=.false.
     last_frame_flag=.false.
     call unpack_jtty_atom(c32(iframe),atom,frame_valid,frame_eom)
     if(.not.frame_valid) then
        all_valid=.false.
        cycle
     endif
     call render_jtty_atom(atom,rendered,render_valid)
     if(.not.render_valid) then
        all_valid=.false.
        cycle
     endif
     last_frame_flag=frame_eom
     if(atom%kind.eq.JTTY_ATOM_TEXT5) then
        call append_payload_chars()
     else
        call append_text(rendered(1:len_trim(rendered)))
        call append_implicit_separator()
     endif

  enddo

  if(present(trailing_sep)) trailing_sep=last_frame_sep
  if(present(is_last_frame)) is_last_frame=last_frame_flag .and. all_valid
  if(present(source_valid)) source_valid=all_valid

  return

contains

  subroutine append_text(text)
    character*(*) text
    integer i

    do i=1,len(text)
       if(k.le.len(message)) message(k:k)=text(i:i)
       k=k+1
    enddo
  end subroutine append_text

  subroutine append_payload_chars()
    ! TEXT5 carries five 6-bit JTTY characters in the upper 30 bits.
    integer n30, j, idx
    character*1 c

    read(c32(iframe),1006) n30
1006 format(b30.30)
    do j=1,5
       idx=iand(ishft(n30,-6*(5-j)),63)
       c=charj(idx)
       if(c.eq.' ') c='~'
       if(k.le.len(message)) message(k:k)=c
       k=k+1
    enddo
  end subroutine append_payload_chars

  subroutine append_implicit_separator()
    ! Structured frames leave one blank column before any following frame.
    k=k+1
    last_frame_sep=.true.
  end subroutine append_implicit_separator

end subroutine unpack_jtty


character*1 function charj(j)

! Returns the printable character corresponding to JTTY index j (0-63),

  charj=source_char(j)

  return
end function charj

integer function jchar(c0)

! Returns the JTTY index (0-63) corresponding to character c0.
  
  character*1 c0

  jchar=source_index(c0)
  
  return
end function jchar

end module jtty_mod
