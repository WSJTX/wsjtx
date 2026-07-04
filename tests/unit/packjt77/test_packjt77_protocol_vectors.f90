program test_packjt77_protocol_vectors

  use packjt77
  use packjt77_test_helpers
  implicit none

  integer :: ntests

  ntests=0

  call expect_hash_vector('K1ABC', 712, 2851, 2920267)
  call expect_hash_vector('W9XYZ', 972, 3889, 3982604)
  call expect_hash_vector('PJ4/K1ABC', 346, 1387, 1420834)
  call expect_hash_vector('KH1/KH7Z', 201, 806, 825805)
  call expect_hash_vector('K1JT/P', 959, 3839, 3931158)
  call expect_hash_vector('W3CCX', 665, 2662, 2726483)
  call expect_hash_vector('3DA0ABC', 48, 192, 196695)
  call expect_hash_vector('3XABC', 563, 2254, 2308721)
  call expect_hash_vector('///////////', 671, 2685, 2749801)
  call expect_hash_vector('ZZZZZZZZZZZ', 902, 3609, 3695718)
  call expect_hash_vector('99999999999', 762, 3050, 3123740)
  call expect_hash_vector('A23456789Z/', 122, 488, 499902)
  call expect_hash_vector('           ', 0, 0, 0)

  call expect_pack_bits('K1ABC W9XYZ FN42', 1, 0, -1, -1, &
       '00001001101111011110001101010000011000010100100111011100000010100001100110001')
  call expect_pack_bits('<PJ4/K1ABC> W9XYZ RR73', 1, 0, -1, -1, &
       '00000011010100101011000010100000011000010100100111011100000111111001110101001')
  call expect_pack_bits('K1ABC RR73; W9XYZ <KH1/KH7Z> -12', 0, 1, -1, -1, &
       '00001001101111011110001101010000110000101001001110111000001100100101001001000')
  call expect_pack_bits('PJ2/W1AW <W7ABC> RR73', 4, 0, -1, -1, &
       '00100100001100000000000000001011000010101110000110010100011010110100111100100')
  call expect_pack_bits('<W3CCX> <K1JT/P> 592047 RR99XX', 5, 0, -1, -1, &
       '10100110011011101111111100000101100111111111111111000111001100001111111111101')
  call expect_pack_bits('K1ABC FN42 37', 0, 6, -1, -1, &
       '00001001101111011110001101010101000011001100101100000000000000000000000110000')
  ! WSPR Type 2 prefix/suffix vectors need both exact bits and decoded text.
  call expect_pack_bits('PJ2/K1ABC 37', 0, 6, -1, -1, &
       '00001001101111011110001101011000000100111110010111000000000000000000000110000', &
       'PJ2/K1ABC 37')
  call expect_pack_bits('<PJ4/K1ABC> FK52AB', 0, 6, 0, 6, &
       '01010110101110001000100010111111101110100000101010000000000000000000000110000')

  call expect_cross_variant_bits('K1ABC W9XYZ FN42', -1, -1)
  call expect_cross_variant_bits('FREE TEXT MSG', -1, -1)
  call expect_cross_variant_bits('WA9XYZ KA1ABC R 16A EMA', -1, -1)
  call expect_cross_variant_bits('WA9XYZ KA1ABC R 32A EMA', -1, -1)
  call expect_cross_variant_bits('TU; W9XYZ G8ABC R 559 0013', -1, -1)
  call expect_cross_variant_bits('K1ABC RR73; W9XYZ <KH1/KH7Z> -12', -1, -1)
  call expect_cross_variant_bits('<PJ4/K1ABC> W9XYZ RR73', -1, -1)
  call expect_cross_variant_bits('PJ2/W1AW <W7ABC> RR73', -1, -1)
  call expect_cross_variant_bits('<W3CCX> <K1JT/P> 592047 RR99XX', -1, -1)
  call expect_cross_variant_bits('K1ABC FN42 37', -1, -1)
  call expect_cross_variant_bits('<PJ4/K1ABC> FK52AB', 0, 6)

  call expect_binary_payload_after_wspr_prefix(.false.)
  call expect_binary_payload_after_wspr_prefix(.true.)

  write(*,1000) ntests
1000 format('packjt77 protocol vector tests passed: ',i0)

contains

  subroutine expect_hash_vector(callsign,want10,want12,want22)
    character(len=*), intent(in) :: callsign
    integer, intent(in) :: want10, want12, want22
    character(len=13) :: c13
    integer :: got10, got12, got22

    c13='             '
    c13=callsign
    got10=ihashcall(c13,10)
    got12=ihashcall(c13,12)
    got22=ihashcall(c13,22)
    if(got10.ne.want10 .or. got12.ne.want12 .or. got22.ne.want22) then
       write(*,1010) trim(callsign), got10, got12, got22, want10, want12, want22
1010   format('Hash vector failure for "',a,'"; got ',i0,1x,i0,1x,i0, &
              ' wanted ',i0,1x,i0,1x,i0)
       error stop 1
    endif

    if(got10.lt.0 .or. got10.gt.1023 .or. got12.lt.0 .or. got12.gt.4095 .or. &
         got22.lt.0 .or. got22.gt.4194303) then
       write(*,1040) trim(callsign), got10, got12, got22
