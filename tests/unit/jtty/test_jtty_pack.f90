program test_jtty_pack

  use jtty_mod
  use packjt77, only: pack28
  character*80 msg0,msg,expected
  character*34 c32(MAX_FRAMES)
  character*17 cparms
  character*1 err
  integer, parameter :: expected_errors = 0

  open(10,file='jtty_msgs.txt',status='old')
  write(*,1000)
1000 format('i2.n2 i2.n2 NC NF err Message'/87('-'))
  nerr=0
  nz=0
  c32=''
  do imsg=1,99
     read(10,'(a80)',end=100) msg0
     call normalize_jtty_message(msg0,expected)
     nlength=len_trim(expected)
     if(nlength.eq.0) then
        write(*,*)
        cycle
     endif
     nz=nz+1
     call pack_jtty(msg0,c32,nframes)
     if(nframes.lt.0) then
        err='*'
        nerr=nerr+1
        cycle
     endif
     call expect_no_reserved_frame_ids(c32,nframes,msg0)
     call unpack_jtty(c32,nframes,msg)
     call display_jtty_message(msg)
     err=' '
     if(msg.ne.expected) then
        err='*'
        nerr=nerr+1
        write(12,1010) trim(expected),trim(msg)
1010    format(a/a)
     endif
     read(c32(1),1012) n2a,i2a
     read(c32(2),1012) n2b,i2b
1012 format(28x,2b2)
     write(cparms,1018) i2a,n2a,i2b,n2b,nlength,nframes
1018 format(i2,'.',i1,2x,i2,'.',i1,i4,i3)
     if(i2a.ge.2) cparms(3:4)='  '
     if(i2b.ge.2) cparms(9:10)='  '
     if(nframes.eq.1) cparms(7:10)='   '
     write(*,1020) cparms,err,trim(msg)
1020 format(a17,2x,a1,2x,a)
  enddo

