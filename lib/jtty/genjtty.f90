subroutine genjtty(umsg,itone,nsym)

! Input:  character*80 umsg               !User message
! Output: integer*4 itone(1:nsym)         !Tones for channel symbols
!         integer*4 nsym                  !Number of channel symbols

  use jtty_mod, only: JTTY_EXCHANGE_UNKNOWN
  parameter (MAX_TONES=59*16)       !Max number of channel symbols
  character*80 umsg                 !User-formatted message
  integer itone(MAX_TONES)          !Array of tone frequencies for this message

  interface
     subroutine genjtty_profile(umsg,exchange_profile,itone,nsym,frame_starts,is_final)
       use jtty_mod, only: MAX_FRAMES
       character(len=80), intent(inout) :: umsg
       integer, intent(in) :: exchange_profile
       integer, intent(out) :: itone(59*MAX_FRAMES),nsym
       integer, intent(out), optional :: frame_starts(MAX_FRAMES)
       integer, intent(in), optional :: is_final
     end subroutine genjtty_profile
  end interface

  call genjtty_profile(umsg,JTTY_EXCHANGE_UNKNOWN,itone,nsym)

 return
end subroutine genjtty

subroutine genjtty_profile(umsg,exchange_profile,itone,nsym,frame_starts,is_final)

  use jtty_mod, only: pack_jtty,MAX_FRAMES
  implicit none
  character(len=80), intent(inout) :: umsg
  integer, intent(in) :: exchange_profile
  integer, intent(out) :: itone(59*MAX_FRAMES),nsym
  integer, intent(out), optional :: frame_starts(MAX_FRAMES)
  integer, intent(in), optional :: is_final
  character(len=34) :: frames(MAX_FRAMES)
  integer :: nframes

  nsym=0
  call pack_jtty(umsg,frames,nframes,exchange_profile,frame_starts,is_final)
  if(nframes.le.0) return
  call genjtty_frames(frames,nframes,itone,nsym)
end subroutine genjtty_profile

subroutine genjtty_atoms(atoms,natoms,itone,nsym)

  use jtty_mod, only: jtty_source_atom,pack_jtty_atoms,MAX_FRAMES
  type(jtty_source_atom), intent(in) :: atoms(:)
  integer, intent(in) :: natoms
  integer, intent(out) :: itone(*)
  integer, intent(out) :: nsym
  character(len=34) :: frames(MAX_FRAMES)
  integer :: nframes
  logical :: valid

  call pack_jtty_atoms(atoms,natoms,frames,nframes,valid)
  nsym=0
  if(.not.valid) return
  call genjtty_frames(frames,nframes,itone,nsym)
end subroutine genjtty_atoms

subroutine genjtty_frames(frames,nframes,itone,nsym)

  use jtty_fec, only: PAYLOAD_BITS,tbcc_encode,is13
  use jtty_tbcc_code_profiles, only: JTTY_TBCC_PROFILE_1167_1545_80F
  implicit none
  character(len=34), intent(in) :: frames(*)
  integer, intent(in) :: nframes
  integer, intent(out) :: itone(*),nsym
  integer :: payload(PAYLOAD_BITS),tone_symbols(46),i,ib

  nsym=0
  do i=1,nframes
     read(frames(i),'(34i1)') payload
     call tbcc_encode(payload,tone_symbols,JTTY_TBCC_PROFILE_1167_1545_80F)
     ib=(i-1)*59+1
     itone(ib:ib+12)=is13
     itone(ib+13:ib+58)=tone_symbols
     nsym=nsym+59
  enddo
end subroutine genjtty_frames

