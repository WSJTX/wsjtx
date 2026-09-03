module jtty_source_codec
  use iso_c_binding, only: c_char,c_int,c_int8_t,c_int32_t
  use packjt77, only: pack28,unpack28
  use packjt77_grammar, only: PACK77_NSEC,PACK77_ARRL_SECTIONS
  implicit none

! i2.n2  Example             Bits  Total  Purpose
! 0.0    CQ K1ABC CQ        28+2+2   32  CQ call action
! 0.1    K1ABC              28+2+2   32  bare call action
! 0.2    TU K1ABC CQ        28+2+2   32  TU/CQ call action
! 0.3    K1ABC TU           28+2+2   32  call/TU action
! 1.0    K1ABC AGN?        28+2+2   32  call/AGN action
! 1.1    TU NOW K1ABC      28+2+2   32  TU NOW/call action
! 1.2-3  reserved          28+2+2   32  invalid in version 1
! 2.x    STRUCT30          27+3+2   32  typed structured source atom
! 3.x    TEXT5               30+2   32  five six-bit JTTY characters
!
! family  Name           Body fields, from bit 1 toward bit 27
! 000     EXCH_NUM       role1 + number_kind4 + value17 + zero5
! 001     EXCH_LOC       role1 + location_kind4 + length1 + token16 + zero5
! 010     EXCH_PAIR      pair_schema3 + pair_data23 + zero1
! 011     EXCH_NUM_TIME  role1 + serial14 + minute_of_day11 + zero1
! 100     MISC           subtype4 + subtype_data23
! 101-110 reserved       invalid in version 1
! 111     guard space    always invalid

  integer, parameter :: I8=selected_int_kind(18)
  character(len=*), parameter :: ALPHABET = &
       '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!"#$%,&*()_''=[]{}<>|:;'
  integer, parameter :: JTTY_ATOM_CALL=0,JTTY_ATOM_EXCH_NUM=1
  integer, parameter :: JTTY_ATOM_EXCH_LOC=2,JTTY_ATOM_EXCH_PAIR=3
  integer, parameter :: JTTY_ATOM_EXCH_NUM_TIME=4,JTTY_ATOM_CONTROL=5
  integer, parameter :: JTTY_ATOM_GRID4=6,JTTY_ATOM_TEXT5=7
  integer, parameter :: JTTY_CALL_CQ=0,JTTY_CALL_CALL=1,JTTY_CALL_TU_CQ=2
  integer, parameter :: JTTY_CALL_CALL_TU=3,JTTY_CALL_CALL_AGN=4,JTTY_CALL_TU_NOW=5
  integer, parameter :: JTTY_ROLE_FIELD_ONLY=0,JTTY_ROLE_FULL=1
  integer, parameter :: JTTY_NUM_SERIAL=0,JTTY_NUM_CQ_ZONE=1,JTTY_NUM_ITU_ZONE=2
  integer, parameter :: JTTY_NUM_AGE=3,JTTY_NUM_POWER=4,JTTY_NUM_CHECK=5
  integer, parameter :: JTTY_NUM_LICENSE_YEAR=6,JTTY_NUM_GENERIC=7
  integer, parameter :: JTTY_LOC_STATE_PROVINCE=0,JTTY_LOC_SECTION=1
  integer, parameter :: JTTY_LOC_COUNTRY_PREFIX=2,JTTY_LOC_QTH=3,JTTY_LOC_ADMIN_CODE=4
  integer, parameter :: JTTY_PAIR_ZONE_LOC3=0,JTTY_PAIR_CLASS_SECTION=1
  integer, parameter :: JTTY_MISC_CONTROL=0,JTTY_MISC_GRID4=1
  integer(c_int), parameter :: JTTY_ENCODE_OK=0,JTTY_ENCODE_INVALID_DESCRIPTOR=1
  integer(c_int), parameter :: JTTY_ENCODE_UNKNOWN_SECTION=2
  integer, parameter :: JTTY_CONTROL_AGN=0,JTTY_CONTROL_CALL=1,JTTY_CONTROL_AGN_CALL=2
  integer, parameter :: JTTY_CONTROL_NR=3,JTTY_CONTROL_AGN_NR=4,JTTY_CONTROL_EXCH=5
  integer, parameter :: JTTY_CONTROL_STATE=6,JTTY_CONTROL_SECTION=7,JTTY_CONTROL_ZONE=8
  integer, parameter :: JTTY_CONTROL_GRID=9,JTTY_CONTROL_RPRT=10,JTTY_CONTROL_QSL_TU=11
  integer, parameter :: JTTY_CONTROL_TU=12,JTTY_CONTROL_QRZ=13,JTTY_CONTROL_QSO_B4=14
  integer, parameter :: JTTY_CONTROL_WAIT=15,JTTY_CONTROL_NIL=16,JTTY_CONTROL_OK=17
  character(len=8), parameter :: CONTROL_TEXT(0:17)=[character(len=8) :: &
       'AGN?','CALL?','AGN CALL','NR?','AGN NR','EXCH?','STATE?','SECTION?', &
       'ZONE?','GRID?','RPRT?','QSL TU','TU','QRZ?','QSO B4','WAIT','NIL?','OK?']

  type :: jtty_source_atom
     integer :: kind=-1,subtype=0,role=JTTY_ROLE_FIELD_ONLY,value=0,value2=0
     character(len=13) :: text=''
  end type jtty_source_atom

  type, bind(C) :: jtty_source_atom_c
     integer(c_int8_t) :: kind,subtype,role,reserved
     integer(c_int32_t) :: value
     character(kind=c_char) :: text(9)
  end type jtty_source_atom_c

