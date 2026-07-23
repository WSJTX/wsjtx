program test_packjt77_grammar_gate

  use packjt77_grammar, only: pack77_gate_messages_match
  implicit none

  integer :: ntests
  character(len=13) :: no_calls(1), staged_calls(2)

  ntests=0
  no_calls=''
  staged_calls=''
  staged_calls(1)='PJ4/K1ABC'
  staged_calls(2)='W3CCX'

  call expect_match('equal messages', &
       '  k1abc   w9xyz   fn42', 'K1ABC W9XYZ FN42', no_calls, 0)
  call expect_match('auto-hash canonicalization', &
       'W1AW/P K1ABC 5A CT', '<W1AW/P> K1ABC 5A CT', no_calls, 0)
  call expect_match('staged placeholder', &
       '<PJ4/K1ABC> W9XYZ RR73', '<...> W9XYZ RR73', staged_calls, 2)

  call expect_no_match('unstaged placeholder', &
       '<PJ4/K1ABC> W9XYZ RR73', '<...> W9XYZ RR73', no_calls, 0)
  call expect_no_match('CQ bracketed C11 mismatch', &
       'CQ <PJ4/K1ABC>', 'CQ PJ4/K1ABC', no_calls, 0)
  call expect_no_match('wrong bracket body', &
       '<K1ABC> W9XYZ RR73', '<W9XYZ> W9XYZ RR73', no_calls, 0)
  call expect_no_match('different token count', &
       'K1ABC W9XYZ FN42', 'K1ABC W9XYZ', no_calls, 0)
  call expect_no_match('changed first call', &
       'K1ABC W9XYZ FN42', 'N1ABC W9XYZ FN42', no_calls, 0)
  call expect_no_match('changed second call', &
       'K1ABC W9XYZ FN42', 'K1ABC W9XYA FN42', no_calls, 0)
  call expect_no_match('changed report', &
       'K1ABC W9XYZ -12', 'K1ABC W9XYZ -13', no_calls, 0)
  call expect_no_match('changed grid', &
       'K1ABC W9XYZ FN42', 'K1ABC W9XYZ FN43', no_calls, 0)
  call expect_no_match('changed serial', &
       'K1ABC FN42 37', 'K1ABC FN42 38', no_calls, 0)

  write(*,1000) ntests
1000 format('packjt77 grammar gate tests passed: ',i0)

contains

  subroutine expect_match(label,input_msg,decoded_msg,staged_calls,nstaged)
    character(len=*), intent(in) :: label,input_msg,decoded_msg
    character(len=13), intent(in) :: staged_calls(:)
    integer, intent(in) :: nstaged

    if(.not.pack77_gate_messages_match(input_msg,decoded_msg,staged_calls, &
         nstaged)) then
       write(*,1010) trim(label), trim(input_msg), trim(decoded_msg)
1010   format(a,' expected match for input "',a,'" decoded "',a,'"')
       error stop 1
    endif
    ntests=ntests+1
  end subroutine expect_match

  subroutine expect_no_match(label,input_msg,decoded_msg,staged_calls,nstaged)
    character(len=*), intent(in) :: label,input_msg,decoded_msg
    character(len=13), intent(in) :: staged_calls(:)
    integer, intent(in) :: nstaged

    if(pack77_gate_messages_match(input_msg,decoded_msg,staged_calls,nstaged)) then
       write(*,1020) trim(label), trim(input_msg), trim(decoded_msg)
1020   format(a,' expected mismatch for input "',a,'" decoded "',a,'"')
       error stop 1
    endif
    ntests=ntests+1
  end subroutine expect_no_match

end program test_packjt77_grammar_gate