! The tones genjtty_profile transmits for an 80-character message, with
! what lies behind them: msg is rewritten as the text the frames carry,
! frames holds each 34-bit frame as '0'/'1' characters (frame i at
! frames(34*(i-1)+1:34*i)) and frame_starts each frame's 1-based column in
! msg. is_final 0 leaves the end-of-message flag clear. status is
! JTTY_ENCODE_UNENCODABLE, with nsym and nframes 0, when the message holds no
! text, does not fit in MAX_FRAMES frames under its exchange profile, or the
! exchange profile is not one pack_jtty knows.
subroutine genjtty_text_c(msg,exchange_profile,is_final,itone,nsym,frames,nframes, &
     frame_starts,status) bind(C,name='genjtty_text_c')

  use iso_c_binding, only: c_char,c_int
  use jtty_mod, only: pack_jtty,MAX_FRAMES,JTTY_ENCODE_OK,JTTY_ENCODE_UNENCODABLE
  implicit none
  character(kind=c_char), intent(inout) :: msg(80)
  integer(c_int), value, intent(in) :: exchange_profile,is_final
  integer(c_int), intent(out) :: itone(59*MAX_FRAMES),nsym
  character(kind=c_char), intent(out) :: frames(34*MAX_FRAMES)
  integer(c_int), intent(out) :: nframes,frame_starts(MAX_FRAMES),status
  character(len=80) :: text
  character(len=34) :: packed(MAX_FRAMES)
  integer :: starts(MAX_FRAMES),tones(59*MAX_FRAMES),i,j,n,symbols

  do i=1,80
     text(i:i)=msg(i)
  enddo
  starts=0
  call pack_jtty(text,packed,n,int(exchange_profile),starts,int(is_final))
  do i=1,80
     msg(i)=text(i:i)
  enddo
  itone=0; nsym=0; frames=' '; nframes=0; frame_starts=0
  status=JTTY_ENCODE_UNENCODABLE
  if(n.le.0) return
  call genjtty_frames(packed,n,tones,symbols)
  itone(1:symbols)=tones(1:symbols)
  nsym=symbols
  do i=1,n
     do j=1,34
        frames(34*(i-1)+j)=packed(i)(j:j)
     enddo
  enddo
  nframes=n
  frame_starts(1:n)=starts(1:n)
  status=JTTY_ENCODE_OK
end subroutine genjtty_text_c

subroutine genjtty_atoms_c(c_atoms,natoms,itone,nsym,status) bind(C,name='genjtty_atoms_c')

  use iso_c_binding, only: c_int
  use jtty_mod, only: jtty_source_atom_c,MAX_FRAMES,JTTY_ENCODE_OK
  type(jtty_source_atom_c), intent(in) :: c_atoms(*)
  integer(c_int), value, intent(in) :: natoms
  integer(c_int), intent(out) :: itone(*)
  integer(c_int), intent(out) :: nsym
  integer(c_int), intent(out) :: status
  character(len=34) :: frames(MAX_FRAMES)
  integer :: nframes

  nsym=0
  call genjtty_descriptor_frames(c_atoms,int(natoms),frames,nframes,status)
  if(status.eq.JTTY_ENCODE_OK) call genjtty_frames(frames,nframes,itone,nsym)
end subroutine genjtty_atoms_c

! genjtty_atoms_c's tones for the same descriptors with the frames behind
! them, from one packing: each frame a string of 34 '0'/'1' characters, frame
! i at frames(34*(i-1)+1:34*i).
subroutine genjtty_atoms_frames_c(c_atoms,natoms,itone,nsym,frames,nframes,status) &
     bind(C,name='genjtty_atoms_frames_c')

  use iso_c_binding, only: c_char,c_int
  use jtty_mod, only: jtty_source_atom_c,MAX_FRAMES,JTTY_ENCODE_OK
  implicit none
  type(jtty_source_atom_c), intent(in) :: c_atoms(*)
  integer(c_int), value, intent(in) :: natoms
  integer(c_int), intent(out) :: itone(*)
  integer(c_int), intent(out) :: nsym
  character(kind=c_char), intent(out) :: frames(34*MAX_FRAMES)
  integer(c_int), intent(out) :: nframes
  integer(c_int), intent(out) :: status
  character(len=34) :: packed(MAX_FRAMES)
  integer :: i,j,n

  nsym=0; nframes=0; frames=' '
  call genjtty_descriptor_frames(c_atoms,int(natoms),packed,n,status)
  if(status.ne.JTTY_ENCODE_OK) return
  call genjtty_frames(packed,n,itone,nsym)
  do i=1,n
     do j=1,34
        frames(34*(i-1)+j)=packed(i)(j:j)
     enddo
  enddo
  nframes=n
end subroutine genjtty_atoms_frames_c

