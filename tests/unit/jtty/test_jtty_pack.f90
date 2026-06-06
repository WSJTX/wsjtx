program test_jtty_pack

  use jtty_mod
  use packjt77, only: pack28
  character*80 msg0,msg,expected
  character*32 c32(MAX_FRAMES)
  character*17 cparms
  character*1 err
  logical norm_ok
  integer, parameter :: expected_errors = 0

  open(10,file='jtty_msgs.txt',status='old')
  write(*,1000)
1000 format('i2.n2 i2.n2 NC NF err Message'/87('-'))
  nerr=0
  nz=0
  c32=''
  do imsg=1,99
     read(10,'(a80)',end=100) msg0
     call normalize_jtty_message(msg0,expected,norm_ok)
     if(.not.norm_ok) then
        write(*,*) 'Fixture normalization failed: ', trim(msg0)
        error stop 1
     endif
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
  call expect_pack('WB9XYZ',1,0,1,-1,-1)
  ! A one-frame compact call wins the canonical tie over one free-text frame.
  call expect_pack('K1A',1,0,1,-1,-1)
  call expect_pack('TU KA1ABC CQ',1,0,2,-1,-1)
  call expect_pack('WB9XYZ TU',1,0,3,-1,-1)
  call expect_pack('WB9XYZ AGN?',1,1,0,-1,-1)
  call expect_pack('TU NOW JA6DEF 599 123',2,1,1,2,-1)
  ! Structured frames can be adjacent with only the implicit decoded separator.
  call expect_pack('WB9XYZ TU CQ KA1ABC CQ',2,0,3,0,0)
  ! DP should still compact a standalone call after an earlier free-text frame.
  call expect_pack('TEST WB9XYZ',2,3,-1,0,1)
  call expect_pack('599 MA',1,2,-1,-1,-1)
  ! Short 599 exchanges also prefer the compact frame on a one-frame tie.
  call expect_pack('599 A',1,2,-1,-1,-1)
  ! A 599 frame carries exactly five payload characters before later text.
  call expect_pack('599 BRUCE F',2,2,-1,3,-1)
  ! 599 is not compacted when it appears inside a larger token.
  call expect_pack('A599 MA',2,3,-1,3,-1)
  call expect_pack('VP2/KF2GHI',2,3,-1,3,-1)
  call expect_pack('VP2/KA1ABC',2,3,-1,3,-1)
  ! chkcall accepts Q placeholders that are not valid JTTY compact calls.
  call expect_pack('QU1RK',1,3,-1,-1,-1)
  call expect_pack('TU QU1RKP CQ',3,3,-1,3,-1)
  call expect_pack('WB9XYZABC',2,3,-1,3,-1)
  call expect_pack('K1A A',1,3,-1,-1,-1)
  ! Normalization is part of the round-trip contract for operator input.
  call expect_pack('cq  ka1abc   cq',1,0,0,-1,-1)
  call expect_pack('  vp2/kf2ghi  ',2,3,-1,3,-1)
  call expect_pack('A'//char(0)//'B',1,3,-1,-1,-1)
  call expect_pack('A~B',1,3,-1,-1,-1)
  call expect_pack('HELLO~',1,3,-1,-1,-1)
  call expect_pack_failure('HELLO'//char(9))
  call expect_unassigned_unpack_empty(1,2)
  call expect_unassigned_unpack_empty(1,3)
  call expect_unpack_overflow_guard()
  call expect_structured_unpack_boundary()

contains

  subroutine display_jtty_message(text)
    character*80 text

    do i=1,len_trim(text)
       if(text(i:i).eq.'~') text(i:i)=' '
    enddo
  end subroutine display_jtty_message

  subroutine expect_pack(text,want_nf,want_i2a,want_n2a,want_i2b,want_n2b)
    character*(*) text
    character*80 input,decoded,want_decoded
    character*32 frames(MAX_FRAMES)
    integer want_nf,want_i2a,want_n2a,want_i2b,want_n2b
    integer got_nf,got_i2a,got_n2a,got_i2b,got_n2b
    logical ok

    input=''
    input=text
    call normalize_jtty_message(input,want_decoded,ok)
    if(.not.ok) then
       write(*,1190) trim(input)
1190   format('Unexpected normalization failure for "',a,'"')
       error stop 1
    endif
    frames=''
    call pack_jtty(input,frames,got_nf)
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
    read(frames(1),'(28x,2b2)') got_n2a,got_i2a
    if(got_i2a.ne.want_i2a) then
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
    character*32 frames(MAX_FRAMES)
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

  subroutine expect_pack_failure(text)
    character*(*) text
    character*80 input
    character*32 frames(MAX_FRAMES)
    integer got_nf

    input=''
    input=text
    frames=''
    call pack_jtty(input,frames,got_nf)
    if(got_nf.ge.0) then
       write(*,1270) trim(input),got_nf
1270   format('Expected pack failure for "',a,'"; got ',i0,' frames')
       error stop 1
    endif
  end subroutine expect_pack_failure

  subroutine expect_unassigned_unpack_empty(i2,n2)
    character*32 frames(MAX_FRAMES)
    character*80 decoded
    character*13 c13
    integer i2,n2,n28,n32

    frames=''
    c13='K1ABC        '
    call pack28(c13,n28)
    n32=shiftl(n28,4) + 4*n2 + i2
    write(frames(1),'(b32.32)') n32
    call unpack_jtty(frames,1,decoded)
    if(len_trim(decoded).ne.0) then
       write(*,1280) i2,n2,trim(decoded)
1280   format('Unassigned frame ',i0,'.',i0,' decoded unexpectedly as "',a,'"')
       error stop 1
    endif
  end subroutine expect_unassigned_unpack_empty

  subroutine expect_unpack_overflow_guard()
    character*32 frames(MAX_FRAMES)
    character*80 decoded,expected
    integer iframe

    ! Sixteen 599 frames must decode exactly up to the fixed output boundary.
    do iframe=1,MAX_FRAMES
       write(frames(iframe),'(b32.32)') 2
    enddo
    expected=''
    do iframe=1,8
       expected((iframe-1)*9+1:iframe*9)='599 00000'
    enddo
    expected(73:80)='599 0000'
    call unpack_jtty(frames,MAX_FRAMES,decoded)
    if(len_trim(decoded).ne.80 .or. decoded.ne.expected) then
       write(*,1290) len_trim(decoded),trim(decoded)
1290   format('Overflow-guard unpack test decoded length ',i0,' as "',a,'"')
       error stop 1
    endif
  end subroutine expect_unpack_overflow_guard

  subroutine expect_structured_unpack_boundary()
    character*32 frames(MAX_FRAMES)
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
       write(frames(iframe),'(b32.32)') n32
    enddo
    c13='KA1ABC       '
    call pack28(c13,n28)
    n32=shiftl(n28,4) + 4*1 + 1
    write(frames(15),'(b32.32)') n32

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

end program test_jtty_pack
