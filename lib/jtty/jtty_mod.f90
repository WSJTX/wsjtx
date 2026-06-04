module jtty_mod

  parameter (MAX_FRAMES=16)             !Max frames for the encoded message

contains

subroutine pack_jtty(message,c32,nframes)

! Input:   character*80   message     !JTTY message, as it appears to a user
! Output:  character*32   c32         !32-bit payload
!          integer        nframes     !Frames in this message (max = 16)
!
! Source coding flow:
!   1. Normalize operator text into the JTTY source alphabet.
!   2. Find the minimum-frame path through compact forms plus free text.
!   3. Convert the selected path into 32-bit JTTY payload frames.

  use packjt77
  character*80 message,msg
  character*32 c32(MAX_FRAMES)

  integer, parameter :: KIND_TEXT=1, KIND_599=2, KIND_STRUCT=3
  integer, parameter :: RANK_COMPACT=1, RANK_TEXT=2, INF=999
  integer dp(81), choice_kind(80), choice_next(80), choice_i2(80)
  integer choice_n2(80), choice_call_first(80), choice_call_len(80)
  integer best_rank(80)
  integer n, ipos, n32, lcall, icall, iend
  integer i2, n2, n28
  character*13 c13
  logical ok

  call normalize_jtty_message(message,msg,ok)
  if(.not.ok) then
     nframes=-1
     c32=''
     return
  endif
  message=msg
  n=len_trim(msg)
  nframes=0
  c32=''
  if(n.le.0) return

  ! dp(i) is the minimum frame count for msg(i:n); choice_* records its first frame.
  dp=INF
  choice_kind=0
  choice_next=0
  choice_i2=0
  choice_n2=0
  choice_call_first=0
  choice_call_len=0
  best_rank=INF
  dp(n+1)=0

  ! Each candidate generator recognizes one assigned part of the JTTY source grammar.
  ! DP positions are source-character offsets. Compact forms may start only at
  ! token boundaries, and structured calls must end at end-of-message or space.
  do ipos=n,1,-1
     call try_text()
     call try_599()
     call try_structured()
  enddo

  ipos=1
  ! Walk the selected path and emit the frame payload for each transition.
  do while(ipos.le.n .and. nframes.lt.MAX_FRAMES)
     if(choice_kind(ipos).le.0) exit
     nframes=nframes+1
     if(choice_kind(ipos).eq.KIND_STRUCT) then
        i2=choice_i2(ipos)
        n2=choice_n2(ipos)
        c13='             '
        icall=choice_call_first(ipos)
        lcall=choice_call_len(ipos)
        c13(1:lcall)=msg(icall:icall+lcall-1)
        call pack28(c13,n28)
        n32=shiftl(n28,4) + 4*n2 + i2
     else if(choice_kind(ipos).eq.KIND_599) then
        call pack_599_frame(ipos,n32)
     else
        call pack_text_frame(ipos,n32)
     endif
     write(c32(nframes),1002) n32
1002 format(b32.32)
     ipos=choice_next(ipos)
  enddo

  if(ipos.le.n) then
     nframes=-1
     c32=''
  endif

  return

