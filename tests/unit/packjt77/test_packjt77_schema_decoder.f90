program test_packjt77_schema_decoder

  use packjt77_schema
  use packjt77
  use packjt77_test_helpers
  implicit none

  integer :: ntests

  ntests=0

  call expect_type1_exact_fields()
  call expect_type2_tag_decode()
  call expect_invalid_binary_and_tag_fail()
  call expect_type1_grid_boundary()
  call expect_wspr_selector_padding_and_power_checks()
  call expect_type5_grid_range_check()
  call expect_type5_raw_invalid_serial_rejected()
  call expect_failure_has_no_shared_state_side_effects()

  write(*,1000) ntests
1000 format('packjt77 schema decoder tests passed: ',i0)

contains

  subroutine expect_type1_exact_fields()
    character(len=77) :: c77
    type(pack77_type12_fields) :: fields
    integer :: i3,n3
    logical :: ok

    c77='00001001101111011110001101010000011000010100100111011100000010100001100110001'

    call decode_pack77_tag(c77,i3,n3,ok)
    call assert_true('Type 1 tag decode',ok)
    call assert_int('Type 1 i3',1,i3)
    call assert_int('Type 1 n3',0,n3)

    call decode_pack77_type1(c77,fields,ok)
    call assert_true('Type 1 field decode',ok)
    call assert_int('Type 1 n28a',10214965,fields%n28a)
    call assert_int('Type 1 ipa',0,fields%ipa)
    call assert_int('Type 1 n28b',12751800,fields%n28b)
    call assert_int('Type 1 ipb',0,fields%ipb)
    call assert_int('Type 1 ir',0,fields%ir)
    call assert_int('Type 1 igrid4',10342,fields%igrid4)

    ntests=ntests+1
  end subroutine expect_type1_exact_fields

  subroutine expect_type2_tag_decode()
    character(len=77) :: c77
    type(pack77_type12_fields) :: fields
    integer :: i3,n3
    logical :: ok

    c77='00001001101111011110001101010000011000010100100111011100000010100001100110010'

    call decode_pack77_tag(c77,i3,n3,ok)
    call assert_true('Type 2 tag decode',ok)
    call assert_int('Type 2 i3',2,i3)
    call assert_int('Type 2 n3',0,n3)
    call decode_pack77_type2(c77,fields,ok)
    call assert_true('Type 2 field decode',ok)
    call assert_int('Type 2 n28a',10214965,fields%n28a)
    call assert_int('Type 2 igrid4',10342,fields%igrid4)

    ntests=ntests+1
  end subroutine expect_type2_tag_decode

  subroutine expect_invalid_binary_and_tag_fail()
    character(len=77) :: c77
    type(pack77_type12_fields) :: fields
    integer :: i3,n3
    logical :: ok

    c77='00001001101111011110001101010000011000010100100111011100000010100001100110001'
    c77(10:10)='x'
    call decode_pack77_tag(c77,i3,n3,ok)
    call assert_true('non-binary tag rejected',.not.ok)

    c77='00001001101111011110001101010000011000010100100111011100000010100001100110001'
    c77(75:77)='010'
    call decode_pack77_type1(c77,fields,ok)
    call assert_true('wrong fixed i3 rejected',.not.ok)

    ntests=ntests+1
  end subroutine expect_invalid_binary_and_tag_fail

  subroutine expect_type1_grid_boundary()
    character(len=37) :: input, decoded
    character(len=77) :: c77
    logical :: ok

    input='K1ABC W9XYZ FN42'
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(input)
      call assert_pack77_result('pack77',input,encoded)
      call assert_int('Type 1 boundary i3',1,encoded%i3)
      call assert_int('Type 1 boundary n3',0,encoded%n3)
      c77=encoded%c77
    end block

    write(c77(60:74),'(b15.15)') 32399
    decoded='                                     '
    ok=.false.
    call unpack77_configured(c77,0,decoded,ok, &
         unpack77_options(record_hashes=.false.,record_recent_calls=.false.))
    call assert_true('Type 1 grid boundary succeeds',ok)
    call assert_text_equal('Type 1 grid boundary decode', &
         'K1ABC W9XYZ RR99',decoded)

    write(c77(60:74),'(b15.15)') 32400
    decoded='                                     '
    ok=.true.
    call unpack77_configured(c77,0,decoded,ok, &
         unpack77_options(record_hashes=.false.,record_recent_calls=.false.))
    call assert_true('Type 1 invalid grid boundary fails',.not.ok)

    ntests=ntests+1
  end subroutine expect_type1_grid_boundary

  subroutine expect_wspr_selector_padding_and_power_checks()
    character(len=77) :: c77
    type(pack77_wspr_type1_fields) :: type1_fields
    type(pack77_wspr_type3_fields) :: type3_fields
    logical :: ok

    c77=repeat('0',77)
    c77(72:77)='110000'
    c77(48:50)='110'
    call decode_pack77_wspr_type1(c77,type1_fields,ok)
    call assert_true('invalid WSPR Type 1 selector rejected',.not.ok)
    call decode_pack77_wspr_type3(c77,type3_fields,ok)
    call assert_true('invalid WSPR Type 3 selector rejected',.not.ok)

    c77=repeat('0',77)
    c77(72:77)='110000'
    c77(48:50)='010'
    c77(51:51)='1'
    call decode_pack77_wspr_type3(c77,type3_fields,ok)
    call assert_true('nonzero WSPR padding rejected',.not.ok)

    c77=repeat('0',77)
    c77(44:48)='11111'
    c77(49:50)='00'
    c77(72:77)='110000'
    call decode_pack77_wspr_type1(c77,type1_fields,ok)
    call assert_true('invalid WSPR raw power rejected',.not.ok)

    ntests=ntests+1
  end subroutine expect_wspr_selector_padding_and_power_checks

  subroutine expect_type5_grid_range_check()
    character(len=77) :: c77
    type(pack77_type5_fields) :: fields
    logical :: ok

    c77=repeat('0',77)
    c77(50:74)=repeat('1',25)
    c77(75:77)='101'
    call decode_pack77_type5(c77,fields,ok)
    call assert_true('Type 5 invalid grid rejected',.not.ok)

    ntests=ntests+1
  end subroutine expect_type5_grid_range_check

  subroutine expect_type5_raw_invalid_serial_rejected()
    character(len=77) :: c77
    character(len=37) :: decoded
    logical :: ok, unpack_ok

    call encode_pack77_type5(0,0,0,0,1,0,c77,ok)
    call assert_true('Type 5 raw serial one encoded',ok)
    write(c77(39:49),'(b11.11)') 0

    decoded='                                     '
    unpack_ok=.true.
    call unpack77(c77,0,decoded,unpack_ok)
    call assert_true('Type 5 raw serial zero rejected',.not.unpack_ok)

    ntests=ntests+1
  end subroutine expect_type5_raw_invalid_serial_rejected

  subroutine expect_failure_has_no_shared_state_side_effects()
    character(len=77) :: c77
    type(pack77_wspr_type3_fields) :: fields
    logical :: ok

    call clear_all_state('','')
    c77=repeat('0',77)
    c77(48:50)='010'
    c77(51:51)='1'
    c77(72:77)='110000'

    call decode_pack77_wspr_type3(c77,fields,ok)

    call assert_true('schema decode failure rejected',.not.ok)
    call assert_shared_hash_tables_empty('decoder failure')

    ntests=ntests+1
  end subroutine expect_failure_has_no_shared_state_side_effects

end program test_packjt77_schema_decoder
