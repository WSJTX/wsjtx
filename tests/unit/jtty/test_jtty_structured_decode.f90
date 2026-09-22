program test_jtty_structured_decode

  use iso_c_binding, only: c_int,c_char,c_null_char,c_sizeof
  use iso_fortran_env, only: int16,int32
  use jtty_fec, only: is13,PAYLOAD_BITS,TOTAL_K,tbcc_encode
  use jtty_tbcc_code_profiles
  use jtty_mdec, only: npending,pending_updates,nactive,discard_pending_updates
  use jtty_mod, only: jtty_source_atom,jtty_source_atom_c,jtty_call_atom, &
       jtty_exch_num_atom,unpack_jtty,MAX_FRAMES,JTTY_CALL_CALL,JTTY_ROLE_FULL, &
       JTTY_NUM_SERIAL,JTTY_ATOM_CALL,JTTY_ATOM_EXCH_NUM,JTTY_ATOM_EXCH_LOC, &
       JTTY_ATOM_EXCH_PAIR, &
       JTTY_ATOM_CONTROL,JTTY_ATOM_GRID4,JTTY_LOC_STATE_PROVINCE, &
       JTTY_PAIR_CLASS_SECTION,JTTY_CONTROL_QSL_TU,JTTY_ROLE_FIELD_ONLY, &
       JTTY_ENCODE_OK,JTTY_ENCODE_INVALID_DESCRIPTOR, &
       JTTY_ENCODE_UNKNOWN_SECTION
  implicit none

  integer, parameter :: nsps=384
  integer, parameter :: frame_symbols=size(is13)+TOTAL_K
  integer :: failures

  interface
     function jtty_cpp_n1mm_smoke(tones,nsym) result(status) bind(C)
       import :: c_int
       integer(c_int), intent(out) :: tones(*),nsym
       integer(c_int) :: status
     end function jtty_cpp_n1mm_smoke
     function jtty_cpp_cq_smoke(tones,nsym,text,chained) result(status) bind(C)
       import :: c_int,c_char
       integer(c_int), intent(out) :: tones(*),nsym
       character(kind=c_char), intent(out) :: text(*)
       integer(c_int), value :: chained
       integer(c_int) :: status
     end function jtty_cpp_cq_smoke
     subroutine genjtty_atoms(atoms,natoms,itone,nsym)
       use jtty_mod, only: jtty_source_atom
       type(jtty_source_atom), intent(in) :: atoms(:)
       integer, intent(in) :: natoms
       integer, intent(out) :: itone(*)
       integer, intent(out) :: nsym
     end subroutine genjtty_atoms
     subroutine genjtty_atoms_c(atoms,natoms,itone,nsym,status) bind(C,name='genjtty_atoms_c')
       import :: c_int,jtty_source_atom_c
       type(jtty_source_atom_c), intent(in) :: atoms(*)
       integer(c_int), value, intent(in) :: natoms
       integer(c_int), intent(out) :: itone(*)
       integer(c_int), intent(out) :: nsym
       integer(c_int), intent(out) :: status
     end subroutine genjtty_atoms_c
  end interface

  failures=0
  call reject_reserved_struct_family(failures)
  call decode_native_call_and_serial(failures)
  call decode_c_adapter_atoms(failures)
  call reject_invalid_c_descriptors(failures)
  call decode_cpp_compiled_n1mm(failures)
  call decode_compact_text(failures)

  if(failures.ne.0) then
     write(*,'(a,i0)') 'test_jtty_structured_decode: failures=',failures
     stop 1
  endif
  write(*,'(a)') 'test_jtty_structured_decode: all checks passed'

