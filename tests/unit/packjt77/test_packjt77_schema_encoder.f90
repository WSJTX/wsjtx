program test_packjt77_schema_encoder

  use packjt77_schema
  use packjt77_test_helpers
  implicit none

  integer :: ntests

  ntests=0

  call expect_type1_exact_bits()
  call expect_type2_tag_bits()
  call expect_range_failures()
  call expect_bitstring_validation()
  call expect_failure_has_no_shared_state_side_effects()

  write(*,1000) ntests
1000 format('packjt77 schema encoder tests passed: ',i0)

contains

  subroutine expect_type1_exact_bits()
    character(len=77) :: c77
    logical :: ok

    call encode_pack77_type1(10214965,0,12751800,0,0,10342,c77,ok)

    call assert_true('Type 1 schema encode',ok)
    call assert_binary_payload('Type 1 schema','K1ABC W9XYZ FN42',c77)
    call assert_text_equal('Type 1 c77',c77, &
         '00001001101111011110001101010000011000010100100111011100000010100001100110001')
    call assert_text_equal('Type 1 i3 bits',c77(75:77),'001')
    ntests=ntests+1
  end subroutine expect_type1_exact_bits

  subroutine expect_type2_tag_bits()
    character(len=77) :: c77
    logical :: ok

    call encode_pack77_type2(10214965,0,12751800,0,0,10342,c77,ok)

    call assert_true('Type 2 schema encode',ok)
    call assert_binary_payload('Type 2 schema','tag-only portable layout',c77)
    call assert_text_equal('Type 2 c77',c77, &
         '00001001101111011110001101010000011000010100100111011100000010100001100110010')
    call assert_text_equal('Type 2 i3 bits',c77(75:77),'010')
    ntests=ntests+1
  end subroutine expect_type2_tag_bits

  subroutine expect_range_failures()
    character(len=77) :: c77
    logical :: ok

    call encode_pack77_type1(-1,0,12751800,0,0,10342,c77,ok)
    call assert_true('negative field rejected',.not.ok)

    call encode_pack77_type1(10214965,0,12751800,0,0,32768,c77,ok)
    call assert_true('too-wide field rejected',.not.ok)

    call encode_pack77_type5(0,0,0,0,0,0,c77,ok)
    call assert_true('Type 5 serial zero rejected',.not.ok)

    ntests=ntests+1
  end subroutine expect_range_failures

  subroutine expect_bitstring_validation()
    character(len=77) :: c77
    logical :: ok

    call encode_pack77_free_text(repeat('1',71),c77,ok)
    call assert_true('free-text bitstring accepted',ok)
    call assert_text_equal('free-text tag bits',c77(72:77),'000000')

    call encode_pack77_free_text(repeat('1',70),c77,ok)
    call assert_true('short bitstring rejected',.not.ok)

    call encode_pack77_free_text(repeat('1',72),c77,ok)
    call assert_true('long bitstring rejected',.not.ok)

    call encode_pack77_free_text(repeat('1',70)//'X',c77,ok)
    call assert_true('non-binary bitstring rejected',.not.ok)

    ntests=ntests+1
  end subroutine expect_bitstring_validation

  subroutine expect_failure_has_no_shared_state_side_effects()
    character(len=77) :: c77
    logical :: ok

    call clear_all_state('','')
    call encode_pack77_type1(10214965,0,12751800,0,0,32768,c77,ok)

    call assert_true('side-effect failure rejected',.not.ok)
    call assert_shared_hash_tables_empty('schema failure')

    ntests=ntests+1
  end subroutine expect_failure_has_no_shared_state_side_effects

end program test_packjt77_schema_encoder