contains

  subroutine consider(inext,kind,i2arg,n2arg,icallarg,lcallarg,rank)
    integer inext,kind,i2arg,n2arg,icallarg,lcallarg,rank
    integer cand

    if(inext.lt.1 .or. inext.gt.n+1) return
    if(dp(inext).ge.INF) return
    cand=1 + dp(inext)
    if(cand.gt.MAX_FRAMES) return
    ! Canonical ties prefer compact encodings, then the longest source span.
    if(cand.lt.dp(ipos) .or. &
         (cand.eq.dp(ipos) .and. rank.lt.best_rank(ipos)) .or. &
         (cand.eq.dp(ipos) .and. rank.eq.best_rank(ipos) .and. inext.gt.choice_next(ipos))) then
       dp(ipos)=cand
       best_rank(ipos)=rank
       choice_kind(ipos)=kind
       choice_next(ipos)=inext
       choice_i2(ipos)=i2arg
       choice_n2(ipos)=n2arg
       choice_call_first(ipos)=icallarg
       choice_call_len(ipos)=lcallarg
    endif
  end subroutine consider

  subroutine try_text()
    ! i2=3: plain free text, five 6-bit JTTY characters per frame.
    ! Always legal: normalize_jtty_message guarantees msg(1:n) is in the source alphabet.
    call consider(min(n,ipos+4)+1,KIND_TEXT,3,0,0,0,RANK_TEXT)
  end subroutine try_text

  subroutine try_599()
    ! i2=2: literal "599 " plus up to five following 6-bit JTTY characters.
    if(.not.at_token_start(ipos)) return
    if(.not.matches(ipos,'599 ')) return
    call consider(min(n,ipos+8)+1,KIND_599,2,0,0,0,RANK_COMPACT)
  end subroutine try_599

  subroutine try_structured()
    ! Structured subtype map:
    !   0.0 CQ <call> CQ     0.1 <call>
    !   0.2 TU <call> CQ     0.3 <call> TU
    !   1.0 <call> AGN?      1.1 TU NOW <call>
    !   1.2 and 1.3 remain reserved.
    if(.not.at_token_start(ipos)) return

    if(matches(ipos,'CQ ')) then
       icall=ipos+3
       do lcall=3,6
          if(valid_call_at(icall,lcall) .and. matches(icall+lcall,' CQ')) then
             iend=icall+lcall+2
             call consider_structured(iend,0,0,icall,lcall)
          endif
       enddo
    endif

    if(matches(ipos,'TU ')) then
       icall=ipos+3
       do lcall=3,6
          if(valid_call_at(icall,lcall) .and. matches(icall+lcall,' CQ')) then
             iend=icall+lcall+2
             call consider_structured(iend,0,2,icall,lcall)
          endif
       enddo
    endif

    if(matches(ipos,'TU NOW ')) then
       icall=ipos+7
       do lcall=3,6
          if(valid_call_at(icall,lcall)) then
             iend=icall+lcall-1
             call consider_structured(iend,1,1,icall,lcall)
          endif
       enddo
    endif

    icall=ipos
    do lcall=3,6
       if(valid_call_at(icall,lcall)) then
          if(matches(icall+lcall,' TU')) then
             iend=icall+lcall+2
             call consider_structured(iend,0,3,icall,lcall)
          endif
          if(matches(icall+lcall,' AGN?')) then
             iend=icall+lcall+4
             call consider_structured(iend,1,0,icall,lcall)
          endif
          iend=icall+lcall-1
          call consider_structured(iend,0,1,icall,lcall)
       endif
    enddo
  end subroutine try_structured

  subroutine consider_structured(iendarg,i2arg,n2arg,icallarg,lcallarg)
    integer iendarg,i2arg,n2arg,icallarg,lcallarg
    integer jnext

    ! Structured frames decode with one implicit separator before any following frame.
    if(iendarg.eq.n) then
       jnext=n+1
    else if(iendarg.lt.n) then
       if(msg(iendarg+1:iendarg+1).eq.' ') then
          jnext=iendarg+2
       else
          return
       endif
    else
       return
    endif
    call consider(jnext,KIND_STRUCT,i2arg,n2arg,icallarg,lcallarg,RANK_COMPACT)
  end subroutine consider_structured

  logical function matches(istart,text)
    integer istart
    character*(*) text
    integer ltext

    ltext=len(text)
    matches=istart.ge.1 .and. istart+ltext-1.le.n
    if(matches) matches=msg(istart:istart+ltext-1).eq.text
  end function matches

  logical function at_token_start(istart)
    integer istart

    if(istart.eq.1) then
       at_token_start=.true.
    else
       at_token_start=msg(istart-1:istart-1).eq.' '
    endif
  end function at_token_start

  logical function valid_call_at(istart,ltext)
    integer istart,ltext
    character*13 calltoken

    valid_call_at=.false.
    if(istart.lt.1 .or. ltext.lt.1 .or. istart+ltext-1.gt.n) return
    calltoken='             '
    calltoken(1:ltext)=msg(istart:istart+ltext-1)
    valid_call_at=jtty_standard_call(calltoken)
  end function valid_call_at

  subroutine pack_text_frame(istart,n32out)
    ! Place five source characters in the upper 30 bits and set i2=3.
    integer istart,n32out
    integer i, n30
    character*1 c

    n30=0
    do i=istart,istart+4
       c=' '
       if(i.le.n) c=msg(i:i)
       n30=64*n30 + jchar(c)
    enddo
    n32out=ishft(n30,2) + 3
  end subroutine pack_text_frame

  subroutine pack_599_frame(istart,n32out)
    ! Place the five characters after "599 " in the upper 30 bits and set i2=2.
    integer istart,n32out
    integer i, n30
    character*1 c

    n30=0
    do i=istart+4,istart+8
       c=' '
       if(i.le.n) c=msg(i:i)
       n30=64*n30 + jchar(c)
    enddo
    n32out=ishft(n30,2) + 2
  end subroutine pack_599_frame

end subroutine pack_jtty

logical function jtty_standard_call(c13)

! True only for tokens safe to carry in a 28-bit structured callsign field.

  use packjt77
  character*13 c13,c13a
  character*13 unpacked
  character*6 bcall_1
  logical ok1,success
  integer*4 n28

  jtty_standard_call=.false.
  c13a=c13
  ! chkcall is a syntax filter; pack28/unpack28 round-trip defines what this
  ! protocol field can actually carry.
  call chkcall(c13a,bcall_1,ok1)
  if(.not.ok1) return
  if(index(c13a,'/').gt.0) return
  if(c13a(1:1).eq.'Q') return
  if(verify(trim(c13a),'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789').ne.0) return

  call pack28(c13a,n28)
  call unpack28(n28,unpacked,success)
  if(.not.success) return
  jtty_standard_call=trim(unpacked).eq.trim(c13a)

end function jtty_standard_call

subroutine normalize_jtty_message(raw,normalized,ok)

