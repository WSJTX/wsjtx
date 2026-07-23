module packjt77_schema

  use, intrinsic :: iso_fortran_env, only: int64
  implicit none

  integer, parameter :: PACK77_SCHEMA_INT_KIND=int64
  integer, parameter :: PACK77_MAX_SCHEMA_FIELDS=8
  integer, parameter :: PACK77_FIELD_NAME_LEN=16
  integer, parameter :: PACK77_SCHEMA_NAME_LEN=32

  private :: NO_FIELD,put_int,get_int,begin_message,put_int_i4,put_int_i8, &
       put_bits,get_int_i4,get_int_i8,get_bits,valid_field_bounds, &
       encode_pack77_field_day,encode_pack77_type12,decode_pack77_field_day, &
       decode_pack77_type12,require_int,pack77_binary_precondition, &
       bits_are_binary

  ! Schema bit ranges use Fortran string coordinates in the 77-character c77
  ! payload: start_bit is 1-based and integer fields are encoded MSB-first.
  type bit_field
     character(len=PACK77_FIELD_NAME_LEN) :: name=''
     integer :: start_bit=0
     integer :: width=0
     integer(kind=int64) :: min_value=0_int64
     integer(kind=int64) :: max_value=-1_int64
     logical :: bitstring=.false.
  end type bit_field

  type message_schema
     integer :: i3=-1
     integer :: n3=-1
     character(len=PACK77_SCHEMA_NAME_LEN) :: name=''
     integer :: nfields=0
     type(bit_field) :: fields(PACK77_MAX_SCHEMA_FIELDS)
  end type message_schema

  type(bit_field), parameter :: NO_FIELD=bit_field('',0,0,0_int64,-1_int64,.false.)

  interface put_int
     module procedure put_int_i4
     module procedure put_int_i8
  end interface put_int

  interface get_int
     module procedure get_int_i4
     module procedure get_int_i8
  end interface get_int

  type(message_schema), parameter :: PACK77_SCHEMA_FREE_TEXT=message_schema( &
       0,0,'free text',3,(/ &
       bit_field('text_bits',1,71,0_int64,0_int64,.true.), &
       bit_field('n3',72,3,0_int64,0_int64,.false.), &
       bit_field('i3',75,3,0_int64,0_int64,.false.), &
       NO_FIELD,NO_FIELD,NO_FIELD,NO_FIELD,NO_FIELD /))

  type(message_schema), parameter :: PACK77_SCHEMA_TELEMETRY=message_schema( &
       0,5,'telemetry',5,(/ &
       bit_field('ntel1',1,23,0_int64,8388607_int64,.false.), &
       bit_field('ntel2',24,24,0_int64,16777215_int64,.false.), &
       bit_field('ntel3',48,24,0_int64,16777215_int64,.false.), &
       bit_field('n3',72,3,5_int64,5_int64,.false.), &
       bit_field('i3',75,3,0_int64,0_int64,.false.), &
       NO_FIELD,NO_FIELD,NO_FIELD /))

  type(message_schema), parameter :: PACK77_SCHEMA_DXPEDITION=message_schema( &
       0,1,'dxpedition',6,(/ &
       bit_field('n28a',1,28,0_int64,268435455_int64,.false.), &
       bit_field('n28b',29,28,0_int64,268435455_int64,.false.), &
       bit_field('n10',57,10,0_int64,1023_int64,.false.), &
       bit_field('n5',67,5,0_int64,31_int64,.false.), &
       bit_field('n3',72,3,1_int64,1_int64,.false.), &
       bit_field('i3',75,3,0_int64,0_int64,.false.), &
       NO_FIELD,NO_FIELD /))

  type(message_schema), parameter :: PACK77_SCHEMA_FIELD_DAY_LOW=message_schema( &
       0,3,'field day low',8,(/ &
       bit_field('n28a',1,28,0_int64,268435455_int64,.false.), &
       bit_field('n28b',29,28,0_int64,268435455_int64,.false.), &
       bit_field('ir',57,1,0_int64,1_int64,.false.), &
       bit_field('intx',58,4,0_int64,15_int64,.false.), &
       bit_field('nclass',62,3,0_int64,7_int64,.false.), &
       bit_field('isec',65,7,0_int64,127_int64,.false.), &
       bit_field('n3',72,3,3_int64,3_int64,.false.), &
       bit_field('i3',75,3,0_int64,0_int64,.false.) /))

  type(message_schema), parameter :: PACK77_SCHEMA_FIELD_DAY_HIGH=message_schema( &
       0,4,'field day high',8,(/ &
       bit_field('n28a',1,28,0_int64,268435455_int64,.false.), &
       bit_field('n28b',29,28,0_int64,268435455_int64,.false.), &
       bit_field('ir',57,1,0_int64,1_int64,.false.), &
       bit_field('intx',58,4,0_int64,15_int64,.false.), &
       bit_field('nclass',62,3,0_int64,7_int64,.false.), &
       bit_field('isec',65,7,0_int64,127_int64,.false.), &
       bit_field('n3',72,3,4_int64,4_int64,.false.), &
       bit_field('i3',75,3,0_int64,0_int64,.false.) /))

  type(message_schema), parameter :: PACK77_SCHEMA_WSPR_TYPE1=message_schema( &
       0,6,'wspr type 1',8,(/ &
       bit_field('n28',1,28,0_int64,268435455_int64,.false.), &
       bit_field('igrid4',29,15,0_int64,32767_int64,.false.), &
       ! idbm max 18 = nint(0.3*dBm) for dBm 0..60; range enforced here
       bit_field('idbm',44,5,0_int64,18_int64,.false.), &
       bit_field('j49',49,1,0_int64,0_int64,.false.), &
       bit_field('j50',50,1,0_int64,0_int64,.false.), &
       bit_field('pad',51,21,0_int64,0_int64,.false.), &
       bit_field('n3',72,3,6_int64,6_int64,.false.), &
       bit_field('i3',75,3,0_int64,0_int64,.false.) /))

  type(message_schema), parameter :: PACK77_SCHEMA_WSPR_TYPE2=message_schema( &
       0,6,'wspr type 2',7,(/ &
       bit_field('n28',1,28,0_int64,268435455_int64,.false.), &
       bit_field('npfx',29,16,0_int64,65535_int64,.false.), &
       ! idbm max 18 = nint(0.3*dBm) for dBm 0..60; range enforced here
       bit_field('idbm',45,5,0_int64,18_int64,.false.), &
       bit_field('j50',50,1,1_int64,1_int64,.false.), &
       bit_field('pad',51,21,0_int64,0_int64,.false.), &
       bit_field('n3',72,3,6_int64,6_int64,.false.), &
       bit_field('i3',75,3,0_int64,0_int64,.false.), &
       NO_FIELD /))

  type(message_schema), parameter :: PACK77_SCHEMA_WSPR_TYPE3=message_schema( &
       0,6,'wspr type 3',6,(/ &
       bit_field('n22',1,22,0_int64,4194303_int64,.false.), &
       bit_field('igrid6',23,25,0_int64,33554431_int64,.false.), &
       bit_field('selector',48,3,2_int64,2_int64,.false.), &
       bit_field('pad',51,21,0_int64,0_int64,.false.), &
       bit_field('n3',72,3,6_int64,6_int64,.false.), &
       bit_field('i3',75,3,0_int64,0_int64,.false.), &
       NO_FIELD,NO_FIELD /))

  type(message_schema), parameter :: PACK77_SCHEMA_TYPE1=message_schema( &
       1,0,'type 1 standard',7,(/ &
       bit_field('n28a',1,28,0_int64,268435455_int64,.false.), &
       bit_field('ipa',29,1,0_int64,1_int64,.false.), &
       bit_field('n28b',30,28,0_int64,268435455_int64,.false.), &
       bit_field('ipb',58,1,0_int64,1_int64,.false.), &
       bit_field('ir',59,1,0_int64,1_int64,.false.), &
       bit_field('igrid4',60,15,0_int64,32767_int64,.false.), &
       bit_field('i3',75,3,1_int64,1_int64,.false.), &
       NO_FIELD /))

  type(message_schema), parameter :: PACK77_SCHEMA_TYPE2=message_schema( &
       2,0,'type 2 portable',7,(/ &
       bit_field('n28a',1,28,0_int64,268435455_int64,.false.), &
       bit_field('ipa',29,1,0_int64,1_int64,.false.), &
       bit_field('n28b',30,28,0_int64,268435455_int64,.false.), &
       bit_field('ipb',58,1,0_int64,1_int64,.false.), &
       bit_field('ir',59,1,0_int64,1_int64,.false.), &
       bit_field('igrid4',60,15,0_int64,32767_int64,.false.), &
       bit_field('i3',75,3,2_int64,2_int64,.false.), &
       NO_FIELD /))

  type(message_schema), parameter :: PACK77_SCHEMA_TYPE3=message_schema( &
       3,0,'type 3 rtty',7,(/ &
       bit_field('itu',1,1,0_int64,1_int64,.false.), &
       bit_field('n28a',2,28,0_int64,268435455_int64,.false.), &
       bit_field('n28b',30,28,0_int64,268435455_int64,.false.), &
       bit_field('ir',58,1,0_int64,1_int64,.false.), &
       bit_field('irpt',59,3,0_int64,7_int64,.false.), &
       bit_field('nexch',62,13,0_int64,8191_int64,.false.), &
       bit_field('i3',75,3,3_int64,3_int64,.false.), &
       NO_FIELD /))

  type(message_schema), parameter :: PACK77_SCHEMA_TYPE4=message_schema( &
       4,0,'type 4 nonstandard',6,(/ &
       bit_field('n12',1,12,0_int64,4095_int64,.false.), &
       bit_field('n58',13,58,0_int64,288230376151711743_int64,.false.), &
       bit_field('iflip',71,1,0_int64,1_int64,.false.), &
       bit_field('nrpt',72,2,0_int64,3_int64,.false.), &
       bit_field('icq',74,1,0_int64,1_int64,.false.), &
       bit_field('i3',75,3,4_int64,4_int64,.false.), &
       NO_FIELD,NO_FIELD /))

  type(message_schema), parameter :: PACK77_SCHEMA_TYPE5=message_schema( &
       5,0,'type 5 eu vhf',7,(/ &
       bit_field('n12',1,12,0_int64,4095_int64,.false.), &
       bit_field('n22',13,22,0_int64,4194303_int64,.false.), &
       bit_field('ir',35,1,0_int64,1_int64,.false.), &
       bit_field('irpt',36,3,0_int64,7_int64,.false.), &
       bit_field('iserial',39,11,1_int64,2047_int64,.false.), &
       ! igrid6 max 18662399 = EU-VHF grid6 range, enforced here
       ! (WSPR type3 igrid6 keeps the full 2^25-1 range)
       bit_field('igrid6',50,25,0_int64,18662399_int64,.false.), &
       bit_field('i3',75,3,5_int64,5_int64,.false.), &
       NO_FIELD /))

  type pack77_free_text_fields
     character(len=71) :: text_bits=''
  end type pack77_free_text_fields

  type pack77_telemetry_fields
     integer :: ntel1=0
     integer :: ntel2=0
     integer :: ntel3=0
  end type pack77_telemetry_fields

  type pack77_dxpedition_fields
     integer :: n28a=0
     integer :: n28b=0
     integer :: n10=0
     integer :: n5=0
  end type pack77_dxpedition_fields

  type pack77_field_day_fields
     integer :: n28a=0
     integer :: n28b=0
     integer :: ir=0
     integer :: intx=0
     integer :: nclass=0
     integer :: isec=0
  end type pack77_field_day_fields

  type pack77_wspr_type1_fields
     integer :: n28=0
     integer :: igrid4=0
     integer :: idbm=0
  end type pack77_wspr_type1_fields

  type pack77_wspr_type2_fields
     integer :: n28=0
     integer :: npfx=0
     integer :: idbm=0
  end type pack77_wspr_type2_fields

  type pack77_wspr_type3_fields
     integer :: n22=0
     integer :: igrid6=0
  end type pack77_wspr_type3_fields

  type pack77_type12_fields
     integer :: n28a=0
     integer :: ipa=0
     integer :: n28b=0
     integer :: ipb=0
     integer :: ir=0
     integer :: igrid4=0
  end type pack77_type12_fields

  type pack77_type3_fields
     integer :: itu=0
     integer :: n28a=0
     integer :: n28b=0
     integer :: ir=0
     integer :: irpt=0
     integer :: nexch=0
  end type pack77_type3_fields

  type pack77_type4_fields
     integer :: n12=0
     integer(kind=int64) :: n58=0_int64
     integer :: iflip=0
     integer :: nrpt=0
     integer :: icq=0
  end type pack77_type4_fields

  type pack77_type5_fields
     integer :: n12=0
     integer :: n22=0
     integer :: ir=0
     integer :: irpt=0
     integer :: iserial=0
     integer :: igrid6=0
  end type pack77_type5_fields
