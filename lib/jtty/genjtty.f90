subroutine genjtty(umsg,itone,nsym)

! Input:  character*80 umsg               !User message
! Output: integer*4 itone(1:nsym)         !Tones for channel symbols
!         integer*4 nsym                  !Number of channel symbols

  use jtty_mod, only: JTTY_EXCHANGE_UNKNOWN
  parameter (MAX_TONES=59*16)       !Max number of channel symbols
  character*80 umsg                 !User-formatted message
  integer itone(MAX_TONES)          !Array of tone frequencies for this message
  call genjtty_profile(umsg,JTTY_EXCHANGE_UNKNOWN,itone,nsym)

 return
end subroutine genjtty

subroutine genjtty_profile(umsg,exchange_profile,itone,nsym)

  use jtty_mod, only: pack_jtty,MAX_FRAMES
  implicit none
  character(len=80), intent(inout) :: umsg
  integer, intent(in) :: exchange_profile
  integer, intent(out) :: itone(59*MAX_FRAMES),nsym
  character(len=34) :: frames(MAX_FRAMES)
  integer :: nframes

  nsym=0
  call pack_jtty(umsg,frames,nframes,exchange_profile)
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

subroutine genjtty_atoms_c(c_atoms,natoms,itone,nsym,status) bind(C,name='genjtty_atoms_c')

  use iso_c_binding, only: c_char,c_int,c_null_char
  use packjt77_grammar, only: pack77_arrl_section_index
  use jtty_mod, only: jtty_source_atom,jtty_source_atom_c,JTTY_ATOM_CALL, &
       JTTY_ATOM_EXCH_NUM,JTTY_ATOM_EXCH_LOC,JTTY_ATOM_EXCH_PAIR, &
       JTTY_ATOM_CONTROL,JTTY_ATOM_GRID4,jtty_call_atom,jtty_exch_num_atom, &
       jtty_exch_loc_atom,jtty_class_section_atom,jtty_control_atom, &
       jtty_grid4_atom,MAX_FRAMES,JTTY_ENCODE_OK,JTTY_ENCODE_INVALID_DESCRIPTOR, &
       JTTY_ENCODE_UNKNOWN_SECTION
  type(jtty_source_atom_c), intent(in) :: c_atoms(*)
  integer(c_int), value, intent(in) :: natoms
  integer(c_int), intent(out) :: itone(*)
  integer(c_int), intent(out) :: nsym
  integer(c_int), intent(out) :: status
  type(jtty_source_atom) :: atoms(MAX_FRAMES)
  character(len=13) :: descriptor_text
  integer :: i,section_index
  logical :: text_valid

  interface
     subroutine genjtty_atoms(atoms,natoms,itone,nsym)
       use jtty_mod, only: jtty_source_atom
       type(jtty_source_atom), intent(in) :: atoms(:)
       integer, intent(in) :: natoms
       integer, intent(out) :: itone(*)
       integer, intent(out) :: nsym
     end subroutine genjtty_atoms
  end interface

  nsym=0; status=JTTY_ENCODE_INVALID_DESCRIPTOR
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
        if(section_index.lt.1) then
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
  call genjtty_atoms(atoms,int(natoms),itone,nsym)
  if(nsym.gt.0) status=JTTY_ENCODE_OK

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
end subroutine genjtty_atoms_c