! Validates the C atom descriptors and packs them; status is JTTY_ENCODE_OK
! only when every descriptor is valid.
subroutine genjtty_descriptor_frames(c_atoms,natoms,frames,nframes,status)

  use iso_c_binding, only: c_char,c_int,c_null_char
  use packjt77_grammar, only: pack77_arrl_section_index,pack77_arrl_section_name
  use jtty_mod, only: jtty_source_atom,jtty_source_atom_c,JTTY_ATOM_CALL, &
       JTTY_ATOM_EXCH_NUM,JTTY_ATOM_EXCH_LOC,JTTY_ATOM_EXCH_PAIR, &
       JTTY_ATOM_CONTROL,JTTY_ATOM_GRID4,jtty_call_atom,jtty_exch_num_atom, &
       jtty_exch_loc_atom,jtty_class_section_atom,jtty_control_atom, &
       jtty_grid4_atom,pack_jtty_atoms,MAX_FRAMES,JTTY_ENCODE_OK, &
       JTTY_ENCODE_INVALID_DESCRIPTOR,JTTY_ENCODE_UNKNOWN_SECTION
  type(jtty_source_atom_c), intent(in) :: c_atoms(*)
  integer, intent(in) :: natoms
  character(len=34), intent(out) :: frames(MAX_FRAMES)
  integer, intent(out) :: nframes
  integer(c_int), intent(out) :: status
  type(jtty_source_atom) :: atoms(MAX_FRAMES)
  character(len=13) :: descriptor_text
  integer :: i,section_index
  logical :: text_valid,valid

  frames=''; nframes=0; status=JTTY_ENCODE_INVALID_DESCRIPTOR
  if(natoms.lt.1 .or. natoms.gt.MAX_FRAMES) return
  do i=1,natoms
     if(c_atoms(i)%reserved.ne.0) return
     call unpack_descriptor_text(c_atoms(i)%text,descriptor_text,text_valid)
     if(.not.text_valid) return
     select case(int(c_atoms(i)%kind))
     case(JTTY_ATOM_CALL)
        if(c_atoms(i)%role.ne.0 .or. c_atoms(i)%value.ne.0) return
        atoms(i)=jtty_call_atom(int(c_atoms(i)%subtype),descriptor_text)
     case(JTTY_ATOM_EXCH_NUM)
        if(len_trim(descriptor_text).ne.0) return
        atoms(i)=jtty_exch_num_atom(int(c_atoms(i)%role), &
             int(c_atoms(i)%subtype),int(c_atoms(i)%value))
     case(JTTY_ATOM_EXCH_LOC)
        if(c_atoms(i)%value.ne.0) return
        atoms(i)=jtty_exch_loc_atom(int(c_atoms(i)%role), &
             int(c_atoms(i)%subtype),descriptor_text)
     case(JTTY_ATOM_EXCH_PAIR)
        if(c_atoms(i)%subtype.ne.1) return
        if(c_atoms(i)%role.lt.0 .or. c_atoms(i)%role.gt.5) return
        section_index=pack77_arrl_section_index(descriptor_text)
        if(section_index.lt.1 .or. descriptor_text.ne.pack77_arrl_section_name(section_index)) then
           status=JTTY_ENCODE_UNKNOWN_SECTION
           return
        endif
        atoms(i)=jtty_class_section_atom(int(c_atoms(i)%value), &
             char(ichar('A')+int(c_atoms(i)%role)),section_index)
     case(JTTY_ATOM_CONTROL)
        if(c_atoms(i)%role.ne.0 .or. c_atoms(i)%value.ne.0 .or. &
             len_trim(descriptor_text).ne.0) return
        atoms(i)=jtty_control_atom(int(c_atoms(i)%subtype))
     case(JTTY_ATOM_GRID4)
        if(c_atoms(i)%subtype.ne.0 .or. c_atoms(i)%value.ne.0) return
        atoms(i)=jtty_grid4_atom(int(c_atoms(i)%role),descriptor_text)
     case default
        return
     end select
  enddo
  call pack_jtty_atoms(atoms,natoms,frames,nframes,valid)
  if(valid) status=JTTY_ENCODE_OK

contains

  subroutine unpack_descriptor_text(c_text,text,text_valid)
    character(kind=c_char), intent(in) :: c_text(9)
    character(len=13), intent(out) :: text
    logical, intent(out) :: text_valid
    integer :: j

    text=''
    text_valid=.false.
    do j=1,9
       if(c_text(j).eq.c_null_char) then
          text_valid=.true.
          return
       endif
       if(j.gt.8) return
       text(j:j)=c_text(j)
    enddo
  end subroutine unpack_descriptor_text
end subroutine genjtty_descriptor_frames