! Fold operator text into the source alphabet used by the JTTY encoder.

  character*80 raw,normalized
  character*1 c
  logical ok,last_space

  normalized=''
  ok=.true.
  last_space=.true.
  j=0

  do i=1,len(raw)
     c=raw(i:i)
     if(ichar(c).eq.0) c=' '
     ! The decoder uses '~' as a display marker for space, not a source symbol.
     if(c.eq.'~') c=' '
     if(c.ge.'a' .and. c.le.'z') c=char(ichar(c)-32)
     if(jchar(c).lt.0) then
        ok=.false.
        normalized=''
        return
     endif
     if(c.eq.' ') then
        if(last_space) cycle
        j=j+1
        normalized(j:j)=' '
        last_space=.true.
     else
        j=j+1
        normalized(j:j)=c
        last_space=.false.
     endif
  enddo

end subroutine normalize_jtty_message

subroutine unpack_jtty(c32,nframes,message)
  
! Input:   character*32   c32         !32-bit payload
!          integer        nframes     !Frames in this message (max = 16)
! Output:  character*80   message     !JTTY message, as it appears to a user
!
! Frame decoding flow:
!   1. Read the class bits once for each 32-bit payload.
!   2. Dispatch only assigned frame forms.
!   3. Append through one bounds-safe path.

  use packjt77
  character*80 message
  character*32 c32(MAX_FRAMES)

  character*13 c13
  logical success

  message=''
  k=1
  do iframe=1,nframes
     if(k.gt.len(message)) exit             !Output buffer full; stop decoding
     read(c32(iframe),1002) n28,n2,i2
1002 format(b28.28,b2.2,b2.2)

     select case(i2)
     case(0)
        call unpack28(n28,c13,success)
        if(success) call append_structured_0(n2,c13)
     case(1)
        ! 1.2 and 1.3 are reserved; unpack_jtty intentionally emits no text for them.
        if(n2.le.1) then
           call unpack28(n28,c13,success)
           if(success) call append_structured_1(n2,c13)
        endif
     case(2)
        call append_text('599 ')
        call append_payload_chars()
     case(3)
        call append_payload_chars()
     end select

  enddo

  return

contains

  subroutine append_text(text)
    character*(*) text
    integer i

    do i=1,len(text)
       if(k.le.len(message)) message(k:k)=text(i:i)
       k=k+1
    enddo
  end subroutine append_text

  subroutine append_payload_chars()
    ! i2=2 and i2=3 carry five 6-bit JTTY characters in the upper 30 bits.
    integer n30, j, idx
    character*1 c

    read(c32(iframe),1006) n30
1006 format(b30.30)
    do j=1,5
       idx=iand(ishft(n30,-6*(5-j)),63)
       c=charj(idx)
       if(c.eq.' ') c='~'
       if(k.le.len(message)) message(k:k)=c
       k=k+1
    enddo
  end subroutine append_payload_chars

  subroutine append_structured_0(n2arg,c13arg)
    ! i2=0 structured subtype map:
    !   0.0 CQ <call> CQ     0.1 <call>
    !   0.2 TU <call> CQ     0.3 <call> TU
    integer n2arg
    character*13 c13arg

    select case(n2arg)
    case(0)
       call append_text('CQ '//trim(c13arg)//' CQ')
    case(1)
       call append_text(trim(c13arg))
    case(2)
       call append_text('TU '//trim(c13arg)//' CQ')
    case(3)
       call append_text(trim(c13arg)//' TU')
    end select
    call append_implicit_separator()
  end subroutine append_structured_0

  subroutine append_structured_1(n2arg,c13arg)
    ! i2=1 currently assigns 1.0 and 1.1; 1.2 and 1.3 remain reserved.
    integer n2arg
    character*13 c13arg

    select case(n2arg)
    case(0)
       call append_text(trim(c13arg)//' AGN?')
    case(1)
       call append_text('TU NOW '//trim(c13arg))
    end select
    call append_implicit_separator()
  end subroutine append_structured_1

  subroutine append_implicit_separator()
    ! Structured frames leave one blank column before any following frame.
    k=k+1
  end subroutine append_implicit_separator

end subroutine unpack_jtty


character*1 function charj(j)

! Returns the printable character corresponding to JTTY index j (0-63),

  character*64 c
!                   1         2         3         4         5         6
! j       0123456789012345678901234567890123456789012345678901234567890123
  data c/"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!@#$%,&*()_'=[]{}<>|:;"/
!                                                    "  
  c(44:44)='"'                                !use " rather than @

  charj=c(j+1:j+1)

  return
end function charj

integer function jchar(c0)

! Returns the JTTY index (0-63) corresponding to character c0.
  
  character*1 c0
  character*64 c
!                   1         2         3         4         5         6
! j       0123456789012345678901234567890123456789012345678901234567890123
  data c/"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!@#$%,&*()_'=[]{}<>|:;"/
!                                                    "  
  c(44:44)='"'                                !use " rather than @

  jchar=index(c,c0)-1
  
  return
end function jchar

end module jtty_mod
