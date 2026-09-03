program test_jtty_source_codec
  use jtty_mod
  implicit none
  type(jtty_source_atom) :: atom,decoded,atoms(2)
  character(len=34) :: frame,frames(MAX_FRAMES)
  character(len=80) :: text
  logical :: valid,eom
  integer :: i,nframes
  character(len=34), parameter :: CALL_VECTORS(0:5)=[character(len=34) :: &
       '0000100110111101111000110101000001','0000100110111101111000110101010001', &
       '0000100110111101111000110101100001','0000100110111101111000110101110001', &
       '0000100110111101111000110101000101','0000100110111101111000110101010101']
  character(len=34), parameter :: NUM_VECTORS(0:7)=[character(len=34) :: &
       '1000000000000000000001000000001001','1000100000000000000001000000001001', &
       '1001000000000000000001000000001001','1001100000000000000001000000001001', &
       '1010000000000000000001000000001001','1010100000000000000001000000001001', &
       '1011000000000000000001000000001001','1011100000000000000001000000001001']
  character(len=34), parameter :: LOC_VECTORS(0:4)=[character(len=34) :: &
       '1000000000000110111010000000011001','1000100000000110111010000000011001', &
       '1001000000000110111010000000011001','1001100000000110111010000000011001', &
       '1010000000000110111010000000011001']
  character(len=34), parameter :: CONTROL_VECTORS(0:17)=[character(len=34) :: &
       '0000000000000000000000000001001001','0000000000100000000000000001001001', &
       '0000000001000000000000000001001001','0000000001100000000000000001001001', &
       '0000000010000000000000000001001001','0000000010100000000000000001001001', &
       '0000000011000000000000000001001001','0000000011100000000000000001001001', &
       '0000000100000000000000000001001001','0000000100100000000000000001001001', &
       '0000000101000000000000000001001001','0000000101100000000000000001001001', &
       '0000000110000000000000000001001001','0000000110100000000000000001001001', &
       '0000000111000000000000000001001001','0000000111100000000000000001001001', &
       '0000001000000000000000000001001001','0000001000100000000000000001001001']

  do i=0,5
     call expect_vector(jtty_call_atom(i,'K1ABC'),CALL_VECTORS(i))
  enddo
  do i=0,7
     call expect_vector(jtty_exch_num_atom(JTTY_ROLE_FULL,i,1),NUM_VECTORS(i))
  enddo
  do i=0,4
     call expect_vector(jtty_exch_loc_atom(JTTY_ROLE_FULL,i,'CA'),LOC_VECTORS(i))
  enddo
  call expect_vector(jtty_zone_loc_atom(5,'NWT'), &
       '0000001011011110010000110100101001')
  call expect_vector(jtty_class_section_atom(1,'D',11), &
       '0010000010110001011000000000101001')
  call expect_vector(jtty_exch_num_time_atom(JTTY_ROLE_FULL,156,17*60+49), &
       '1000000100111001000010110100111001')
  call expect_vector(jtty_control_atom(JTTY_CONTROL_AGN), &
       '0000000000000000000000000001001001')
  call expect_vector(jtty_grid4_atom(JTTY_ROLE_FIELD_ONLY,'FN42'), &
       '0001001010000110011000000001001001')
  call expect_vector(jtty_text5_atom('HELLO'), &
       '0100010011100101010101010110001101')

  do i=0,17
     atom=jtty_control_atom(i)
     call expect_vector(atom,CONTROL_VECTORS(i))
     call round_trip(atom,text)
     if(trim(text).ne.trim(CONTROL_TEXT(i))) error stop 'control rendering mismatch'
  enddo

  atoms(1)=jtty_call_atom(JTTY_CALL_CALL,'K1ABC')
  atoms(2)=jtty_exch_num_atom(JTTY_ROLE_FULL,JTTY_NUM_SERIAL,7)
  call pack_jtty_atoms(atoms,2,frames,nframes,valid)
  if(.not.valid .or. nframes.ne.2) error stop 'atom sequence rejected'
  if(frames(1)(33:34).ne.'00' .or. frames(2)(33:34).ne.'01') error stop 'EOM mismatch'

  atom=jtty_exch_loc_atom(JTTY_ROLE_FULL,JTTY_LOC_QTH,'0CA')
  call expect_rejected(atom,'noncanonical base36')
  call expect_rejected(jtty_exch_loc_atom(1,0,'A-'),'invalid base36 digit')
  call expect_rejected(jtty_exch_num_atom(1,JTTY_NUM_CQ_ZONE,0),'CQ zone minimum')
  call expect_rejected(jtty_exch_num_atom(1,JTTY_NUM_CQ_ZONE,41),'CQ zone maximum')
  call expect_rejected(jtty_exch_num_atom(1,JTTY_NUM_ITU_ZONE,0),'ITU zone minimum')
  call expect_rejected(jtty_exch_num_atom(1,JTTY_NUM_ITU_ZONE,91),'ITU zone maximum')
  call expect_rejected(jtty_exch_num_atom(1,JTTY_NUM_LICENSE_YEAR,10000),'license year')
  call expect_rejected(jtty_exch_num_time_atom(1,16384,0),'time serial')
  call expect_rejected(jtty_exch_num_time_atom(1,0,1440),'minute')
  call expect_rejected(jtty_zone_loc_atom(0,'CA'),'pair zone')
  call expect_rejected(jtty_class_section_atom(0,'A',1),'transmitter count')
  call expect_rejected(jtty_class_section_atom(33,'A',1),'transmitter count')
  call expect_rejected(jtty_class_section_atom(1,'G',1),'class')
  call expect_rejected(jtty_class_section_atom(1,'A',0),'section')
  call expect_rejected(jtty_class_section_atom(1,'A',87),'section')
  call expect_rejected(jtty_grid4_atom(1,'SA00'),'grid field')
  call expect_rejected(jtty_grid4_atom(1,'AA0A'),'grid digit')
  call expect_rejected(jtty_call_atom(JTTY_CALL_CALL,'BAD'),'malformed call')

  call expect_legal(jtty_exch_num_atom(1,JTTY_NUM_SERIAL,0))
  call expect_legal(jtty_exch_num_atom(1,JTTY_NUM_SERIAL,131071))
  call expect_legal(jtty_exch_num_atom(1,JTTY_NUM_CQ_ZONE,1))
  call expect_legal(jtty_exch_num_atom(1,JTTY_NUM_CQ_ZONE,40))
  call expect_legal(jtty_exch_num_atom(1,JTTY_NUM_ITU_ZONE,1))
  call expect_legal(jtty_exch_num_atom(1,JTTY_NUM_ITU_ZONE,90))
  call expect_legal(jtty_exch_num_atom(1,JTTY_NUM_LICENSE_YEAR,9999))
  call expect_legal(jtty_exch_loc_atom(1,0,'00'))
  call expect_legal(jtty_exch_loc_atom(1,0,'100'))
  call expect_legal(jtty_exch_num_time_atom(1,0,0))
  call expect_legal(jtty_exch_num_time_atom(1,16383,1439))
  call expect_legal(jtty_class_section_atom(1,'A',1))
  call expect_legal(jtty_class_section_atom(32,'F',86))
  call expect_legal(jtty_grid4_atom(1,'AA00'))
  call expect_legal(jtty_grid4_atom(1,'RR99'))

  call expect_render(jtty_exch_num_atom(1,JTTY_NUM_SERIAL,999),'599 999')
  call expect_render(jtty_exch_num_atom(1,JTTY_NUM_SERIAL,1000),'599 1000')
  call expect_render(jtty_exch_num_atom(1,JTTY_NUM_SERIAL,131071),'599 131071')
  call expect_render(jtty_exch_num_atom(0,JTTY_NUM_CHECK,7),'07')
  call expect_render(jtty_exch_num_atom(0,JTTY_NUM_CHECK,100),'100')
  call expect_render(jtty_exch_num_time_atom(1,16383,1439),'599 16383 2359')

  frame=repeat('0',34)
  call unpack_jtty_atom(frame,decoded,valid,eom)
  if(valid .or. eom) error stop 'all-zero sentinel accepted'
  atom=jtty_control_atom(0); call pack_jtty_atom(atom,frame,.true.,valid)
  frame(33:33)='1'; call unpack_jtty_atom(frame,decoded,valid,eom)
  if(valid .or. eom) error stop 'reserved bit 33 accepted'
  atom=jtty_exch_num_atom(JTTY_ROLE_FULL,JTTY_NUM_SERIAL,1)
  call pack_jtty_atom(atom,frame,.true.,valid)
  frame(27:27)='1'; call unpack_jtty_atom(frame,decoded,valid,eom)
  if(valid .or. eom) error stop 'reserved STRUCT30 field accepted'
  atom=jtty_control_atom(0); call pack_jtty_atom(atom,frame,.true.,valid)
  frame(28:30)='101'; call expect_invalid_frame(frame,'reserved family')
  atom=jtty_control_atom(0); call pack_jtty_atom(atom,frame,.true.,valid)
  frame(1:4)='0010'; call expect_invalid_frame(frame,'reserved MISC subtype')
  atom=jtty_zone_loc_atom(1,'CA'); call pack_jtty_atom(atom,frame,.true.,valid)
  frame(1:3)='010'; call expect_invalid_frame(frame,'reserved pair schema')

  write(*,'(a)') 'test_jtty_source_codec: all checks passed'