contains

subroutine pack_jtty_atom(atom,frame,eom,valid)
  type(jtty_source_atom), intent(in) :: atom
  character(len=34), intent(out) :: frame
  logical, intent(in) :: eom
  logical, intent(out) :: valid
  integer(I8) :: body,top30,n32,token_value,pair_data,data
  integer :: i,n28,i2,n2,n,grid_index,class_index
  character(len=13) :: call_text,roundtrip
  character(len=5) :: text5
  logical :: success
  frame=''; valid=.false.; n32=0
  select case(atom%kind)
  case(JTTY_ATOM_CALL)
     if(atom%subtype.lt.0 .or. atom%subtype.gt.5) return
     call_text=atom%text
     if(.not.standard_call(call_text)) return
     call pack28(call_text,n28); call unpack28(n28,roundtrip,success)
     if(.not.success .or. trim(roundtrip).ne.trim(call_text)) return
     if(atom%subtype.le.3) then; i2=0; n2=atom%subtype
     else; i2=1; n2=atom%subtype-4; endif
     n32=ishft(int(n28,I8),4)+4*n2+i2
  case(JTTY_ATOM_EXCH_NUM)
     if(.not.valid_role(atom%role) .or. .not.valid_number(atom%subtype,atom%value)) return
     body=ishft(int(atom%role,I8),26)+ishft(int(atom%subtype,I8),22)+ishft(int(atom%value,I8),5)
     call finish_struct(body,0,n32)
  case(JTTY_ATOM_EXCH_LOC)
     if(.not.valid_role(atom%role) .or. atom%subtype.lt.0 .or. atom%subtype.gt.4) return
     n=len_trim(atom%text); if(n.ne.2 .and. n.ne.3) return
     call pack_base36(atom%text(1:n),token_value,success); if(.not.success) return
     body=ishft(int(atom%role,I8),26)+ishft(int(atom%subtype,I8),22)+ &
          ishft(int(n-2,I8),21)+ishft(token_value,5)
     call finish_struct(body,1,n32)
  case(JTTY_ATOM_EXCH_PAIR)
     select case(atom%subtype)
     case(JTTY_PAIR_ZONE_LOC3)
        if(atom%value.lt.1 .or. atom%value.gt.40) return
        n=len_trim(atom%text); if(n.ne.2 .and. n.ne.3) return
        call pack_base36(atom%text(1:n),token_value,success); if(.not.success) return
        pair_data=ishft(int(atom%value,I8),17)+ishft(int(n-2,I8),16)+token_value
     case(JTTY_PAIR_CLASS_SECTION)
        if(atom%value.lt.1 .or. atom%value.gt.32) return
        if(atom%value2.lt.1 .or. atom%value2.gt.PACK77_NSEC .or. len_trim(atom%text).ne.1) return
        class_index=ichar(atom%text(1:1))-ichar('A'); if(class_index.lt.0 .or. class_index.gt.5) return
        pair_data=ishft(int(atom%value,I8),17)+ishft(int(class_index,I8),14)+ &
             ishft(int(atom%value2,I8),7)
     case default; return
     end select
     body=ishft(int(atom%subtype,I8),24)+ishft(pair_data,1); call finish_struct(body,2,n32)
  case(JTTY_ATOM_EXCH_NUM_TIME)
     if(.not.valid_role(atom%role) .or. atom%value.lt.0 .or. atom%value.gt.16383) return
     if(atom%value2.lt.0 .or. atom%value2.gt.1439) return
     body=ishft(int(atom%role,I8),26)+ishft(int(atom%value,I8),12)+ishft(int(atom%value2,I8),1)
     call finish_struct(body,3,n32)
  case(JTTY_ATOM_CONTROL)
     if(atom%subtype.lt.0 .or. atom%subtype.gt.17) return
     body=ishft(int(JTTY_MISC_CONTROL,I8),23)+ishft(int(atom%subtype,I8),16)
     call finish_struct(body,4,n32)
  case(JTTY_ATOM_GRID4)
     if(.not.valid_role(atom%role)) return
     call grid4_to_index(atom%text,grid_index,success); if(.not.success) return
     data=ishft(int(atom%role,I8),22)+ishft(int(grid_index,I8),7)
     body=ishft(int(JTTY_MISC_GRID4,I8),23)+data; call finish_struct(body,4,n32)
  case(JTTY_ATOM_TEXT5)
     text5=atom%text(1:5); top30=0
     do i=1,5
        if(source_index(text5(i:i)).lt.0) return
        top30=64*top30+source_index(text5(i:i))
     enddo
     n32=4*top30+3
  case default; return
  end select
  write(frame(1:32),'(b32.32)') n32
  frame(33:33)='0'
  frame(34:34)=merge('1','0',eom)
  valid=.true.