contains

  subroutine decode_compact_text(count)
    integer, intent(inout) :: count
    integer(c_int) :: native_tones(MAX_FRAMES*frame_symbols),native_nsymbols,status
    character(kind=c_char) :: c_text(80)
    character(len=80) :: text
    integer :: tones(MAX_FRAMES*frame_symbols),nsymbols,chained,i

    do chained=0,1
       status=jtty_cpp_cq_smoke(native_tones,native_nsymbols,c_text,int(chained,c_int))
       call expect(status.eq.JTTY_ENCODE_OK .and. native_nsymbols.eq.frame_symbols, &
            'native F1 generates one frame',count)
       if(status.ne.JTTY_ENCODE_OK .or. native_nsymbols.ne.frame_symbols) return
       do i=1,len(text)
          text(i:i)=c_text(i)
       enddo
       call genjtty(text,tones,nsymbols)
       call expect(nsymbols.eq.native_nsymbols,'GUI CQ text generates one frame',count)
       if(nsymbols.ne.native_nsymbols) return
       call expect(all(tones(1:nsymbols).eq.native_tones(1:nsymbols)), &
            'GUI CQ text and native F1 generate identical channel symbols',count)
    enddo
    call decode_waveform(tones,nsymbols)
    call expect(npending.eq.1,'compact CQ produces one update',count)
    if(npending.eq.1) then
       call expect(trim(normalized(pending_updates(1)%decoded)).eq.'CQ K1ABC CQ' .and. &
            pending_updates(1)%complete .and. nactive.eq.0, &
            'compact CQ preserves text and completes',count)
    endif

    text='TEST WB9XYZ 599 123 QSL TU'
    call genjtty(text,tones,nsymbols)
    call expect(nsymbols.eq.4*frame_symbols,'mixed text generates four frames',count)
    if(nsymbols.ne.4*frame_symbols) return
    call decode_waveform(tones,nsymbols,10.0)
    call expect(npending.eq.1,'mixed text produces one update on the transmitted channel',count)
    if(npending.eq.1) then
       call expect(trim(normalized(pending_updates(1)%decoded)).eq.trim(text) .and. &
            pending_updates(1)%complete .and. nactive.eq.0, &
            'mixed TEXT5/call/exchange/control preserves spacing and EOM',count)
    endif
  end subroutine decode_compact_text

  subroutine decode_cpp_compiled_n1mm(count)
    integer, intent(inout) :: count
    integer(c_int) :: tones(MAX_FRAMES*frame_symbols),nsymbols,status

    status=jtty_cpp_n1mm_smoke(tones,nsymbols)
    call expect(status.eq.JTTY_ENCODE_OK .and. nsymbols.eq.2*frame_symbols, &
         'C++ compiled descriptors cross the native encoder ABI',count)
    if(status.ne.JTTY_ENCODE_OK .or. nsymbols.ne.2*frame_symbols) return
    call decode_waveform(tones,int(nsymbols))
    call expect(npending.eq.1,'C++ compiled N1MM message produces one update',count)
    if(npending.ne.1) return
    call expect(trim(normalized(pending_updates(1)%decoded)).eq.'W9XYZ 32D EMA' .and. &
         pending_updates(1)%complete .and. nactive.eq.0, &
         'C++ call/exchange descriptors preserve canonical text and EOM',count)
  end subroutine decode_cpp_compiled_n1mm

  subroutine decode_native_call_and_serial(count)
    integer, intent(inout) :: count
    type(jtty_source_atom) :: atoms(2)
    integer :: tones(MAX_FRAMES*frame_symbols),nsymbols

    atoms(1)=jtty_call_atom(JTTY_CALL_CALL,'WB9XYZ')
    atoms(2)=jtty_exch_num_atom(JTTY_ROLE_FULL,JTTY_NUM_SERIAL,1234)
    call genjtty_atoms(atoms,size(atoms),tones,nsymbols)

    call expect(nsymbols.eq.size(atoms)*frame_symbols, &
         'native call plus serial generates two frames',count)
    if(nsymbols.le.0) return

    call decode_waveform(tones,nsymbols)

    call expect(npending.eq.1, &
         'native call plus serial produces one update',count)
    if(npending.ne.1) return
    call expect(trim(normalized(pending_updates(1)%decoded)).eq.'WB9XYZ 599 1234', &
         'merged native atoms use canonical call and serial rendering',count)
    call expect(pending_updates(1)%complete .and. nactive.eq.0, &
         'final native atom completes and releases the message',count)
  end subroutine decode_native_call_and_serial

  subroutine decode_c_adapter_atoms(count)
    integer, intent(inout) :: count
    type(jtty_source_atom_c) :: atoms(6)
    integer(c_int) :: tones(MAX_FRAMES*frame_symbols),nsymbols,status

    call expect(c_sizeof(atoms(1)).eq.20,'C atom descriptor remains 20 bytes',count)
    call initialize_c_atom(atoms(1),JTTY_ATOM_CALL,JTTY_CALL_CALL,0,0,'K1ABC')
    call initialize_c_atom(atoms(2),JTTY_ATOM_EXCH_NUM,JTTY_NUM_SERIAL, &
         JTTY_ROLE_FULL,12,'')
    call initialize_c_atom(atoms(3),JTTY_ATOM_EXCH_LOC,JTTY_LOC_STATE_PROVINCE, &
         JTTY_ROLE_FULL,0,'CA')
    call initialize_c_atom(atoms(4),JTTY_ATOM_EXCH_PAIR,JTTY_PAIR_CLASS_SECTION, &
         3,1,'EMA')
    call initialize_c_atom(atoms(5),JTTY_ATOM_GRID4,0,JTTY_ROLE_FIELD_ONLY,0,'FN42')
    call initialize_c_atom(atoms(6),JTTY_ATOM_CONTROL,JTTY_CONTROL_QSL_TU,0,0,'')
    call genjtty_atoms_c(atoms,size(atoms),tones,nsymbols,status)
    call expect(nsymbols.eq.size(atoms)*frame_symbols, &
         'C adapter generates serial/state/FD/grid/control frames',count)
    call expect(status.eq.JTTY_ENCODE_OK,'C adapter reports successful encoding',count)
    if(nsymbols.le.0) return

    call decode_waveform(tones,int(nsymbols))
    call expect(npending.eq.1,'C adapter atoms produce one update',count)
    if(npending.ne.1) return
    call expect(trim(normalized(pending_updates(1)%decoded)).eq. &
         'K1ABC 599 012 599 CA 1D EMA FN42 QSL TU', &
         'C adapter atoms retain canonical structured rendering',count)
    call expect(pending_updates(1)%complete .and. nactive.eq.0, &
         'final C adapter atom completes and releases the message',count)
  end subroutine decode_c_adapter_atoms

  subroutine reject_invalid_c_descriptors(count)
    integer, intent(inout) :: count
    type(jtty_source_atom_c) :: atom(1)
    integer(c_int) :: tones(frame_symbols),nsymbols,status

    call initialize_c_atom(atom(1),JTTY_ATOM_EXCH_LOC,JTTY_LOC_STATE_PROVINCE,1,0,'CA')
    atom(1)%reserved=1
    call genjtty_atoms_c(atom,1,tones,nsymbols,status)
    call expect(nsymbols.eq.0,'C adapter rejects nonzero reserved byte',count)
    call expect(status.eq.JTTY_ENCODE_INVALID_DESCRIPTOR, &
         'C adapter identifies invalid descriptor',count)

    call initialize_c_atom(atom(1),JTTY_ATOM_CALL,JTTY_CALL_CALL,0,0,'K1ABC')
    atom(1)%text='A'
    call genjtty_atoms_c(atom,1,tones,nsymbols,status)
    call expect(nsymbols.eq.0,'C adapter rejects unterminated text',count)

    call initialize_c_atom(atom(1),JTTY_ATOM_EXCH_PAIR,JTTY_PAIR_CLASS_SECTION,0,1,'ZZZ')
    call genjtty_atoms_c(atom,1,tones,nsymbols,status)
    call expect(nsymbols.eq.0,'C adapter rejects unknown Field Day section',count)
    call expect(status.eq.JTTY_ENCODE_UNKNOWN_SECTION, &
         'C adapter identifies unknown Field Day section',count)

    call initialize_c_atom(atom(1),JTTY_ATOM_EXCH_PAIR,0,0,1,'EMA')
    call genjtty_atoms_c(atom,1,tones,nsymbols,status)
    call expect(nsymbols.eq.0,'C adapter rejects non-CLASS_SECTION pair schema',count)

    call initialize_c_atom(atom(1),JTTY_ATOM_GRID4,0,0,0,'SA00')
    call genjtty_atoms_c(atom,1,tones,nsymbols,status)
    call expect(nsymbols.eq.0,'C adapter rejects invalid GRID4',count)

    call initialize_c_atom(atom(1),JTTY_ATOM_CONTROL,18,0,0,'')
    call genjtty_atoms_c(atom,1,tones,nsymbols,status)
    call expect(nsymbols.eq.0,'C adapter rejects unassigned control phrase',count)
  end subroutine reject_invalid_c_descriptors

  subroutine initialize_c_atom(atom,kind,subtype,role,value,text)
    type(jtty_source_atom_c), intent(out) :: atom
    integer, intent(in) :: kind,subtype,role,value
    character(len=*), intent(in) :: text
    integer :: i,n

    atom%kind=kind; atom%subtype=subtype; atom%role=role; atom%reserved=0
    atom%value=value; atom%text=c_null_char
    n=min(8,len_trim(text))
    do i=1,n
       atom%text(i)=text(i:i)
    enddo
  end subroutine initialize_c_atom

  subroutine reject_reserved_struct_family(count)
    integer, intent(inout) :: count
    character(len=34) :: frame,frames(MAX_FRAMES)
    character(len=80) :: decoded
    integer(int32) :: payload(PAYLOAD_BITS),encoded(TOTAL_K)
    integer :: tones(frame_symbols)
    logical :: source_valid,is_last_frame

    ! With an otherwise-zero STRUCT30 body, n32=22 selects reserved family 101.
    write(frame(1:32),'(b32.32)') 22_int32
    frame(33:34)='01'
    frames=''
    frames(1)=frame
    call unpack_jtty(frames,1,decoded,is_last_frame=is_last_frame, &
         source_valid=source_valid)
    call expect(.not.source_valid, &
         'reserved STRUCT30 family is source-invalid',count)
    call expect(.not.is_last_frame, &
         'source-invalid frame cannot report EOM',count)

    read(frame,'(34i1)') payload
    call tbcc_encode(payload,encoded,JTTY_TBCC_PROFILE_1167_1545_80F)
    tones(1:size(is13))=is13
    tones(size(is13)+1:frame_symbols)=encoded
    call decode_waveform(tones,frame_symbols)
    call expect(npending.eq.0 .and. nactive.eq.0, &
         'CRC/FEC-valid source-invalid frame creates no message or update',count)
  end subroutine reject_reserved_struct_family

  subroutine decode_waveform(tones,nsymbols,channel_width)
    integer, intent(in) :: tones(:),nsymbols
    real, intent(in), optional :: channel_width
    integer :: nsamples,total_samples,nfa,nfb
    real :: tolerance
    integer(int16), allocatable :: pcm(:)
    real, allocatable :: wave(:)
    complex, allocatable :: complex_wave(:)

    nsamples=nsymbols*nsps
    total_samples=(nsymbols+frame_symbols)*nsps
    allocate(wave(nsamples),complex_wave(nsamples),pcm(total_samples))
    call gen_jttywave(tones,nsymbols,nsps,2.0,12000.0,1500.0, &
         complex_wave,wave,0,nsamples)
    pcm=0_int16
    pcm(1:nsamples)=int(nint(30000.0*wave),int16)

    nfa=200; nfb=2800; tolerance=50.0
    if(present(channel_width)) then
       tolerance=channel_width
       nfa=ceiling(1500.0-tolerance)
       nfb=floor(1500.0+tolerance)
    endif
    call discard_pending_updates()
    call rjtty_sub(pcm,1,nsps,nfa,nfb,1500.0,tolerance)
    call rjtty_sub(pcm,total_samples,nsps,nfa,nfb,1500.0,tolerance)

    deallocate(pcm,wave,complex_wave)
  end subroutine decode_waveform

  function normalized(value) result(result_value)
    character(len=*), intent(in) :: value
    character(len=80) :: result_value
    integer :: i

    result_value=adjustl(value)
    do i=1,len_trim(result_value)
       if(result_value(i:i).eq.'~') result_value(i:i)=' '
    enddo
  end function normalized

  subroutine expect(condition,description,count)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description
    integer, intent(inout) :: count

    if(.not.condition) then
       count=count+1
       write(*,'(a)') 'FAIL: '//description
    endif
  end subroutine expect

end program test_jtty_structured_decode