contains


  subroutine begin_message(c77,ok)
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    c77=repeat('0',len(c77))
    ok=.true.
  end subroutine begin_message

  subroutine put_int_i4(ok,c77,field,value)
    logical, intent(inout) :: ok
    character(len=77), intent(inout) :: c77
    type(bit_field), intent(in) :: field
    integer, intent(in) :: value

    call put_int_i8(ok,c77,field,int(value,kind=int64))
  end subroutine put_int_i4

  subroutine put_int_i8(ok,c77,field,value)
    logical, intent(inout) :: ok
    character(len=77), intent(inout) :: c77
    type(bit_field), intent(in) :: field
    integer(kind=int64), intent(in) :: value
    integer :: i
    integer(kind=int64) :: place

    if(.not.ok) return
    ok=.false.
    if(.not.valid_field_bounds(field,len(c77))) return
    if(field%bitstring) return
    if(value.lt.field%min_value .or. value.gt.field%max_value) return
    do i=field%width,1,-1
       place=2_int64**(field%width-i)
       if(mod(value/place,2_int64).eq.1_int64) then
          c77(field%start_bit+i-1:field%start_bit+i-1)='1'
       else
          c77(field%start_bit+i-1:field%start_bit+i-1)='0'
       endif
    enddo
    ok=.true.
  end subroutine put_int_i8

  subroutine put_bits(ok,c77,field,bits)
    logical, intent(inout) :: ok
    character(len=77), intent(inout) :: c77
    type(bit_field), intent(in) :: field
    character(len=*), intent(in) :: bits
    integer :: i

    if(.not.ok) return
    ok=.false.
    if(.not.valid_field_bounds(field,len(c77))) return
    if(.not.field%bitstring) return
    if(len_trim(bits).ne.field%width) return
    do i=1,field%width
       if(bits(i:i).ne.'0' .and. bits(i:i).ne.'1') return
    enddo
    c77(field%start_bit:field%start_bit+field%width-1)=bits(1:field%width)
    ok=.true.
  end subroutine put_bits

  subroutine get_int_i4(ok,c77,field,value)
    logical, intent(inout) :: ok
    character(len=77), intent(in) :: c77
    type(bit_field), intent(in) :: field
    integer, intent(out) :: value
    integer(kind=int64) :: int64_value

    value=0
    call get_int_i8(ok,c77,field,int64_value)
    if(ok) value=int(int64_value)
  end subroutine get_int_i4

  subroutine get_int_i8(ok,c77,field,value)
    logical, intent(inout) :: ok
    character(len=77), intent(in) :: c77
    type(bit_field), intent(in) :: field
    integer(kind=int64), intent(out) :: value
    integer :: i

    value=0_int64
    if(.not.ok) return
    ok=.false.
    if(.not.valid_field_bounds(field,len(c77))) return
    if(field%bitstring) return
    do i=0,field%width-1
       value=value*2_int64
       if(c77(field%start_bit+i:field%start_bit+i).eq.'1') then
          value=value+1_int64
       else if(c77(field%start_bit+i:field%start_bit+i).ne.'0') then
          return
       endif
    enddo
    if(value.lt.field%min_value .or. value.gt.field%max_value) return
    ok=.true.
  end subroutine get_int_i8

  subroutine get_bits(ok,c77,field,bits)
    logical, intent(inout) :: ok
    character(len=77), intent(in) :: c77
    type(bit_field), intent(in) :: field
    character(len=*), intent(out) :: bits

    bits=''
    if(.not.ok) return
    ok=.false.
    if(.not.valid_field_bounds(field,len(c77))) return
    if(.not.field%bitstring) return
    if(field%width.gt.len(bits)) return
    if(.not.bits_are_binary(c77(field%start_bit:field%start_bit+field%width-1))) return
    bits(1:field%width)=c77(field%start_bit:field%start_bit+field%width-1)
    ok=.true.
  end subroutine get_bits

  logical function valid_field_bounds(field,nbits) result(ok)
    type(bit_field), intent(in) :: field
    integer, intent(in) :: nbits

    ok=.false.
    if(field%start_bit.lt.1 .or. field%width.lt.1) return
    if(field%start_bit+field%width-1.gt.nbits) return
    ok=.true.
  end function valid_field_bounds

  subroutine encode_pack77_free_text(text_bits,c77,ok)
    character(len=*), intent(in) :: text_bits
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok
    character(len=71) :: packed_text

    call begin_message(c77,ok)
    if(len(text_bits).ne.71) then
       ok=.false.
       return
    endif
    packed_text=text_bits
    call put_bits(ok,c77,PACK77_SCHEMA_FREE_TEXT%fields(1),packed_text)
    call put_int(ok,c77,PACK77_SCHEMA_FREE_TEXT%fields(2),0)
    call put_int(ok,c77,PACK77_SCHEMA_FREE_TEXT%fields(3),0)
  end subroutine encode_pack77_free_text

  subroutine encode_pack77_telemetry(ntel1,ntel2,ntel3,c77,ok)
    integer, intent(in) :: ntel1,ntel2,ntel3
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call begin_message(c77,ok)
    call put_int(ok,c77,PACK77_SCHEMA_TELEMETRY%fields(1),ntel1)
    call put_int(ok,c77,PACK77_SCHEMA_TELEMETRY%fields(2),ntel2)
    call put_int(ok,c77,PACK77_SCHEMA_TELEMETRY%fields(3),ntel3)
    call put_int(ok,c77,PACK77_SCHEMA_TELEMETRY%fields(4),5)
    call put_int(ok,c77,PACK77_SCHEMA_TELEMETRY%fields(5),0)
  end subroutine encode_pack77_telemetry

  subroutine encode_pack77_dxpedition(n28a,n28b,n10,n5,c77,ok)
    integer, intent(in) :: n28a,n28b,n10,n5
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call begin_message(c77,ok)
    call put_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(1),n28a)
    call put_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(2),n28b)
    call put_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(3),n10)
    call put_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(4),n5)
    call put_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(5),1)
    call put_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(6),0)
  end subroutine encode_pack77_dxpedition

  subroutine encode_pack77_field_day(schema,n28a,n28b,ir,intx,nclass,isec,c77,ok)
    type(message_schema), intent(in) :: schema
    integer, intent(in) :: n28a,n28b,ir,intx,nclass,isec
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call begin_message(c77,ok)
    call put_int(ok,c77,schema%fields(1),n28a)
    call put_int(ok,c77,schema%fields(2),n28b)
    call put_int(ok,c77,schema%fields(3),ir)
    call put_int(ok,c77,schema%fields(4),intx)
    call put_int(ok,c77,schema%fields(5),nclass)
    call put_int(ok,c77,schema%fields(6),isec)
    call put_int(ok,c77,schema%fields(7),schema%n3)
    call put_int(ok,c77,schema%fields(8),0)
  end subroutine encode_pack77_field_day

  subroutine encode_pack77_field_day_low(n28a,n28b,ir,intx,nclass,isec,c77,ok)
    integer, intent(in) :: n28a,n28b,ir,intx,nclass,isec
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call encode_pack77_field_day(PACK77_SCHEMA_FIELD_DAY_LOW, &
         n28a,n28b,ir,intx,nclass,isec,c77,ok)
  end subroutine encode_pack77_field_day_low

  subroutine encode_pack77_field_day_high(n28a,n28b,ir,intx,nclass,isec,c77,ok)
    integer, intent(in) :: n28a,n28b,ir,intx,nclass,isec
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call encode_pack77_field_day(PACK77_SCHEMA_FIELD_DAY_HIGH, &
         n28a,n28b,ir,intx,nclass,isec,c77,ok)
  end subroutine encode_pack77_field_day_high

  subroutine encode_pack77_wspr_type1(n28,igrid4,idbm,c77,ok)
    integer, intent(in) :: n28,igrid4,idbm
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call begin_message(c77,ok)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(1),n28)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(2),igrid4)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(3),idbm)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(4),0)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(5),0)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(6),0)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(7),6)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(8),0)
  end subroutine encode_pack77_wspr_type1

  subroutine encode_pack77_wspr_type2(n28,npfx,idbm,c77,ok)
    integer, intent(in) :: n28,npfx,idbm
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call begin_message(c77,ok)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(1),n28)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(2),npfx)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(3),idbm)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(4),1)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(5),0)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(6),6)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(7),0)
  end subroutine encode_pack77_wspr_type2

  subroutine encode_pack77_wspr_type3(n22,igrid6,c77,ok)
    integer, intent(in) :: n22,igrid6
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call begin_message(c77,ok)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(1),n22)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(2),igrid6)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(3),2)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(4),0)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(5),6)
    call put_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(6),0)
  end subroutine encode_pack77_wspr_type3

  subroutine encode_pack77_type12(schema,n28a,ipa,n28b,ipb,ir,igrid4,c77,ok)
    type(message_schema), intent(in) :: schema
    integer, intent(in) :: n28a,ipa,n28b,ipb,ir,igrid4
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call begin_message(c77,ok)
    call put_int(ok,c77,schema%fields(1),n28a)
    call put_int(ok,c77,schema%fields(2),ipa)
    call put_int(ok,c77,schema%fields(3),n28b)
    call put_int(ok,c77,schema%fields(4),ipb)
    call put_int(ok,c77,schema%fields(5),ir)
    call put_int(ok,c77,schema%fields(6),igrid4)
    call put_int(ok,c77,schema%fields(7),schema%i3)
  end subroutine encode_pack77_type12

  subroutine encode_pack77_type1(n28a,ipa,n28b,ipb,ir,igrid4,c77,ok)
    integer, intent(in) :: n28a,ipa,n28b,ipb,ir,igrid4
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call encode_pack77_type12(PACK77_SCHEMA_TYPE1, &
         n28a,ipa,n28b,ipb,ir,igrid4,c77,ok)
  end subroutine encode_pack77_type1

  subroutine encode_pack77_type2(n28a,ipa,n28b,ipb,ir,igrid4,c77,ok)
    integer, intent(in) :: n28a,ipa,n28b,ipb,ir,igrid4
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call encode_pack77_type12(PACK77_SCHEMA_TYPE2, &
         n28a,ipa,n28b,ipb,ir,igrid4,c77,ok)
  end subroutine encode_pack77_type2

  subroutine encode_pack77_type3(itu,n28a,n28b,ir,irpt,nexch,c77,ok)
    integer, intent(in) :: itu,n28a,n28b,ir,irpt,nexch
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call begin_message(c77,ok)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(1),itu)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(2),n28a)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(3),n28b)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(4),ir)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(5),irpt)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(6),nexch)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(7),3)
  end subroutine encode_pack77_type3

  subroutine encode_pack77_type4(n12,n58,iflip,nrpt,icq,c77,ok)
    integer, intent(in) :: n12,iflip,nrpt,icq
    integer(kind=int64), intent(in) :: n58
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call begin_message(c77,ok)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(1),n12)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(2),n58)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(3),iflip)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(4),nrpt)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(5),icq)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(6),4)
  end subroutine encode_pack77_type4

  subroutine encode_pack77_type5(n12,n22,ir,irpt,iserial,igrid6,c77,ok)
    integer, intent(in) :: n12,n22,ir,irpt,iserial,igrid6
    character(len=77), intent(out) :: c77
    logical, intent(out) :: ok

    call begin_message(c77,ok)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(1),n12)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(2),n22)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(3),ir)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(4),irpt)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(5),iserial)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(6),igrid6)
    call put_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(7),5)
  end subroutine encode_pack77_type5

  subroutine decode_pack77_tag(c77,i3,n3,ok,binary_checked)
    character(len=77), intent(in) :: c77
    integer, intent(out) :: i3,n3
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked
    integer(kind=int64) :: value

    i3=-1
    n3=-1
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,bit_field('i3',75,3,0_int64,7_int64,.false.),value)
    if(.not.ok) return
    i3=int(value)
    if(i3.eq.0) then
       call get_int(ok,c77,bit_field('n3',72,3,0_int64,7_int64,.false.),value)
       if(.not.ok) return
       n3=int(value)
    else
       n3=0
    endif
  end subroutine decode_pack77_tag

  subroutine decode_pack77_free_text(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_free_text_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_free_text_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_bits(ok,c77,PACK77_SCHEMA_FREE_TEXT%fields(1),fields%text_bits)
    call require_int(ok,c77,PACK77_SCHEMA_FREE_TEXT%fields(2),0)
    call require_int(ok,c77,PACK77_SCHEMA_FREE_TEXT%fields(3),0)
  end subroutine decode_pack77_free_text

  subroutine decode_pack77_telemetry(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_telemetry_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_telemetry_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,PACK77_SCHEMA_TELEMETRY%fields(1),fields%ntel1)
    call get_int(ok,c77,PACK77_SCHEMA_TELEMETRY%fields(2),fields%ntel2)
    call get_int(ok,c77,PACK77_SCHEMA_TELEMETRY%fields(3),fields%ntel3)
    call require_int(ok,c77,PACK77_SCHEMA_TELEMETRY%fields(4),5)
    call require_int(ok,c77,PACK77_SCHEMA_TELEMETRY%fields(5),0)
  end subroutine decode_pack77_telemetry

  subroutine decode_pack77_dxpedition(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_dxpedition_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_dxpedition_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(1),fields%n28a)
    call get_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(2),fields%n28b)
    call get_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(3),fields%n10)
    call get_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(4),fields%n5)
    call require_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(5),1)
    call require_int(ok,c77,PACK77_SCHEMA_DXPEDITION%fields(6),0)
  end subroutine decode_pack77_dxpedition

  subroutine decode_pack77_field_day_low(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_field_day_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    call decode_pack77_field_day(PACK77_SCHEMA_FIELD_DAY_LOW,c77,fields, &
         ok,binary_checked)
  end subroutine decode_pack77_field_day_low

  subroutine decode_pack77_field_day_high(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_field_day_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    call decode_pack77_field_day(PACK77_SCHEMA_FIELD_DAY_HIGH,c77,fields, &
         ok,binary_checked)
  end subroutine decode_pack77_field_day_high

  subroutine decode_pack77_wspr_type1(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_wspr_type1_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_wspr_type1_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(1),fields%n28)
    call get_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(2),fields%igrid4)
    call get_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(3),fields%idbm)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(4),0)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(5),0)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(6),0)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(7),6)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE1%fields(8),0)
  end subroutine decode_pack77_wspr_type1

  subroutine decode_pack77_wspr_type2(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_wspr_type2_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_wspr_type2_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(1),fields%n28)
    call get_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(2),fields%npfx)
    call get_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(3),fields%idbm)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(4),1)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(5),0)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(6),6)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE2%fields(7),0)
  end subroutine decode_pack77_wspr_type2

  subroutine decode_pack77_wspr_type3(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_wspr_type3_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_wspr_type3_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(1),fields%n22)
    call get_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(2),fields%igrid6)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(3),2)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(4),0)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(5),6)
    call require_int(ok,c77,PACK77_SCHEMA_WSPR_TYPE3%fields(6),0)
  end subroutine decode_pack77_wspr_type3

  subroutine decode_pack77_type1(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_type12_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    call decode_pack77_type12(PACK77_SCHEMA_TYPE1,c77,fields,ok,binary_checked)
  end subroutine decode_pack77_type1

  subroutine decode_pack77_type2(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_type12_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    call decode_pack77_type12(PACK77_SCHEMA_TYPE2,c77,fields,ok,binary_checked)
  end subroutine decode_pack77_type2

  subroutine decode_pack77_type3(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_type3_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_type3_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(1),fields%itu)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(2),fields%n28a)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(3),fields%n28b)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(4),fields%ir)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(5),fields%irpt)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(6),fields%nexch)
    call require_int(ok,c77,PACK77_SCHEMA_TYPE3%fields(7),3)
  end subroutine decode_pack77_type3

  subroutine decode_pack77_type4(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_type4_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_type4_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(1),fields%n12)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(2),fields%n58)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(3),fields%iflip)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(4),fields%nrpt)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(5),fields%icq)
    call require_int(ok,c77,PACK77_SCHEMA_TYPE4%fields(6),4)
  end subroutine decode_pack77_type4

  subroutine decode_pack77_type5(c77,fields,ok,binary_checked)
    character(len=77), intent(in) :: c77
    type(pack77_type5_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_type5_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(1),fields%n12)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(2),fields%n22)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(3),fields%ir)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(4),fields%irpt)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(5),fields%iserial)
    call get_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(6),fields%igrid6)
    call require_int(ok,c77,PACK77_SCHEMA_TYPE5%fields(7),5)
  end subroutine decode_pack77_type5

  subroutine decode_pack77_field_day(schema,c77,fields,ok,binary_checked)
    type(message_schema), intent(in) :: schema
    character(len=77), intent(in) :: c77
    type(pack77_field_day_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_field_day_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,schema%fields(1),fields%n28a)
    call get_int(ok,c77,schema%fields(2),fields%n28b)
    call get_int(ok,c77,schema%fields(3),fields%ir)
    call get_int(ok,c77,schema%fields(4),fields%intx)
    call get_int(ok,c77,schema%fields(5),fields%nclass)
    call get_int(ok,c77,schema%fields(6),fields%isec)
    call require_int(ok,c77,schema%fields(7),schema%n3)
    call require_int(ok,c77,schema%fields(8),0)
  end subroutine decode_pack77_field_day

  subroutine decode_pack77_type12(schema,c77,fields,ok,binary_checked)
    type(message_schema), intent(in) :: schema
    character(len=77), intent(in) :: c77
    type(pack77_type12_fields), intent(out) :: fields
    logical, intent(out) :: ok
    logical, intent(in), optional :: binary_checked

    fields=pack77_type12_fields()
    ok=pack77_binary_precondition(c77,binary_checked)
    call get_int(ok,c77,schema%fields(1),fields%n28a)
    call get_int(ok,c77,schema%fields(2),fields%ipa)
    call get_int(ok,c77,schema%fields(3),fields%n28b)
    call get_int(ok,c77,schema%fields(4),fields%ipb)
    call get_int(ok,c77,schema%fields(5),fields%ir)
    call get_int(ok,c77,schema%fields(6),fields%igrid4)
    call require_int(ok,c77,schema%fields(7),schema%i3)
  end subroutine decode_pack77_type12

  subroutine require_int(ok,c77,field,expected)
    logical, intent(inout) :: ok
    character(len=77), intent(in) :: c77
    type(bit_field), intent(in) :: field
    integer, intent(in) :: expected
    integer(kind=int64) :: value

    call get_int(ok,c77,field,value)
    if(ok) ok=value.eq.int(expected,kind=int64)
  end subroutine require_int

  logical function pack77_binary_precondition(c77,binary_checked) result(ok)
    character(len=*), intent(in) :: c77
    logical, intent(in), optional :: binary_checked

    ok=.false.
    if(present(binary_checked)) then
       if(binary_checked) then
          ! Caller asserts decode_pack77_tag has accepted this same c77.
          ok=.true.
          return
       endif
    endif
    ok=bits_are_binary(c77)
  end function pack77_binary_precondition

  logical function bits_are_binary(c77) result(ok)
    character(len=*), intent(in) :: c77
    integer :: i

    ok=.false.
    do i=1,len(c77)
       if(c77(i:i).ne.'0' .and. c77(i:i).ne.'1') return
    enddo
    ok=.true.
  end function bits_are_binary

end module packjt77_schema