end subroutine pack_jtty_atom

subroutine finish_struct(body,family,n32)
  integer(I8), intent(in) :: body
  integer, intent(in) :: family
  integer(I8), intent(out) :: n32
  n32=4*(8*body+family)+2
end subroutine finish_struct

subroutine unpack_jtty_atom(frame,atom,valid,eom)
  character(len=34), intent(in) :: frame
  type(jtty_source_atom), intent(out) :: atom
  logical, intent(out) :: valid,eom
  integer(I8) :: top30,body,pair_data,data,token_value
  integer :: i,i2,family,n28,n2,zero,n,grid_index,class_index
  character(len=13) :: call_text
  logical :: success
  atom=jtty_source_atom(); valid=.false.; eom=.false.
  if(verify(frame,'01').ne.0 .or. frame(33:33).ne.'0') return
  if(frame(1:32).eq.repeat('0',32)) return
  read(frame,'(b30.30,b2.2)') top30,i2
  select case(i2)
  case(0,1)
     read(frame,'(b28.28,b2.2)') n28,n2
     if(i2.eq.1 .and. n2.gt.1) return
     call unpack28(n28,call_text,success)
     if(.not.success .or. .not.standard_call(call_text)) return
     atom=jtty_call_atom(merge(n2,n2+4,i2.eq.0),call_text)
  case(2)
     family=int(iand(top30,7_I8)); body=ishft(top30,-3)
     select case(family)
     case(0)
        atom%kind=JTTY_ATOM_EXCH_NUM; atom%role=int(ibits(body,26,1))
        atom%subtype=int(ibits(body,22,4)); atom%value=int(ibits(body,5,17))
        zero=int(ibits(body,0,5))
        if(zero.ne.0 .or. .not.valid_number(atom%subtype,atom%value)) return
     case(1)
        atom%kind=JTTY_ATOM_EXCH_LOC; atom%role=int(ibits(body,26,1))
        atom%subtype=int(ibits(body,22,4)); n=int(ibits(body,21,1))+2
        token_value=ibits(body,5,16); zero=int(ibits(body,0,5))
        if(zero.ne.0 .or. atom%subtype.gt.4 .or. token_value.ge.36_I8**n) return
        if(n.eq.3 .and. token_value.lt.36_I8**2) return
        call unpack_base36(token_value,n,atom%text)
     case(2)
        atom%kind=JTTY_ATOM_EXCH_PAIR; atom%subtype=int(ibits(body,24,3))
        pair_data=ibits(body,1,23); zero=int(ibits(body,0,1)); if(zero.ne.0) return
        select case(atom%subtype)
        case(JTTY_PAIR_ZONE_LOC3)
           atom%value=int(ibits(pair_data,17,6)); n=int(ibits(pair_data,16,1))+2
           token_value=ibits(pair_data,0,16)
           if(atom%value.lt.1 .or. atom%value.gt.40 .or. token_value.ge.36_I8**n) return
           if(n.eq.3 .and. token_value.lt.36_I8**2) return
           call unpack_base36(token_value,n,atom%text)
        case(JTTY_PAIR_CLASS_SECTION)
           atom%value=int(ibits(pair_data,17,6)); class_index=int(ibits(pair_data,14,3))
           atom%value2=int(ibits(pair_data,7,7)); zero=int(ibits(pair_data,0,7))
           if(atom%value.lt.1 .or. atom%value.gt.32 .or. class_index.gt.5) return
           if(atom%value2.lt.1 .or. atom%value2.gt.PACK77_NSEC .or. zero.ne.0) return
           atom%text(1:1)=char(ichar('A')+class_index)
        case default; return
        end select
     case(3)
        atom%kind=JTTY_ATOM_EXCH_NUM_TIME; atom%role=int(ibits(body,26,1))
        atom%value=int(ibits(body,12,14)); atom%value2=int(ibits(body,1,11))
        zero=int(ibits(body,0,1)); if(zero.ne.0 .or. atom%value2.gt.1439) return
     case(4)
        i=int(ibits(body,23,4)); data=ibits(body,0,23)
        select case(i)
        case(JTTY_MISC_CONTROL)
           atom%kind=JTTY_ATOM_CONTROL; atom%subtype=int(ibits(data,16,7))
           zero=int(ibits(data,0,16)); if(zero.ne.0 .or. atom%subtype.gt.17) return
        case(JTTY_MISC_GRID4)
           atom%kind=JTTY_ATOM_GRID4; atom%role=int(ibits(data,22,1))
           grid_index=int(ibits(data,7,15)); zero=int(ibits(data,0,7))
           if(zero.ne.0 .or. grid_index.ge.32400) return
           call index_to_grid4(grid_index,atom%text)
        case default; return
        end select
     case default; return
     end select
  case(3)
     atom%kind=JTTY_ATOM_TEXT5
     do i=1,5
        atom%text(i:i)=source_char(int(iand(ishft(top30,-6*(5-i)),63_I8)))
     enddo
  end select
  eom=frame(34:34).eq.'1'
  valid=.true.
