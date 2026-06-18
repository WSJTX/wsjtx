! Phase 9 parse-unit regression driver. Phase 9 unpacks two bit-packed legacy ints
! into 9 wire keys; parse_control_frame folds them BACK into the derived members
! cfg%ndepth_packed / cfg%nexp_decode_packed (the post-pass pack_phase9_), so this
! driver is the authoritative guard for the PACKING (each key -> its bit), the
! contest_type -> ncontest table (incl. the SpecOp 5/8/9 -> 1 collapse), the two
! atomic-decline errors (legacy depth co-present with any unpacked ndepth key; a
! depth_level vs q65_maxiters_level inconsistency), the bit-width MASKING of
! depth_level/q65_maxiters_level, and the noise_blanker -3 offset + [0,255] clamp.
!
! WHY a Fortran unit driver (not only fixtures through jt9): only depth_level is
! decode-observable on FT8/JT9 (Gate 24); the other 8 keys are inert (Q65/JT65/
! FST4/JT4 or contest-branch only), and the bit-PACKING of even
! depth_level is invisible to a decode-count gate (ndepth=3 and ndepth=3|16 both
! decode 21 on FT8). This driver + run-apply-routing-unit.sh (the packed-int APPLY
! routing) + no-regression are the coverage for the 8 inert keys, exactly like the
! Phase 4/6/7 inert fields. Compiled + run by run-phase9-parse-unit.sh.
program p9_parse_test
  use streaming_control, only: parse_control_frame, configure_fields,        &
       control_type_error, CTRL_CONFIGURE
  implicit none
  integer :: nfail
  nfail = 0

  call frame_A()
  call frame_B()
  call frame_C()
  call frame_D()
  call frame_E()
  call frame_F()
  call frame_G()
  call frame_H()
  call frame_I()
  call frame_J()
  call frame_K()
  call frame_L()
  call frame_M()

  write(*,'(a)') '------------------------------------------------------------'
  if (nfail .eq. 0) then
     write(*,'(a)') 'ALL PHASE 9 PARSE-UNIT CHECKS PASSED'
  else
     write(*,'(a,i0,a)') 'PHASE 9 PARSE-UNIT: ', nfail, ' CHECK(S) FAILED'
     call exit(1)
  end if

