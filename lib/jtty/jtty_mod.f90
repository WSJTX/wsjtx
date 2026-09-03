module jtty_mod

  use jtty_source_codec

  parameter (MAX_FRAMES=16)             !Max frames for the encoded message
  character(len=*), parameter :: JTTY_ALPHABET = &
       '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!"#$%,&*()_''=[]{}<>|:;'

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
! Literal input is normalized and encoded as TEXT5. Structured source forms
! are available only through the explicit jtty_source_atom interface.

  character*80 message,msg
  character*34 c32(MAX_FRAMES)
  integer n, ipos, n32

  call normalize_jtty_message(message,msg)
  message=msg
  n=len_trim(msg)
  nframes=0
  c32=''
  if(n.le.0) return

  ! Literal operator input is always encoded as TEXT5. Native callers use
  ! pack_jtty_atoms to opt into call and STRUCT30 atoms explicitly.
  nframes=(n+4)/5
  if(nframes.gt.MAX_FRAMES) then
     nframes=-1
     c32=''
     return
  endif
  do ipos=1,n,5
     call pack_text_frame(ipos,n32)
     write(c32((ipos+4)/5),'(b32.32)') n32
     c32((ipos+4)/5)(33:34)='00'
  enddo
  c32(nframes)(34:34)='1'
  return

contains

  subroutine pack_text_frame(istart,n32out)
    ! Place five source characters in the upper 30 bits and set i2=3.
    integer istart,n32out
    integer i, n30
    character*1 c

    n30=0
    do i=istart,istart+4
       c=' '
       if(i.le.n) c=msg(i:i)
       n30=64*n30 + jchar(c)
    enddo
    n32out=ishft(n30,2) + 3
  end subroutine pack_text_frame

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

  charj=JTTY_ALPHABET(j+1:j+1)

  return
end function charj

integer function jchar(c0)

! Returns the JTTY index (0-63) corresponding to character c0.
  
  character*1 c0

  jchar=index(JTTY_ALPHABET,c0)-1
  
  return
end function jchar

end module jtty_mod
