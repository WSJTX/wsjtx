subroutine genjtty(umsg,itone,nsym)

! Input:  character*80 umsg               !User message
! Output: integer*4 itone(1:nsym)         !Tones for channel symbols
!         integer*4 nsym                  !Number of channel symbols

  use jtty_mod
  use jtty_fec
  parameter (MAX_TONES=59*16)       !Max number of channel symbols
  character*80 umsg                 !User-formatted message
  character*34 c32(16)
  integer itone(MAX_TONES)          !Array of tone frequencies for this message
  integer payload(PAYLOAD_BITS)
  integer tone_symbols(46)

  call tbcc_init(JTTY_WAVA_NU)
  call pack_jtty(umsg,c32,nframes)
  nsym=0
  do i=1,nframes
    read(c32(i),'(34i1)') payload
    call tbcc_encode(payload,tone_symbols)
    ib=(i-1)*59+1   ! 59 tones per frame
    itone(ib:ib+12)=is13
    itone(ib+13:ib+58)=tone_symbols
    nsym=nsym+59
 enddo

 return
end subroutine genjtty

subroutine genjtty_atoms(atoms,natoms,itone,nsym)

  use jtty_mod, only: jtty_source_atom,pack_jtty_atoms,MAX_FRAMES
  use jtty_fec
  type(jtty_source_atom), intent(in) :: atoms(:)
  integer, intent(in) :: natoms
  integer, intent(out) :: itone(*)
  integer, intent(out) :: nsym
  character(len=34) :: frames(MAX_FRAMES)
  integer :: payload(PAYLOAD_BITS),tone_symbols(46)
  integer :: nframes,i,ib
  logical :: valid

  call pack_jtty_atoms(atoms,natoms,frames,nframes,valid)
  nsym=0
  if(.not.valid) return
  call tbcc_init(JTTY_WAVA_NU)
  do i=1,nframes
     read(frames(i),'(34i1)') payload
     call tbcc_encode(payload,tone_symbols)
     ib=(i-1)*59+1
     itone(ib:ib+12)=is13
     itone(ib+13:ib+58)=tone_symbols
     nsym=nsym+59
  enddo
end subroutine genjtty_atoms

subroutine genjtty_atoms_c(c_atoms,natoms,itone,nsym) bind(C,name='genjtty_atoms_c')

  use iso_c_binding, only: c_int,c_null_char
  use jtty_mod, only: jtty_source_atom,jtty_source_atom_c,JTTY_ATOM_CALL, &
       JTTY_ATOM_EXCH_NUM,jtty_call_atom,jtty_exch_num_atom,MAX_FRAMES
  type(jtty_source_atom_c), intent(in) :: c_atoms(*)
  integer(c_int), value, intent(in) :: natoms
  integer(c_int), intent(out) :: itone(*)
  integer(c_int), intent(out) :: nsym
  type(jtty_source_atom) :: atoms(MAX_FRAMES)
  character(len=13) :: call_text
  integer :: i,j

  interface
     subroutine genjtty_atoms(atoms,natoms,itone,nsym)
       use jtty_mod, only: jtty_source_atom
       type(jtty_source_atom), intent(in) :: atoms(:)
       integer, intent(in) :: natoms
       integer, intent(out) :: itone(*)
       integer, intent(out) :: nsym
     end subroutine genjtty_atoms
  end interface

  nsym=0
  if(natoms.lt.1 .or. natoms.gt.MAX_FRAMES) return
  do i=1,natoms
     if(c_atoms(i)%reserved.ne.0) return
     select case(int(c_atoms(i)%kind))
     case(JTTY_ATOM_CALL)
        call_text=''
        do j=1,9
           if(c_atoms(i)%call(j).eq.c_null_char) exit
           if(j.gt.8) return
           call_text(j:j)=c_atoms(i)%call(j)
        enddo
        atoms(i)=jtty_call_atom(int(c_atoms(i)%subtype),call_text)
     case(JTTY_ATOM_EXCH_NUM)
        atoms(i)=jtty_exch_num_atom(int(c_atoms(i)%role), &
             int(c_atoms(i)%subtype),int(c_atoms(i)%value))
     case default
        return
     end select
  enddo
  call genjtty_atoms(atoms,int(natoms),itone,nsym)
end subroutine genjtty_atoms_c