contains

  subroutine ok(label, cond)
    character(len=*), intent(in) :: label
    logical,          intent(in) :: cond
    if (cond) then
       write(*,'(a,a)') '  PASS  ', label
    else
       write(*,'(a,a)') '  FAIL  ', label
       nfail = nfail + 1
    end if
  end subroutine ok

  ! Frame A: ndepth bit-pack. depth_level=2 (bits0-2), use_averaging (0x10),
  ! deep_ap_search (0x20), q65_auto_clear_average (0x80) -> ndepth = 2|16|32|128 =
  ! 178. Asserts each bit lands (a dropped/mis-shifted bit changes the total).
  subroutine frame_A()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","depth_level":2,"use_averaging":true,' //                  &
      '"deep_ap_search":true,"q65_auto_clear_average":true}'
    write(*,'(a)') 'Frame A: ndepth bit-pack (2 | 0x10 | 0x20 | 0x80 = 178)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('action == CTRL_CONFIGURE',  action .eq. CTRL_CONFIGURE)
    call ok('terr not present',           .not. terr%present)
    call ok('ndepth_packed set',          cfg%ndepth_packed_set)
    call ok('ndepth_packed == 178',       cfg%ndepth_packed .eq. 178)
    ! Individual bits, so a single dropped key surfaces:
    call ok('bit 0-2 (depth_level=2)',    iand(cfg%ndepth_packed, 7)  .eq. 2)
    call ok('bit 4 (use_averaging)',      iand(cfg%ndepth_packed, 16) .eq. 16)
    call ok('bit 5 (deep_ap_search)',     iand(cfg%ndepth_packed, 32) .eq. 32)
    call ok('bit 7 (q65_auto_clear_avg)', iand(cfg%ndepth_packed, 128).eq. 128)
    call ok('bit 3 unused (clear)',       iand(cfg%ndepth_packed, 8)  .eq. 0)
    call ok('bit 6 unused (clear)',       iand(cfg%ndepth_packed, 64) .eq. 0)
    call ok('nexp_decode NOT packed (no nexp key)', .not. cfg%nexp_decode_packed_set)
  end subroutine frame_A

  ! Frame B: contest_type -> ncontest table (incl. SpecOp/collapse).
  ! Each string parsed in its own frame. The 5/8/9 -> 1 collapse is the key risk.
  subroutine frame_B()
    write(*,'(a)') 'Frame B: contest_type -> ncontest table (incl. SpecOp 5/8/9 collapse)'
    call con_ok('none -> 0',       'none',       0)
    call con_ok('na_vhf -> 1',     'na_vhf',     1)
    call con_ok('eu_vhf -> 2',     'eu_vhf',     2)
    call con_ok('field_day -> 3',  'field_day',  3)
    call con_ok('rtty -> 4',       'rtty',       4)
    call con_ok('ww_digi -> 1 (SpecOp 5 collapsed)',    'ww_digi',    1)
    call con_ok('fox -> 6',        'fox',        6)
    call con_ok('hound -> 7',      'hound',      7)
    call con_ok('arrl_digi -> 1 (SpecOp 8 collapsed)',  'arrl_digi',  1)
    call con_ok('q65_pileup -> 1 (SpecOp 9 collapsed)', 'q65_pileup', 1)
  end subroutine frame_B

  ! Helper: parse {"t":"configure","contest_type":"<s>"} and assert the collapsed
  ! ncontest lands in nexp_decode_packed bits 0-2 (and contest_type member).
  subroutine con_ok(label, s, want)
    character(len=*), intent(in) :: label, s
    integer,          intent(in) :: want
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=80) :: b
    b = '{"t":"configure","contest_type":"' // trim(s) // '"}'
    call parse_control_frame(trim(b), action, cfg, terr)
    call ok(label, (.not. terr%present) .and. cfg%contest_type_set .and.        &
         cfg%contest_type .eq. want .and. cfg%nexp_decode_packed_set .and.       &
         iand(cfg%nexp_decode_packed, 7) .eq. want)
  end subroutine con_ok

  ! Frame C: nexp_decode full pack. contest_type=fox (6) | single_decode (0x20) |
  ! vhf_features (0x40) | noise_blanker_level=2 ((2+3)<<8 = 1280) -> 6+32+64+1280 =
  ! 1382. Asserts each sub-field lands in its slot.
  subroutine frame_C()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","contest_type":"fox","single_decode":true,' //             &
      '"vhf_features":true,"noise_blanker_level":2}'
    write(*,'(a)') 'Frame C: nexp_decode full pack (6 | 0x20 | 0x40 | (2+3)<<8 = 1382)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',             .not. terr%present)
    call ok('nexp_decode_packed == 1382',   cfg%nexp_decode_packed .eq. 1382)
    call ok('bits 0-2 (ncontest fox=6)',    iand(cfg%nexp_decode_packed, 7)  .eq. 6)
    call ok('bit 5 (single_decode)',        iand(cfg%nexp_decode_packed, 32) .eq. 32)
    call ok('bit 6 (vhf_features)',         iand(cfg%nexp_decode_packed, 64) .eq. 64)
    call ok('upper byte (nb+3 = 5)',        cfg%nexp_decode_packed / 256 .eq. 5)
    call ok('ndepth NOT packed (no ndepth key)', .not. cfg%ndepth_packed_set)
  end subroutine frame_C

  ! Frame D: depth_level vs q65_maxiters_level consistency (bits 0-1
  ! overlap). (a) consistent pair (depth_level=6 low2=2, q65_maxiters=2) -> OK,
  ! ndepth bits0-2 = 6. (b) inconsistent pair -> configure_type_error.
  subroutine frame_D()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: ba =                                      &
      '{"t":"configure","depth_level":6,"q65_maxiters_level":2}'
    character(len=*), parameter :: bb =                                      &
      '{"t":"configure","depth_level":6,"q65_maxiters_level":1}'
    write(*,'(a)') 'Frame D: depth_level/q65_maxiters_level consistency (bits 0-1 overlap)'
    call parse_control_frame(ba, action, cfg, terr)
    call ok('(a) consistent (6 low2 == 2): no error',  .not. terr%present)
    call ok('(a) ndepth_packed == 6',                  cfg%ndepth_packed .eq. 6)
    call parse_control_frame(bb, action, cfg, terr)
    call ok('(b) inconsistent (6 low2=2 != 1): terr present', terr%present)
    call ok('(b) key == q65_maxiters_level',  trim(terr%key) .eq. 'q65_maxiters_level')
    call ok('(b) expected == match depth_lvl', trim(terr%expected) .eq. 'match depth_lvl')
    call ok('(b) got == conflict',            trim(terr%got) .eq. 'conflict')
    call ok('(b) ndepth NOT packed (declined)', .not. cfg%ndepth_packed_set)
  end subroutine frame_D

  ! Frame E: legacy depth co-present with an unpacked ndepth key -> conflict
  ! configure_type_error (the two encodings are mutually exclusive).
  subroutine frame_E()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","depth":3,"depth_level":1}'
    write(*,'(a)') 'Frame E: depth + depth_level conflict -> type error'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr present',             terr%present)
    call ok('key == ndepth',            trim(terr%key) .eq. 'ndepth')
    call ok('expected == depth|unpacked', trim(terr%expected) .eq. 'depth|unpacked')
    call ok('got == conflict',          trim(terr%got) .eq. 'conflict')
    call ok('ndepth NOT packed (declined)', .not. cfg%ndepth_packed_set)
  end subroutine frame_E

  ! Frame F: bit-width MASKING. An out-of-range depth_level=99 (0x63) must mask to
  ! its 3 bits (99 & 7 = 3) so it cannot corrupt sibling bits (4/5/7). Likewise a
  ! lone q65_maxiters_level=99 masks to 2 bits (99 & 3 = 3). No crash.
  subroutine frame_F()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: ba = '{"t":"configure","depth_level":99}'
    character(len=*), parameter :: bb = '{"t":"configure","q65_maxiters_level":99}'
    write(*,'(a)') 'Frame F: out-of-range depth_level/q65_maxiters masked to bit width'
    call parse_control_frame(ba, action, cfg, terr)
    call ok('(a) depth_level=99 -> ndepth = 3 (99 & 7)', cfg%ndepth_packed .eq. 3)
    call ok('(a) no sibling bits set',  iand(cfg%ndepth_packed, 240) .eq. 0)
    call ok('(a) no type error',        .not. terr%present)
    call parse_control_frame(bb, action, cfg, terr)
    call ok('(b) q65_maxiters=99 -> ndepth = 3 (99 & 3)', cfg%ndepth_packed .eq. 3)
  end subroutine frame_F

  ! Frame G: q65_maxiters_level alone (no depth_level) -> derive depth_level's
  ! bits 0-1 from it (receiver-side derivation). q65_maxiters=3 -> ndepth=3.
  subroutine frame_G()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","q65_maxiters_level":3,"use_averaging":true}'
    write(*,'(a)') 'Frame G: q65_maxiters_level alone -> ndepth bits 0-1 derived'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',           .not. terr%present)
    call ok('ndepth_packed == 3|16 = 19', cfg%ndepth_packed .eq. 19)
    call ok('bits 0-1 = q65_maxiters 3',  iand(cfg%ndepth_packed, 3) .eq. 3)
  end subroutine frame_G

  ! Frame H: noise_blanker_level -3 offset + [0,255] clamp (upper byte).
  ! nb=0 -> (0+3)<<8 = 768; nb=-3 -> 0; nb=300 -> clamp 255 -> 255<<8 = 65280.
  ! Each still sets nexp_decode_packed_set (the key was present).
  subroutine frame_H()
    write(*,'(a)') 'Frame H: noise_blanker_level offset + clamp'
    call nb_ok('nb=0 -> upper byte 3 (768)',     0,   768)
    call nb_ok('nb=2 -> upper byte 5 (1280)',    2,   1280)
    call nb_ok('nb=-3 -> upper byte 0 (clamp)',  -3,  0)
    call nb_ok('nb=300 -> upper byte 255 (clamp)', 300, 65280)
  end subroutine frame_H

  subroutine nb_ok(label, nb, want)
    character(len=*), intent(in) :: label
    integer,          intent(in) :: nb, want
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=16) :: ns
    character(len=64) :: b
    write(ns, '(i0)') nb
    b = '{"t":"configure","noise_blanker_level":' // trim(ns) // '}'
    call parse_control_frame(trim(b), action, cfg, terr)
    call ok(label, (.not. terr%present) .and. cfg%nexp_decode_packed_set .and.  &
         cfg%nexp_decode_packed .eq. want)
  end subroutine nb_ok

  ! Frame I: wrong-TYPE detection. depth_level as a string (int expected) and
  ! contest_type as a number (string expected) -> check_type_ rejects.
  subroutine frame_I()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b1 = '{"t":"configure","depth_level":"deep"}'
    character(len=*), parameter :: b2 = '{"t":"configure","contest_type":5}'
    character(len=*), parameter :: b3 = '{"t":"configure","use_averaging":7}'
    write(*,'(a)') 'Frame I: wrong-type Phase 9 keys -> type error'
    call parse_control_frame(b1, action, cfg, terr)
    call ok('depth_level string: terr present',    terr%present)
    call ok('depth_level string: key',     trim(terr%key) .eq. 'depth_level')
    call ok('depth_level string: expected', trim(terr%expected) .eq. 'int')
    call ok('depth_level string: got',     trim(terr%got) .eq. 'string')
    call ok('depth_level string: ndepth NOT packed', .not. cfg%ndepth_packed_set)
    call parse_control_frame(b2, action, cfg, terr)
    call ok('contest_type number: terr present',   terr%present)
    call ok('contest_type number: key',    trim(terr%key) .eq. 'contest_type')
    call ok('contest_type number: expected', trim(terr%expected) .eq. 'string')
    call parse_control_frame(b3, action, cfg, terr)
    call ok('use_averaging number: terr present',  terr%present)
    call ok('use_averaging number: expected bool', trim(terr%expected) .eq. 'bool')
  end subroutine frame_I

  ! Frame J: unknown contest_type string -> value-domain SILENT SKIP (left unset,
  ! NO type error — mirrors the unknown-mode-name skip). nexp not packed.
  subroutine frame_J()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b =                                       &
      '{"t":"configure","contest_type":"sweepstakes","mode":"FT8"}'
    write(*,'(a)') 'Frame J: unknown contest_type -> silent skip (no false decline)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',         .not. terr%present)
    call ok('contest_type stays UNSET', .not. cfg%contest_type_set)
    call ok('nexp NOT packed',          .not. cfg%nexp_decode_packed_set)
    call ok('mode still parsed (FT8)',  cfg%mode_set .and. cfg%mode .eq. 8)
  end subroutine frame_J

  ! Frame K: value-vs-key aliasing. The mycall VALUE is the literal
  ! token "depth_level" (11 chars, fits mycall len=12) with no real depth_level
  ! key. The anchored locator must NOT mistake the value for the key.
  subroutine frame_K()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mycall":"depth_level"}'
    write(*,'(a)') 'Frame K: mycall value == "depth_level" token (aliasing guard)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present (no false decline)', .not. terr%present)
    call ok('mycall == depth_level',  cfg%mycall_set .and. trim(cfg%mycall) .eq. 'depth_level')
    call ok('depth_level stays UNSET', .not. cfg%depth_level_set)
    call ok('ndepth NOT packed',       .not. cfg%ndepth_packed_set)
  end subroutine frame_K

  ! Frame L: Phase 9 keys absent -> all unset, no error, no packing (keys optional).
  subroutine frame_L()
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=*), parameter :: b = '{"t":"configure","mode":"FT8","depth":3}'
    write(*,'(a)') 'Frame L: Phase 9 keys absent -> unset (legacy depth alone OK)'
    call parse_control_frame(b, action, cfg, terr)
    call ok('terr not present',           .not. terr%present)
    call ok('depth (legacy) still set',   cfg%depth_set .and. cfg%depth .eq. 3)
    call ok('depth_level unset',          .not. cfg%depth_level_set)
    call ok('contest_type unset',         .not. cfg%contest_type_set)
    call ok('ndepth NOT packed',          .not. cfg%ndepth_packed_set)
    call ok('nexp_decode NOT packed',     .not. cfg%nexp_decode_packed_set)
  end subroutine frame_L

  ! Frame M: per-bool SINGLE-BIT isolation (closes the swap-blindness where
  ! two bool keys that only ever appear together-all-true can be
  ! swapped invisibly: Frame A sets all 3 ndepth bools true, Frame C both nexp
  ! bools true, so a deep_ap_search<->q65_auto_clear_average (0x20<->0x80) or a
  ! single_decode<->vhf_features (0x20<->0x40) swap leaves the total unchanged).
  ! Each bool set ALONE asserts EXACTLY its bit, so every bool->bit assignment is
  ! now independently load-bearing. (use_averaging is also disambiguated by Frame G,
  ! but assert it here too for symmetry.)
  subroutine frame_M()
    write(*,'(a)') 'Frame M: per-bool single-bit isolation (swap-blindness guard)'
    call bit_ok_ndepth('use_averaging alone -> ndepth 0x10',          'use_averaging',          16)
    call bit_ok_ndepth('deep_ap_search alone -> ndepth 0x20',         'deep_ap_search',         32)
    call bit_ok_ndepth('q65_auto_clear_average alone -> ndepth 0x80', 'q65_auto_clear_average', 128)
    call bit_ok_nexp('single_decode alone -> nexp 0x20', 'single_decode', 32)
    call bit_ok_nexp('vhf_features alone -> nexp 0x40',  'vhf_features',  64)
  end subroutine frame_M

  ! Helper: parse {"t":"configure","<key>":true} and assert ndepth_packed == want
  ! EXACTLY (so the bit isolated is the only one set; a mis-shift fails the equality).
  subroutine bit_ok_ndepth(label, key, want)
    character(len=*), intent(in) :: label, key
    integer,          intent(in) :: want
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=80) :: b
    b = '{"t":"configure","' // trim(key) // '":true}'
    call parse_control_frame(trim(b), action, cfg, terr)
    call ok(label, (.not. terr%present) .and. cfg%ndepth_packed_set .and.       &
         cfg%ndepth_packed .eq. want)
  end subroutine bit_ok_ndepth

  subroutine bit_ok_nexp(label, key, want)
    character(len=*), intent(in) :: label, key
    integer,          intent(in) :: want
    type(configure_fields)   :: cfg
    type(control_type_error) :: terr
    integer :: action
    character(len=80) :: b
    b = '{"t":"configure","' // trim(key) // '":true}'
    call parse_control_frame(trim(b), action, cfg, terr)
    call ok(label, (.not. terr%present) .and. cfg%nexp_decode_packed_set .and.  &
         cfg%nexp_decode_packed .eq. want)
  end subroutine bit_ok_nexp

end program p9_parse_test