end subroutine unpack_jtty_atom

subroutine render_jtty_atom(atom,text,valid)
  type(jtty_source_atom), intent(in) :: atom
  character(len=80), intent(out) :: text
  logical, intent(out) :: valid
  character(len=16) :: first,second
  character(len=3) :: section
  integer :: hours,minutes
  text=''; valid=.true.
  select case(atom%kind)
  case(JTTY_ATOM_CALL)
     select case(atom%subtype)
     case(JTTY_CALL_CQ); text='CQ '//trim(atom%text)//' CQ'
     case(JTTY_CALL_CALL); text=trim(atom%text)
     case(JTTY_CALL_TU_CQ); text='TU '//trim(atom%text)//' CQ'
     case(JTTY_CALL_CALL_TU); text=trim(atom%text)//' TU'
     case(JTTY_CALL_CALL_AGN); text=trim(atom%text)//' AGN?'
     case(JTTY_CALL_TU_NOW); text='TU NOW '//trim(atom%text)
     case default; valid=.false.
     end select
  case(JTTY_ATOM_EXCH_NUM)
     if(.not.valid_role(atom%role) .or. .not.valid_number(atom%subtype,atom%value)) then
        valid=.false.; return
     endif
     call render_number(atom%subtype,atom%value,first); call render_role(atom%role,trim(first),text)
  case(JTTY_ATOM_EXCH_LOC)
     if(atom%subtype.lt.0 .or. atom%subtype.gt.4) then; valid=.false.; return; endif
     call render_role(atom%role,trim(atom%text),text)
  case(JTTY_ATOM_EXCH_PAIR)
     select case(atom%subtype)
     case(JTTY_PAIR_ZONE_LOC3)
        write(first,'(i2.2)') atom%value; text='599 '//trim(first)//' '//trim(atom%text)
     case(JTTY_PAIR_CLASS_SECTION)
        if(atom%value2.lt.1 .or. atom%value2.gt.PACK77_NSEC) then; valid=.false.; return; endif
        section=PACK77_ARRL_SECTIONS(atom%value2)
        write(first,'(i0,a1)') atom%value,atom%text(1:1); text=trim(first)//' '//trim(section)
     case default; valid=.false.
     end select
  case(JTTY_ATOM_EXCH_NUM_TIME)
     if(.not.valid_role(atom%role) .or. atom%value.lt.0 .or. atom%value.gt.16383 .or. &
          atom%value2.lt.0 .or. atom%value2.gt.1439) then; valid=.false.; return; endif
     call render_number(JTTY_NUM_SERIAL,atom%value,first)
     hours=atom%value2/60; minutes=mod(atom%value2,60)
     write(second,'(i2.2,i2.2)') hours,minutes
     call render_role(atom%role,trim(first)//' '//trim(second),text)
  case(JTTY_ATOM_CONTROL)
     if(atom%subtype.lt.0 .or. atom%subtype.gt.17) then; valid=.false.; return; endif
     text=trim(CONTROL_TEXT(atom%subtype))
  case(JTTY_ATOM_GRID4)
     call render_role(atom%role,atom%text(1:4),text)
  case(JTTY_ATOM_TEXT5); text(1:5)=atom%text(1:5)
  case default; valid=.false.
  end select
end subroutine render_jtty_atom

subroutine render_role(role,field,text)
  integer, intent(in) :: role
  character(len=*), intent(in) :: field
  character(len=80), intent(out) :: text
  if(role.eq.JTTY_ROLE_FULL) then; text='599 '//field; else; text=field; endif
end subroutine render_role

subroutine render_number(kind,value,text)
  integer, intent(in) :: kind,value
  character(len=16), intent(out) :: text
  select case(kind)
  case(JTTY_NUM_SERIAL)
     if(value.lt.1000) then; write(text,'(i3.3)') value; else; write(text,'(i0)') value; endif
  case(JTTY_NUM_CQ_ZONE,JTTY_NUM_ITU_ZONE,JTTY_NUM_CHECK)
     if(value.lt.100) then; write(text,'(i2.2)') value; else; write(text,'(i0)') value; endif
  case(JTTY_NUM_LICENSE_YEAR); write(text,'(i4.4)') value
  case default; write(text,'(i0)') value
  end select
end subroutine render_number

logical function valid_number(kind,value)
  integer, intent(in) :: kind,value
  valid_number=kind.ge.0 .and. kind.le.7 .and. value.ge.0 .and. value.le.131071
  if(.not.valid_number) return
  if(kind.eq.JTTY_NUM_CQ_ZONE) valid_number=value.ge.1 .and. value.le.40
  if(kind.eq.JTTY_NUM_ITU_ZONE) valid_number=value.ge.1 .and. value.le.90
  if(kind.eq.JTTY_NUM_LICENSE_YEAR) valid_number=value.le.9999
end function valid_number

logical function valid_role(role)
  integer, intent(in) :: role
  valid_role=role.eq.JTTY_ROLE_FIELD_ONLY .or. role.eq.JTTY_ROLE_FULL
end function valid_role

logical function standard_call(call_text)
  character(len=13), intent(in) :: call_text
  character(len=13) :: work,unpacked
  character(len=6) :: base_call
  logical :: ok,success
  integer :: n28
  standard_call=.false.; work=call_text
  call chkcall(work,base_call,ok)
  if(.not.ok .or. index(work,'/').gt.0 .or. work(1:1).eq.'Q') return
  if(verify(trim(work),'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789').ne.0) return
  call pack28(work,n28); call unpack28(n28,unpacked,success)
  standard_call=success .and. trim(unpacked).eq.trim(work)
end function standard_call

type(jtty_source_atom) function jtty_call_atom(action,call) result(atom)
  integer, intent(in) :: action
  character(len=*), intent(in) :: call
  atom%kind=JTTY_ATOM_CALL; atom%subtype=action; atom%text=call
end function jtty_call_atom

type(jtty_source_atom) function jtty_exch_num_atom(role,kind,value) result(atom)
  integer, intent(in) :: role,kind,value
  atom%kind=JTTY_ATOM_EXCH_NUM; atom%role=role; atom%subtype=kind; atom%value=value
end function jtty_exch_num_atom

type(jtty_source_atom) function jtty_exch_loc_atom(role,kind,token) result(atom)
  integer, intent(in) :: role,kind
  character(len=*), intent(in) :: token
  atom%kind=JTTY_ATOM_EXCH_LOC; atom%role=role; atom%subtype=kind; atom%text=token
end function jtty_exch_loc_atom

type(jtty_source_atom) function jtty_zone_loc_atom(zone,token) result(atom)
  integer, intent(in) :: zone
  character(len=*), intent(in) :: token
  atom%kind=JTTY_ATOM_EXCH_PAIR; atom%subtype=JTTY_PAIR_ZONE_LOC3
  atom%value=zone; atom%text=token
end function jtty_zone_loc_atom

type(jtty_source_atom) function jtty_class_section_atom(count,class,section_index) result(atom)
  integer, intent(in) :: count,section_index
  character(len=*), intent(in) :: class
  atom%kind=JTTY_ATOM_EXCH_PAIR; atom%subtype=JTTY_PAIR_CLASS_SECTION
  atom%value=count; atom%value2=section_index; atom%text=class
end function jtty_class_section_atom

type(jtty_source_atom) function jtty_exch_num_time_atom(role,serial,minute_of_day) result(atom)
  integer, intent(in) :: role,serial,minute_of_day
  atom%kind=JTTY_ATOM_EXCH_NUM_TIME; atom%role=role
  atom%value=serial; atom%value2=minute_of_day
end function jtty_exch_num_time_atom

type(jtty_source_atom) function jtty_control_atom(phrase_id) result(atom)
  integer, intent(in) :: phrase_id
  atom%kind=JTTY_ATOM_CONTROL; atom%subtype=phrase_id
end function jtty_control_atom

type(jtty_source_atom) function jtty_grid4_atom(role,grid) result(atom)
  integer, intent(in) :: role
  character(len=*), intent(in) :: grid
  atom%kind=JTTY_ATOM_GRID4; atom%role=role; atom%text=grid
end function jtty_grid4_atom

type(jtty_source_atom) function jtty_text5_atom(text) result(atom)
  character(len=*), intent(in) :: text
  integer :: n
  atom%kind=JTTY_ATOM_TEXT5; atom%text=''; n=min(5,len(text))
  if(n.gt.0) atom%text(1:n)=text(1:n)
end function jtty_text5_atom

subroutine pack_jtty_atoms(atoms,natoms,frames,nframes,valid)
  type(jtty_source_atom), intent(in) :: atoms(:)
  integer, intent(in) :: natoms
  character(len=34), intent(out) :: frames(:)
  integer, intent(out) :: nframes
  logical, intent(out) :: valid
  integer :: i
  frames=''; nframes=0
  valid=natoms.ge.1 .and. natoms.le.16 .and. natoms.le.size(atoms) .and. natoms.le.size(frames)
  if(.not.valid) return
  do i=1,natoms
     call pack_jtty_atom(atoms(i),frames(i),i.eq.natoms,valid)
     if(.not.valid) then; frames=''; return; endif
  enddo
  nframes=natoms
end subroutine pack_jtty_atoms

subroutine pack_base36(token,value,valid)
  character(len=*), intent(in) :: token
  integer(I8), intent(out) :: value
  logical, intent(out) :: valid
  integer :: i,digit
  value=0; valid=len(token).eq.2 .or. len(token).eq.3
  if(.not.valid) return
  do i=1,len(token)
     digit=index('0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ',token(i:i))-1
     if(digit.lt.0) then; valid=.false.; return; endif
     value=36*value+digit
  enddo
  if(len(token).eq.3 .and. value.lt.36_I8**2) valid=.false.
end subroutine pack_base36

subroutine unpack_base36(value,n,token)
  integer(I8), intent(in) :: value
  integer, intent(in) :: n
  character(len=13), intent(out) :: token
  integer(I8) :: work
  integer :: i,digit
  token=''; work=value
  do i=n,1,-1
     digit=int(mod(work,36_I8)); work=work/36
     token(i:i)='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'(digit+1:digit+1)
  enddo
end subroutine unpack_base36

subroutine grid4_to_index(grid,index_value,valid)
  character(len=*), intent(in) :: grid
  integer, intent(out) :: index_value
  logical, intent(out) :: valid
  integer :: a,b,c,d
  index_value=0; valid=len_trim(grid).eq.4
  if(.not.valid) return
  a=ichar(grid(1:1))-ichar('A'); b=ichar(grid(2:2))-ichar('A')
  c=ichar(grid(3:3))-ichar('0'); d=ichar(grid(4:4))-ichar('0')
  valid=a.ge.0 .and. a.lt.18 .and. b.ge.0 .and. b.lt.18 .and. &
       c.ge.0 .and. c.lt.10 .and. d.ge.0 .and. d.lt.10
  if(valid) index_value=((a*18+b)*10+c)*10+d
end subroutine grid4_to_index

subroutine index_to_grid4(index_value,grid)
  integer, intent(in) :: index_value
  character(len=13), intent(out) :: grid
  integer :: work,a,b,c,d
  grid=''; work=index_value
  d=mod(work,10); work=work/10; c=mod(work,10); work=work/10
  b=mod(work,18); a=work/18
  grid(1:4)=char(ichar('A')+a)//char(ichar('A')+b)//char(ichar('0')+c)//char(ichar('0')+d)
end subroutine index_to_grid4

integer function source_index(c)
  character(len=1), intent(in) :: c
  source_index=index(ALPHABET,c)-1
end function source_index

character(len=1) function source_char(i)
  integer, intent(in) :: i
  source_char=ALPHABET(i+1:i+1)
end function source_char

end module jtty_source_codec