contains
  subroutine expect_vector(source,want)
    type(jtty_source_atom), intent(in) :: source
    character(len=34), intent(in) :: want
    type(jtty_source_atom) :: result
    character(len=34) :: got
    logical :: ok,last
    call pack_jtty_atom(source,got,.true.,ok)
    if(.not.ok .or. got.ne.want) then
       write(*,'(a,/,a,/,a)') 'golden vector mismatch',want,got
       error stop 1
    endif
    call unpack_jtty_atom(got,result,ok,last)
    if(.not.ok .or. .not.last) error stop 'golden vector did not unpack'
  end subroutine expect_vector

  subroutine round_trip(source,rendered)
    type(jtty_source_atom), intent(in) :: source
    character(len=80), intent(out) :: rendered
    type(jtty_source_atom) :: result
    character(len=34) :: got
    logical :: ok,last
    call pack_jtty_atom(source,got,.true.,ok)
    if(.not.ok) error stop 'round-trip pack failed'
    call unpack_jtty_atom(got,result,ok,last)
    if(.not.ok) error stop 'round-trip unpack failed'
    call render_jtty_atom(result,rendered,ok)
    if(.not.ok) error stop 'round-trip render failed'
  end subroutine round_trip

  subroutine expect_legal(source)
    type(jtty_source_atom), intent(in) :: source
    character(len=34) :: got
    logical :: ok
    call pack_jtty_atom(source,got,.true.,ok)
    if(.not.ok) error stop 'legal boundary rejected'
  end subroutine expect_legal

  subroutine expect_render(source,want)
    type(jtty_source_atom), intent(in) :: source
    character(len=*), intent(in) :: want
    character(len=80) :: got
    call round_trip(source,got)
    if(trim(got).ne.want) then
       write(*,'(a,/,a,/,a)') 'canonical rendering mismatch',want,trim(got)
       error stop 1
    endif
  end subroutine expect_render

  subroutine expect_rejected(source,label)
    type(jtty_source_atom), intent(in) :: source
    character(len=*), intent(in) :: label
    character(len=34) :: got
    logical :: ok
    call pack_jtty_atom(source,got,.true.,ok)
    if(ok) then
       write(*,'(a,a)') 'invalid atom accepted: ',label
       error stop 1
    endif
  end subroutine expect_rejected

  subroutine expect_invalid_frame(got,label)
    character(len=34), intent(in) :: got
    character(len=*), intent(in) :: label
    type(jtty_source_atom) :: result
    logical :: ok,last
    call unpack_jtty_atom(got,result,ok,last)
    if(ok .or. last) then
       write(*,'(a,a)') 'invalid frame accepted or completed EOM: ',label
       error stop 1
    endif
  end subroutine expect_invalid_frame
end program test_jtty_source_codec
