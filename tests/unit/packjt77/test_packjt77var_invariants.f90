program test_packjt77var_invariants

  use packjt77
  use packjt77_test_helpers
  implicit none

  integer :: ntests
  external :: genft8var

  ntests=0

  call expect_genft8var_rejects_unexpanded_dx_macro()
  call expect_configured_var_rejects_unqualified_hash_only_message()

  write(*,1000) ntests
1000 format('packjt77var invariant tests passed: ',i0)

contains

  subroutine expect_genft8var_rejects_unexpanded_dx_macro()
    character(len=37) :: input, direct, msgsent
    integer :: i3, n3
    integer :: itone(79)
    integer*1 :: msgbits(77)

    input='$DX K1ABC FN42'
    direct='W9XYZ K1ABC FN42'
    msgsent='                                     '
    msgbits=0
    itone=0
    i3=-1
    n3=-1

    call genft8var(input,i3,n3,0,msgsent,msgbits,itone)

    call assert_int('var unexpanded DX macro i3',-1,i3)
    call assert_int('var unexpanded DX macro n3',-1,n3)
    call assert_text_equal('var unexpanded DX macro message', &
         '*** bad message ***',msgsent)
    call assert_true('var unexpanded DX macro message bits', &
         all(msgbits.eq.0))
    call assert_true('var unexpanded DX macro tones',all(itone.eq.0))

    call genft8var(direct,i3,n3,0,msgsent,msgbits,itone)

    call assert_int('var pre-expanded DX macro i3',1,i3)
    call assert_int('var pre-expanded DX macro n3',0,n3)
    call assert_text_equal('var pre-expanded DX macro decode',direct,msgsent)
    call assert_true('var pre-expanded DX macro message bits', &
         any(msgbits.ne.0))
    call assert_true('var pre-expanded DX macro tones',any(itone.ne.0))

    ntests=ntests+1
  end subroutine expect_genft8var_rejects_unexpanded_dx_macro

  subroutine expect_configured_var_rejects_unqualified_hash_only_message()
    character(len=77) :: c77
    character(len=13) :: c13
    character(len=37) :: decoded, input
    integer :: n10, n12, n22
    logical :: ok

    input='PJ2/W1AW <W7ABC>'
    call reset_packjt77var_state('N0AAA', 'N0BBB')
    block
      type(pack77_result) :: encoded
      encoded=pack77_result_from_api(input)
      call assert_pack77_result('configured-var delta',input,encoded)
      call assert_int('configured-var delta i3',4,encoded%i3)
      call assert_int('configured-var delta n3',0,encoded%n3)
      c77=encoded%c77
    end block

    call reset_packjt77var_state('N0AAA', 'N0BBB')
    call normalize_call('W7ABC',c13)
    n10=ihashcall(c13,10)
    n12=ihashcall(c13,12)
    n22=ihashcall(c13,22)
    call save_hash_call(c13,n10,n12,n22)

    decoded='                                     '
    ok=.true.
    call unpack77_configured(c77,0,decoded,ok,unpack77_options(thread_index=1))
    if(ok) then
       write(*,1010) trim(decoded)
1010   format('Configured-var hash-only delta decoded as "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_configured_var_rejects_unqualified_hash_only_message

end program test_packjt77var_invariants