100  write(*,1100) nz,nerr
1100 format(/'Total messages:',i3,'   Number of errors:',i3)
  if(nerr.ne.expected_errors) error stop 1

  call expect_pack('CQ KA1ABC CQ',1,0,0,-1,-1)
  call expect_pack('CQ K1ABC CQ',1,0,0,-1,-1)
  call expect_pack('K1A',1,0,1,-1,-1)
  call expect_pack('WB9XYZ',1,0,1,-1,-1)
  call expect_pack('TU WB9XYZ CQ',1,0,2,-1,-1)
  call expect_pack('WB9XYZ TU',1,0,3,-1,-1)
  call expect_pack('WB9XYZ AGN?',1,1,0,-1,-1)
  call expect_pack('TU NOW WB9XYZ',1,1,1,-1,-1)
  call expect_pack('WB9XYZ TU CQ KA1ABC CQ',2,0,3,0,0)
  call expect_pack('K1ABC TU NOW W1XYZ',2,0,1,1,1)
  call expect_pack('WB9XYZ 599 123',2,0,1,2,-1)
  call expect_pack('599 MA',1,2,-1,-1,-1)
  call expect_pack('599 FN42',1,2,-1,-1,-1)
  call expect_pack('FN42',1,2,-1,-1,-1)
  call expect_pack('1D EMA',1,2,-1,-1,-1)
  call expect_pack('32F EMA',1,2,-1,-1,-1)
  call expect_pack('0',1,2,-1,-1,-1)
  call expect_pack('131071',1,2,-1,-1,-1)
  call expect_pack('599 131071',1,2,-1,-1,-1)
  call expect_pack('599 001',2,-1,-1,-1,-1)
  call expect_pack('599 05',2,-1,-1,-1,-1)
  call expect_pack('599 BRUCE',2,-1,-1,-1,-1)
  call expect_pack('131072',2,3,-1,3,-1)
  call expect_pack('000001',2,3,-1,3,-1)
  call expect_pack('01D EMA',2,3,-1,3,-1)
  call expect_pack('33D EMA',2,3,-1,3,-1)
  call expect_pack('1G EMA',2,3,-1,3,-1)
  call expect_pack('1D ZZZ',2,3,-1,3,-1)
  call expect_pack('599 SA00',2,-1,-1,-1,-1)
  call expect_pack('599 A!',2,-1,-1,-1,-1)
  call expect_pack('599 XYZ',1,2,-1,-1,-1)
  call expect_pack('599 0AB',2,-1,-1,-1,-1)
  call expect_pack('CQ K1ABC CQ!',3,-1,-1,-1,-1)
  call expect_pack('TEST WB9XYZ',2,3,-1,0,1)
  call expect_pack('HI WB9XYZ',2,3,-1,3,-1)
  call expect_pack('K1ABC HELLO',2,0,1,3,-1)
  call expect_pack('TEST AGN NR',2,3,-1,2,-1)
  call expect_pack(repeat('A',80),16,3,-1,3,-1)
  call expect_pack('',0,-1,-1,-1,-1)
  call expect_pack('   ',0,-1,-1,-1,-1)
  call expect_pack('599 A',1,3,-1,-1,-1)
  call expect_pack('599 BRUCE F',3,-1,-1,-1,-1)
  call expect_pack('A599 MA',2,3,-1,3,-1)
  call expect_pack('VP2/KF2GHI',2,3,-1,3,-1)
  call expect_pack('VP2/KA1ABC',2,3,-1,3,-1)
  call expect_pack('QU1RK',1,3,-1,-1,-1)
  call expect_pack('TU QU1RKP CQ',3,2,-1,3,-1)
  call expect_pack('WB9XYZABC',2,3,-1,3,-1)
  call expect_pack('K1A A',1,3,-1,-1,-1)
  call expect_pack('PSE AGN NR',2,-1,-1,-1,-1)
  call expect_pack('AGN NR PSE',2,2,-1,3,-1)
  call expect_control_phrase_literals()
  call expect_atom('599 123',jtty_exch_num_atom(JTTY_ROLE_FULL,JTTY_NUM_GENERIC,123))
  call expect_atom('123',jtty_exch_num_atom(JTTY_ROLE_FIELD_ONLY,JTTY_NUM_GENERIC,123))
  call expect_atom('599 MA',jtty_exch_loc_atom(JTTY_ROLE_FULL,JTTY_LOC_QTH,'MA'))
  call expect_atom('599 FN42',jtty_grid4_atom(JTTY_ROLE_FULL,'FN42'))
  call expect_exchange_profiles()
  ! Normalization is part of the round-trip contract for operator input.
  call expect_pack('cq  ka1abc   cq',1,0,0,-1,-1)
  call expect_pack('  vp2/kf2ghi  ',2,3,-1,3,-1)
  call expect_pack('A'//char(0)//'B',1,3,-1,-1,-1)
  call expect_pack('A~B',1,3,-1,-1,-1)
  call expect_pack('HELLO~',1,3,-1,-1,-1)
  call expect_pack('HELLO'//char(9)//'WORLD',3,3,-1,3,-1)
  call expect_pack('HELLO'//char(10)//'WORLD',3,3,-1,3,-1)
  call expect_pack('HELLO'//char(13)//'WORLD',3,3,-1,3,-1)
  call expect_unassigned_unpack_empty(1,2)
  call expect_unassigned_unpack_empty(1,3)
  call expect_structured_unpack_boundary()
  call expect_empty_waveform_guard()
  call expect_last_frame_flag()

contains

  subroutine expect_atom(text,atom,exchange_profile)
    character(len=*), intent(in) :: text
    type(jtty_source_atom), intent(in) :: atom
    integer, intent(in), optional :: exchange_profile
    character(len=80) :: input
    character(len=34) :: frames(MAX_FRAMES),expected_frame
    integer :: nframes
    logical :: valid

    input=text
    call pack_jtty(input,frames,nframes,exchange_profile)
    call pack_jtty_atom(atom,expected_frame,.true.,valid)
    if(.not.valid .or. nframes.ne.1 .or. frames(1).ne.expected_frame) then
       write(*,'(a)') 'Unexpected inferred atom for "'//text//'"'
       error stop 1
    endif
  end subroutine expect_atom

  subroutine expect_exchange_profiles()
    character(len=80), parameter :: examples(*)=[character(len=80) :: &
         '599 001','K1ABC 599 001','599 05','599 0123','001','599 123', &
         '599 MA','599 131071','599 0AB','1D EMA','CQ K1ABC CQ','K1A A']
    character(len=80) :: input,decoded
    character(len=34) :: baseline(MAX_FRAMES),frames(MAX_FRAMES)
    integer :: i,profile,baseline_nf,nframes

    call expect_pack('599 001',1,2,-1,-1,-1,JTTY_EXCHANGE_RTTY)
    call expect_pack('K1ABC 599 001',2,0,1,2,-1,JTTY_EXCHANGE_RTTY)
    call expect_pack('K1ABC 599 001',3,0,1,-1,-1,JTTY_EXCHANGE_UNKNOWN)
    call expect_pack('599 05',1,2,-1,-1,-1,JTTY_EXCHANGE_RTTY,'599 005')
    call expect_pack('599 0123',1,2,-1,-1,-1,JTTY_EXCHANGE_RTTY,'599 123')
    call expect_pack('K1ABC 599 05 QSL TU',3,0,1,2,-1,JTTY_EXCHANGE_RTTY,'K1ABC 599 005 QSL TU')
    call expect_pack('599 05 599 0123',2,2,-1,2,-1,JTTY_EXCHANGE_RTTY,'599 005 599 123')
    call expect_pack('599 000005',1,2,-1,-1,-1,JTTY_EXCHANGE_RTTY,'599 005')
    call expect_pack('599 0',1,2,-1,-1,-1,JTTY_EXCHANGE_RTTY,'599 000')
    call expect_pack('599 0000005',3,-1,-1,-1,-1,JTTY_EXCHANGE_RTTY)
    call expect_pack('599 131072',2,-1,-1,-1,-1,JTTY_EXCHANGE_RTTY)
    call expect_pack('599 05A',2,-1,-1,-1,-1,JTTY_EXCHANGE_RTTY)
    call expect_pack('A599 05',2,3,-1,3,-1,JTTY_EXCHANGE_RTTY)
    call expect_pack('599 05!',2,-1,-1,-1,-1,JTTY_EXCHANGE_RTTY)
    call expect_pack('05',1,3,-1,-1,-1,JTTY_EXCHANGE_RTTY)
    call expect_pack('599 05',2,-1,-1,-1,-1,JTTY_EXCHANGE_FIELD_DAY)
    call expect_pack(repeat('A',72)//' 599 05',16,3,-1,3,-1,JTTY_EXCHANGE_RTTY, &
         repeat('A',72)//' 599 005')
    call expect_pack(repeat('A',64)//' 599 05 599 0123',15,3,-1,3,-1,JTTY_EXCHANGE_RTTY, &
         repeat('A',64)//' 599 005 599 123')
    call expect_pack('001',1,3,-1,-1,-1,JTTY_EXCHANGE_RTTY)
    call expect_atom('599 001',jtty_exch_num_atom(JTTY_ROLE_FULL,JTTY_NUM_SERIAL,1),JTTY_EXCHANGE_RTTY)
    call expect_atom('599 123',jtty_exch_num_atom(JTTY_ROLE_FULL,JTTY_NUM_SERIAL,123),JTTY_EXCHANGE_RTTY)
    call expect_atom('599 MA',jtty_exch_loc_atom(JTTY_ROLE_FULL,JTTY_LOC_STATE_PROVINCE,'MA'),JTTY_EXCHANGE_RTTY)
    call expect_atom('123',jtty_exch_num_atom(JTTY_ROLE_FIELD_ONLY,JTTY_NUM_GENERIC,123),JTTY_EXCHANGE_RTTY)
    call expect_atom('599 123',jtty_exch_num_atom(JTTY_ROLE_FULL,JTTY_NUM_GENERIC,123),JTTY_EXCHANGE_FIELD_DAY)
    call expect_atom('599 MA',jtty_exch_loc_atom(JTTY_ROLE_FULL,JTTY_LOC_QTH,'MA'),JTTY_EXCHANGE_FIELD_DAY)

    do i=1,size(examples)
       input=examples(i)
       call pack_jtty(input,baseline,baseline_nf)
       do profile=JTTY_EXCHANGE_UNKNOWN,JTTY_EXCHANGE_RTTY
          input=examples(i)
          call pack_jtty(input,frames,nframes,profile)
          if(nframes.lt.1 .or. nframes.gt.baseline_nf) error stop 'Profile worsened frame count'
          if(profile.ne.JTTY_EXCHANGE_RTTY) then
             if(nframes.ne.baseline_nf .or. any(frames.ne.baseline)) error stop 'Unexpected non-RTTY inference'
          endif
          call unpack_jtty(frames,nframes,decoded)
          call display_jtty_message(decoded)
          if(decoded.ne.input) error stop 'Profile changed rendered text'
       enddo
       input=examples(i)
       call pack_jtty(input,frames,nframes)
       if(nframes.ne.baseline_nf .or. any(frames.ne.baseline)) error stop 'Profile leaked between calls'
    enddo
    input='599 001'
    call pack_jtty(input,frames,nframes,-1)
    if(nframes.ne.-1 .or. any(frames.ne.'')) error stop 'Accepted invalid negative profile'
    call pack_jtty(input,frames,nframes,JTTY_EXCHANGE_RTTY+1)
    if(nframes.ne.-1 .or. any(frames.ne.'')) error stop 'Accepted unknown profile'
    input=repeat('A',73)//' 599 05'
    call pack_jtty(input,frames,nframes,JTTY_EXCHANGE_RTTY)
    if(nframes.ne.-1 .or. any(frames.ne.'')) error stop 'Accepted canonical text exceeding 80 characters'
  end subroutine expect_exchange_profiles

  subroutine expect_control_phrase_literals()
    type(jtty_source_atom) atom
    character*80 input,decoded,expected
    character*34 frame,frames(MAX_FRAMES)
    integer phrase_id,got_nf
    logical valid,eom

    do phrase_id=lbound(CONTROL_TEXT,1),ubound(CONTROL_TEXT,1)
       input=''
       if(phrase_id.eq.JTTY_CONTROL_AGN_NR) then
          input='  agn   nr  '
       else
          input=trim(CONTROL_TEXT(phrase_id))
       endif
       call normalize_jtty_message(input,expected)
       call pack_jtty(input,frames,got_nf)
       if(got_nf.ne.1) then
          write(*,1030) trim(expected),got_nf
1030      format('Control phrase "',a,'" encoded in ',i0,' frames')
          error stop 1
       endif
       frame=frames(1)
       call unpack_jtty_atom(frame,atom,valid,eom)
       if(.not.valid .or. .not.eom .or. atom%kind.ne.JTTY_ATOM_CONTROL .or. &
            atom%subtype.ne.phrase_id .or. frames(1)(33:33).ne.'0') then
          write(*,1040) trim(expected),phrase_id
1040      format('Control phrase "',a,'" did not encode as CONTROL ',i0)
          error stop 1
       endif
       call unpack_jtty(frames,got_nf,decoded)
       call display_jtty_message(decoded)
       if(decoded.ne.expected) then
          write(*,1050) trim(expected),trim(decoded)
1050      format('Control phrase "',a,'" decoded as "',a,'"')
          error stop 1
       endif
    enddo
  end subroutine expect_control_phrase_literals

  subroutine display_jtty_message(text)
    character*80 text

    do i=1,len_trim(text)
       if(text(i:i).eq.'~') text(i:i)=' '
    enddo
  end subroutine display_jtty_message

  subroutine expect_pack(text,want_nf,want_i2a,want_n2a,want_i2b,want_n2b,exchange_profile,canonical_text)
    character*(*) text
    character*80 input,decoded,want_decoded,part,incremental
    character*34 frames(MAX_FRAMES),single(MAX_FRAMES)
    integer want_nf,want_i2a,want_n2a,want_i2b,want_n2b
    integer, intent(in), optional :: exchange_profile
    character(len=*), intent(in), optional :: canonical_text
    integer got_nf,got_i2a,got_n2a,got_i2b,got_n2b,iframe
    logical trailing_sep,is_last,valid

    input=''
    input=text
    call normalize_jtty_message(input,want_decoded)
    if(present(canonical_text)) want_decoded=canonical_text
    frames=''
    call pack_jtty(input,frames,got_nf,exchange_profile)
    if(input.ne.want_decoded) error stop 'Returned message differs from canonical text'
    if(got_nf.lt.0) then
       write(*,1195) trim(input)
1195   format('Unexpected pack failure for "',a,'"')
       error stop 1
    endif
    call expect_no_reserved_frame_ids(frames,got_nf,input)
    call unpack_jtty(frames,got_nf,decoded)
    call display_jtty_message(decoded)
    if(decoded.ne.want_decoded) then
       write(*,1200) trim(want_decoded),trim(decoded)
1200   format('Round-trip failure for "',a,'"; decoded "',a,'"')
       error stop 1
    endif
    if(got_nf.ne.want_nf) then
       write(*,1210) trim(input),want_nf,got_nf
1210   format('Frame-count failure for "',a,'"; wanted ',i0,' got ',i0)
       error stop 1
    endif
    incremental=''
    do iframe=1,got_nf
       single=''
       single(1)=frames(iframe)
       call unpack_jtty(single,1,part,trailing_sep,is_last,valid)
       if(.not.valid .or. (is_last.neqv.(iframe.eq.got_nf))) error stop 'Invalid incremental frame'
       incremental=trim(incremental)//trim(part)
       if(trailing_sep .and. iframe.lt.got_nf) incremental=trim(incremental)//'~'
       if(frames(iframe)(33:33).ne.'0') error stop 'Nonzero reserved bit'
    enddo
    call display_jtty_message(incremental)
    if(incremental.ne.want_decoded) error stop 'Incremental text differs from whole-message text'
    if(got_nf.eq.0) return
    read(frames(1),'(28x,2b2)') got_n2a,got_i2a
    if(want_i2a.ge.0 .and. got_i2a.ne.want_i2a) then
       write(*,1220) trim(input),want_i2a,got_i2a
1220   format('First-frame i2 failure for "',a,'"; wanted ',i0,' got ',i0)
       error stop 1
    endif
    if(want_n2a.ge.0 .and. got_n2a.ne.want_n2a) then
       write(*,1230) trim(input),want_n2a,got_n2a
1230   format('First-frame n2 failure for "',a,'"; wanted ',i0,' got ',i0)
       error stop 1
    endif
    if(want_i2b.ge.0) then
       read(frames(2),'(28x,2b2)') got_n2b,got_i2b
       if(got_i2b.ne.want_i2b) then
          write(*,1240) trim(input),want_i2b,got_i2b
1240      format('Second-frame i2 failure for "',a,'"; wanted ',i0,' got ',i0)
          error stop 1
       endif
       if(want_n2b.ge.0 .and. got_n2b.ne.want_n2b) then
          write(*,1250) trim(input),want_n2b,got_n2b
1250      format('Second-frame n2 failure for "',a,'"; wanted ',i0,' got ',i0)
          error stop 1
       endif
    endif
  end subroutine expect_pack

  subroutine expect_no_reserved_frame_ids(frames,nframes,input)
    character*34 frames(MAX_FRAMES)
    character*(*) input
    integer nframes, iframe, got_i2, got_n2

    do iframe=1,nframes
       read(frames(iframe),'(28x,2b2)') got_n2, got_i2
       if(got_i2.eq.1 .and. got_n2.ge.2) then
          write(*,1260) trim(input),iframe,got_i2,got_n2
1260      format('Reserved frame id for "',a,'" frame ',i0,': ',i0,'.',i0)
          error stop 1
       endif
    enddo
  end subroutine expect_no_reserved_frame_ids

  subroutine expect_unassigned_unpack_empty(i2,n2)
    character*34 frames(MAX_FRAMES)
    character*80 decoded
    character*13 c13
    integer i2,n2,n28,n32

    frames=''
    c13='K1ABC        '
    call pack28(c13,n28)
    n32=shiftl(n28,4) + 4*n2 + i2
    write(frames(1),'(b32.32,a2)') n32,'00'
    call unpack_jtty(frames,1,decoded)
    if(len_trim(decoded).ne.0) then
       write(*,1280) i2,n2,trim(decoded)
1280   format('Unassigned frame ',i0,'.',i0,' decoded unexpectedly as "',a,'"')
       error stop 1
    endif
  end subroutine expect_unassigned_unpack_empty

  subroutine expect_structured_unpack_boundary()
    character*34 frames(MAX_FRAMES)
    character*80 decoded,expected
    character*13 c13
    integer iframe,n28,n32,n30

    ! A long structured append that straddles column 80 must be truncated exactly.
    frames=''
    n30=0
    do i=1,5
       n30=64*n30 + jchar('A')
    enddo
    n32=ishft(n30,2) + 3
    do iframe=1,14
       write(frames(iframe),'(b32.32,a2)') n32,'00'
    enddo
    c13='KA1ABC       '
    call pack28(c13,n28)
    n32=shiftl(n28,4) + 4*1 + 1
    write(frames(15),'(b32.32,a2)') n32,'00'

    expected=''
    do i=1,70
       expected(i:i)='A'
    enddo
    expected(71:80)='TU NOW KA1'
    call unpack_jtty(frames,15,decoded)
    if(len_trim(decoded).ne.80 .or. decoded.ne.expected) then
       write(*,1300) len_trim(decoded),trim(decoded)
1300   format('Structured-boundary unpack test decoded length ',i0,' as "',a,'"')
       error stop 1
    endif
  end subroutine expect_structured_unpack_boundary

  subroutine expect_empty_waveform_guard()
    integer, parameter :: TEST_NSPS=4*384
    integer tones(1)
    integer nsym, nsps, icmplx, nwave
    real bt, fsample, f0
    real wave(TEST_NSPS)
    complex cwave(TEST_NSPS)

    tones=0
    nsym=0
    nsps=TEST_NSPS
    bt=2.0
    fsample=48000.0
    f0=1500.0
    icmplx=0
    nwave=nsps

    call gen_jttywave(tones,nsym,nsps,bt,fsample,f0,cwave,wave,icmplx,nwave)
    if(nwave.ne.0) then
       write(*,1310) nwave
1310   format('Empty waveform guard returned nwave ',i0)
       error stop 1
    endif
  end subroutine expect_empty_waveform_guard

  subroutine expect_last_frame_flag()
    ! Bit 34 of each 34-bit frame word must be 0 on every frame except the
    ! last, and 1 on the last frame of a multi-frame message -- matching how
    ! the real decode drivers call unpack_jtty one frame at a time.
    character*80 input, decoded
    character*34 frames(MAX_FRAMES)
    integer got_nf, iframe
    logical is_last

    input='TU NOW JA6DEF 599 123'
    frames=''
    call pack_jtty(input,frames,got_nf)
    if(got_nf.lt.2) then
       write(*,1400) got_nf
1400   format('expect_last_frame_flag: expected >=2 frames, got ',i0)
       error stop 1
    endif
    do iframe=1,got_nf
       call unpack_jtty(frames(iframe:iframe),1,decoded,is_last_frame=is_last)
       if(iframe.lt.got_nf .and. is_last) then
          write(*,1410) iframe
1410      format('expect_last_frame_flag: frame ',i0,' unexpectedly flagged as last')
          error stop 1
       endif
       if(iframe.eq.got_nf .and. .not.is_last) then
          write(*,1420) iframe
1420      format('expect_last_frame_flag: final frame ',i0,' not flagged as last')
          error stop 1
       endif
    enddo
  end subroutine expect_last_frame_flag

end program test_jtty_pack
