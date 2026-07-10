program test_packjt77_silent_rewrite_corpus

  use packjt77
  implicit none

  integer, parameter :: max_line=256
  character(len=max_line) :: corpus_path, arg, line, input, origin
  character(len=37) :: decoded
  character(len=77) :: c77
  integer :: unit, ios, sep, i3, n3
  integer :: total, exact, canon, reject, silent
  logical :: success, assert_zero

  total=0
  exact=0
  canon=0
  reject=0
  silent=0
  assert_zero=.false.

  corpus_path='tests/unit/packjt77/silent_rewrite_corpus.txt'
  if(command_argument_count().ge.1) call get_command_argument(1,corpus_path)
  if(command_argument_count().ge.2) then
     call get_command_argument(2,arg)
     assert_zero=trim(arg).eq.'--assert-zero-silent'
  endif

  open(newunit=unit,file=trim(corpus_path),status='old',action='read',iostat=ios)
  if(ios.ne.0) then
     write(*,1000) trim(corpus_path), ios
1000 format('failed to open silent-rewrite corpus "',a,'": ',i0)
     error stop 1
  endif

  do
     read(unit,'(a)',iostat=ios) line
     if(ios.ne.0) exit
     if(len_trim(line).eq.0) cycle
     sep=index(line,'|')
     if(sep.le.0) cycle
     input=' '
     origin=' '
     input=adjustl(line(1:sep-1))
     origin=adjustl(line(sep+1:))
     call classify_case(trim(input),trim(origin),exact,canon,reject,silent)
     total=total+1
  enddo
  close(unit)

  write(*,1010) total, exact, canon, reject, silent
1010 format('SILENT_REWRITE_SUMMARY total=',i0,' exact=',i0,' canon=',i0, &
            ' reject=',i0,' silent_rewrite=',i0)
  if(assert_zero .and. silent.ne.0) error stop 1

  ! Self-contained on purpose: this driver must also compile against older
  ! source snapshots where shared grammar/test helper APIs may not exist.
contains

  subroutine classify_case(input_msg,origin,exact,canon,reject,silent)
    character(len=*), intent(in) :: input_msg, origin
    integer, intent(inout) :: exact, canon, reject, silent
    character(len=37) :: packed_input
    character(len=16) :: class

    packed_input='                                     '
    packed_input=input_msg
    call reset_hash_state()
    call pack77(packed_input,i3,n3,c77)
    success=i3.ge.0
    if(.not.success) then
       reject=reject+1
       class='REJECT'
       call print_case(input_msg,origin,.false.,i3,n3,' ',class)
       return
    endif

    decoded='                                     '
    success=.false.
    call reset_hash_state()
    call unpack77(c77,0,decoded,success)
    if(.not.success) then
       silent=silent+1
       class='SILENT_REWRITE'
       call print_case(input_msg,origin,.true.,i3,n3,decoded,class)
       return
    endif

    if(normalized_equal(input_msg,decoded)) then
       exact=exact+1
       class='EXACT'
    else if(canonical_match(input_msg,decoded)) then
       canon=canon+1
       class='CANON'
    else
       silent=silent+1
       class='SILENT_REWRITE'
    endif
    call print_case(input_msg,origin,.true.,i3,n3,decoded,class)
  end subroutine classify_case

  subroutine print_case(input_msg,origin,encoded,i3,n3,decoded_msg,class)
    character(len=*), intent(in) :: input_msg, origin, decoded_msg, class
    logical, intent(in) :: encoded
    integer, intent(in) :: i3, n3

    write(*,1020) trim(input_msg), trim(origin), encoded, i3, n3, &
         trim(decoded_msg), trim(class)
1020 format(a,' | ',a,' | encoded=',l1,' | type=',i0,'.',i0, &
            ' | decoded=',a,' | ',a)
  end subroutine print_case

  logical function normalized_equal(left,right) result(equal)
    character(len=*), intent(in) :: left, right
    character(len=37) :: left_norm, right_norm

    call normalize_message(left,left_norm)
    call normalize_message(right,right_norm)
    equal=trim(left_norm).eq.trim(right_norm)
  end function normalized_equal

  logical function canonical_match(left,right) result(matches)
    character(len=*), intent(in) :: left, right
    character(len=37) :: left_tokens(19), right_tokens(19), wrapped
    integer :: left_n, right_n, i

    matches=.false.
    call split_tokens(left,left_tokens,left_n)
    call split_tokens(right,right_tokens,right_n)
    if(left_n.ne.right_n) return
    do i=1,left_n
       if(trim(left_tokens(i)).eq.trim(right_tokens(i))) cycle
       wrapped='                                     '
       wrapped='<'//trim(left_tokens(i))//'>'
       if(trim(right_tokens(i)).eq.trim(wrapped)) cycle
       if(trim(right_tokens(i)).eq.'<...>' .and. hash_like(left_tokens(i))) cycle
       return
    enddo
    matches=.true.
  end function canonical_match

  logical function hash_like(token) result(ok)
    character(len=*), intent(in) :: token
    integer :: n

    n=len_trim(token)
    ok=.false.
    if(n.le.0) return
    ok=index(token(1:n),'/').gt.0
    if(n.ge.2) ok=ok .or. (token(1:1).eq.'<' .and. token(n:n).eq.'>')
  end function hash_like

  subroutine normalize_message(input_msg,normalized)
    character(len=*), intent(in) :: input_msg
    character(len=37), intent(out) :: normalized
    character(len=37) :: tokens(19)
    integer :: nwords, i

    normalized='                                     '
    call split_tokens(input_msg,tokens,nwords)
    do i=1,nwords
       if(len_trim(normalized).gt.0) normalized=trim(normalized)//' '
       normalized=trim(normalized)//trim(tokens(i))
    enddo
  end subroutine normalize_message

  subroutine split_tokens(input_msg,tokens,nwords)
    character(len=*), intent(in) :: input_msg
    character(len=37), intent(out) :: tokens(19)
    integer, intent(out) :: nwords
    character(len=1) :: c, previous
    integer :: i, n

    tokens='                                     '
    nwords=0
    n=0
    previous=' '
    do i=1,len_trim(input_msg)
       c=input_msg(i:i)
       if(ichar(c).eq.0) c=' '
       if(c.ge.'a' .and. c.le.'z') c=char(ichar(c)-32)
       if(c.eq.' ' .and. previous.eq.' ') cycle
       if(c.ne.' ' .and. previous.eq.' ') then
          nwords=nwords+1
          n=0
          if(nwords.gt.size(tokens)) return
       endif
       if(c.ne.' ') then
          n=n+1
          if(n.le.len(tokens(1))) tokens(nwords)(n:n)=c
       endif
       previous=c
    enddo
  end subroutine split_tokens

  subroutine reset_hash_state()
    calls10=''
    calls12=''
    calls22=''
    recent_calls=''
    ihash22=-1
    nzhash=0
    mycall13='             '
    dxcall13='             '
  end subroutine reset_hash_state

end program test_packjt77_silent_rewrite_corpus