1040   format('Hash range failure for "',a,'"; got ',i0,1x,i0,1x,i0)
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_hash_vector

  subroutine expect_pack_bits(input,want_i3,want_n3,pack_i3,pack_n3,expected_c77, &
       expected_decode)
    character(len=*), intent(in) :: input, expected_c77
    character(len=*), intent(in), optional :: expected_decode
    integer, intent(in) :: want_i3, want_n3, pack_i3, pack_n3
    character(len=77) :: c77
    character(len=37) :: decoded
    integer :: got_i3, got_n3
    logical :: ok

    call pack_standard(input,pack_i3,pack_n3,got_i3,got_n3,c77)
    call assert_message_type('pack77',input,want_i3,want_n3,got_i3,got_n3)
    call assert_binary_payload('pack77',input,c77)
    if(c77.ne.expected_c77) then
       write(*,1030) trim(input), c77, expected_c77
1030   format('77-bit vector failure for "',a,'"; got ',a,' wanted ',a)
       error stop 1
    endif

    if(present(expected_decode)) then
       call reset_packjt77_state()
       decoded='                                     '
       ok=.false.
       call unpack77(c77,0,decoded,ok)
       call assert_decode('pack77 vector',input,expected_decode,decoded,ok)
    endif

    ntests=ntests+1
  end subroutine expect_pack_bits

  subroutine expect_cross_variant_bits(input,pack_i3,pack_n3)
    character(len=*), intent(in) :: input
    integer, intent(in) :: pack_i3, pack_n3
    character(len=77) :: c77, c77var
    integer :: got_i3, got_n3, got_i3var, got_n3var

    call pack_standard(input,pack_i3,pack_n3,got_i3,got_n3,c77)
    call pack_var(input,pack_i3,pack_n3,got_i3var,got_n3var,c77var)
    if(got_i3.ne.got_i3var .or. got_n3.ne.got_n3var) then
       write(*,1040) trim(input), got_i3, got_n3, got_i3var, got_n3var
1040   format('Cross-variant type failure for "',a,'"; pack77 got ',i0,'.',i0, &
              ' pack77var got ',i0,'.',i0)
       error stop 1
    endif
    call assert_binary_payload('pack77',input,c77)
    call assert_binary_payload('pack77var',input,c77var)
    if(c77.ne.c77var) then
       write(*,1050) trim(input), c77, c77var
1050   format('Cross-variant bit failure for "',a,'"; pack77 ',a,' pack77var ',a)
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_cross_variant_bits

  subroutine expect_binary_payload_after_wspr_prefix(use_var)
    logical, intent(in) :: use_var
    character(len=77) :: c77
    character(len=37) :: prefix_input, edge_input
    integer :: got_i3, got_n3

    prefix_input='                                     '
    prefix_input='PJ4/K1ABC 37'
    edge_input='                                     '
    edge_input='K1ABC/ABCD 37'
    call reset_packjt77_state()

    if(use_var) then
       got_i3=-1
       got_n3=-1
       c77=''
       call pack77var(prefix_input,got_i3,got_n3,c77,0)
       call assert_message_type('pack77var','PJ4/K1ABC 37',0,6,got_i3,got_n3)
       call assert_binary_payload('pack77var','PJ4/K1ABC 37',c77)

       got_i3=-1
       got_n3=-1
       c77=''
       call pack77var(edge_input,got_i3,got_n3,c77,0)
       call assert_message_type('pack77var','K1ABC/ABCD 37',0,6,got_i3,got_n3)
       call assert_binary_payload('pack77var','K1ABC/ABCD 37',c77)
    else
       got_i3=-1
       got_n3=-1
       c77=''
       call pack77(prefix_input,got_i3,got_n3,c77)
       call assert_message_type('pack77','PJ4/K1ABC 37',0,6,got_i3,got_n3)
       call assert_binary_payload('pack77','PJ4/K1ABC 37',c77)

       got_i3=-1
       got_n3=-1
       c77=''
       call pack77(edge_input,got_i3,got_n3,c77)
       call assert_message_type('pack77','K1ABC/ABCD 37',0,6,got_i3,got_n3)
       call assert_binary_payload('pack77','K1ABC/ABCD 37',c77)
    endif

    ntests=ntests+1
  end subroutine expect_binary_payload_after_wspr_prefix

  subroutine pack_standard(input,pack_i3,pack_n3,got_i3,got_n3,c77)
    character(len=*), intent(in) :: input
    integer, intent(in) :: pack_i3, pack_n3
    integer, intent(out) :: got_i3, got_n3
    character(len=77), intent(out) :: c77
    character(len=37) :: packed_input

    packed_input='                                     '
    packed_input=input
    call reset_packjt77_state()
    got_i3=pack_i3
    got_n3=pack_n3
    c77=''
    call pack77(packed_input,got_i3,got_n3,c77)
  end subroutine pack_standard

  subroutine pack_var(input,pack_i3,pack_n3,got_i3,got_n3,c77)
    character(len=*), intent(in) :: input
    integer, intent(in) :: pack_i3, pack_n3
    integer, intent(out) :: got_i3, got_n3
    character(len=77), intent(out) :: c77
    character(len=37) :: packed_input

    packed_input='                                     '
    packed_input=input
    call reset_packjt77_state()
    got_i3=pack_i3
    got_n3=pack_n3
    c77=''
    call pack77var(packed_input,got_i3,got_n3,c77,0)
  end subroutine pack_var

  subroutine reset_packjt77_state()
    integer :: n10, n12, n22

    call clear_all_state('N0AAA','N0BBB')
    call save_hash_mycallvar(mycall13var,hashmy10var,hashmy12var,hashmy22var)
    hashdx10var=ihashcall(dxcall13var,10)
    call save_hash_call(dxcall13,n10,n12,n22)
    call save_hash_callvar(dxcall13var,1)
  end subroutine reset_packjt77_state

end program test_packjt77_protocol_vectors
