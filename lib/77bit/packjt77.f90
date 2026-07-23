module packjt77

  use packjt77_schema
  use packjt77_grammar

! These variables are accessible from outside via "use packjt77":
  parameter (MAXHASH=1000,MAXRECENT=10)
  ! Callsign hash resolution uses one session-persistent shared table. Encode,
  ! decode, and configured/var decode stage calls at their boundaries, then
  ! commit them through save_hash_call instead of maintaining separate TX/RX
  ! hash stores.
  character (len=13), dimension(0:1023) ::  calls10=''
  character (len=13), dimension(0:4095) ::  calls12=''
  character (len=13), dimension(1:MAXHASH) :: calls22=''
  character (len=13), dimension(1:MAXRECENT) :: recent_calls=''
  character (len=13) :: mycall13=''
  character (len=13) :: dxcall13=''
  integer, dimension(1:MAXHASH) :: ihash22=-1
  integer :: nzhash=0
  ! Configured/var decodes queue per-thread facts until fillhashvar folds them
  ! into the shared hash and recent-call state.
  character (len=13), dimension(1:840) :: queued_calls_by_thread=''
  character (len=13), dimension(1:840) :: queued_recent_calls_by_thread=''
  character (len=13) :: mycall13_configured=''
  character (len=13) :: dxcall13_configured=''
  character (len=13) :: mycall13_configured_prev=''
  character (len=13) :: dxcall13_configured_prev=''
  integer hashmy10_configured,hashmy12_configured,hashmy22_configured,hashdx10_configured
  logical :: dxcall13_configured_set=.false.
  logical :: mycall13_configured_set=.false.
  integer, dimension(1:24) :: nqueued_calls_by_thread=0
  integer, dimension(1:24) :: nqueued_recent_calls_by_thread=0
  ! Per-thread slice boundaries of queued_*_by_thread; earlier (busier)
  ! decode threads get larger slots.
  integer, dimension(1:25) :: thread_call_index=(/0,200,300,370,420,460,500,530,560,590,610,630,650,670,690,710, &
                                        730,750,770,790,800,810,820,830,840/)
  integer n28a_configured,n28b_configured
!$omp threadprivate(n28a_configured,n28b_configured)
! Everything is private unless exported here; imported grammar/schema
! symbols therefore never re-export through "use packjt77".
  private
  public :: pack77,pack77_legacy_truncating_fallback
  public :: pack77_options,unpack77_options
  public :: PACK77_STATUS_NOT_ENCODED,PACK77_STATUS_ENCODED
  public :: PACK77_STATUS_FREE_TEXT_TOO_LONG
  public :: PACK77_STATUS_FREE_TEXT_INVALID
  public :: PACK77_STATUS_PREFERRED_FAMILY_REJECTED
  public :: PACK77_STATUS_INTERNAL_ROUNDTRIP_REJECTED
  public :: unpack77,unpack77_configured,pack28,unpack28
  public :: split77,packtext77,unpacktext77
  public :: ihashcall,hash10,hash12,hash22,save_hash_call
  public :: add_call_to_recent_calls,queue_hash_call_for_thread
  public :: fold_queued_recent_calls,sync_configured_calls_for_decode_start
  public :: MAXHASH,MAXRECENT
  public :: calls10,calls12,calls22,recent_calls,ihash22,nzhash
  public :: mycall13,dxcall13
  public :: queued_calls_by_thread,queued_recent_calls_by_thread
  public :: nqueued_calls_by_thread,nqueued_recent_calls_by_thread
  public :: mycall13_configured,dxcall13_configured
  public :: mycall13_configured_prev,dxcall13_configured_prev
  public :: mycall13_configured_set,dxcall13_configured_set
  public :: hashmy10_configured,hashmy12_configured,hashmy22_configured
  public :: hashdx10_configured
  public :: thread_call_index

  integer, parameter :: PACK77_STATUS_NOT_ENCODED=0
  integer, parameter :: PACK77_STATUS_ENCODED=1
  integer, parameter :: PACK77_STATUS_FREE_TEXT_TOO_LONG=3
  integer, parameter :: PACK77_STATUS_FREE_TEXT_INVALID=4
  integer, parameter :: PACK77_STATUS_PREFERRED_FAMILY_REJECTED=5
  integer, parameter :: PACK77_STATUS_INTERNAL_ROUNDTRIP_REJECTED=6

  type pack77_result
     logical :: encoded=.false.
     integer :: status=PACK77_STATUS_NOT_ENCODED
     integer :: i3=-1
     integer :: n3=-1
     character(len=77) :: c77=' '
  end type pack77_result

  type pack77_hash_facts
     ! Calls that an accepted encode should make resolvable by later hash-only
     ! decodes. pack77 commits these unless record_tx_hashes is disabled.
     integer :: ncalls=0
     character(len=13) :: calls(32)=''
  end type pack77_hash_facts

  type pack77_candidate
     type(pack77_result) :: result
     type(pack77_hash_facts) :: hash_facts
  end type pack77_candidate

  type pack77_encode_core_result
     type(pack77_result) :: result
     type(pack77_hash_facts) :: hash_facts
  end type pack77_encode_core_result



  type unpack77_effects
     ! Decoders collect hash/recent-call side effects here and apply them only
     ! after the message has decoded successfully.
     integer :: nhash=0
     integer :: nrecent=0
     character(len=13) :: hash_calls(32)=''
     character(len=13) :: recent_calls(32)=''
  end type unpack77_effects

  type unpack77_context
     integer :: nrx=0
     logical :: configured=.false.
     logical :: strict_var_guards=.false.
     logical :: mycall_set=.false.
     logical :: dxcall_set=.false.
     character(len=13) :: mycall=''
     character(len=13) :: dxcall=''
     integer :: hashmy10=-1
     integer :: hashmy12=-1
     integer :: hashmy22=-1
     integer :: hashdx10=-1
  end type unpack77_context

  type configured_decode_state
     logical :: mycall_set=.false.
     logical :: dxcall_set=.false.
     character(len=13) :: mycall=''
     character(len=13) :: dxcall=''
     integer :: hashmy10=-1
     integer :: hashmy12=-1
     integer :: hashmy22=-1
     integer :: hashdx10=-1
  end type configured_decode_state

  type unpack77_core_result
     logical :: success=.false.
     integer :: i3=-1
     integer :: n3=-1
     character(len=37) :: msg=' '
     type(unpack77_effects) :: effects
  end type unpack77_core_result

  type unpack77_effect_policy
     logical :: record_hashes=.true.
     logical :: record_recent_calls=.true.
     logical :: queue_by_thread=.false.
     ! Queued policies require valid 1-based thread indexes; disable recording
     ! through record_hashes or record_recent_calls instead of sentinel indexes.
     integer :: nthr_hash=1
     integer :: nthr_recent=1
  end type unpack77_effect_policy

  type pack77_options
     logical :: prefer_wspr_50bit=.false.
     logical :: record_tx_hashes=.true.
     logical :: expand_dx_macro=.false.
     character(len=6) :: dx_macro_base=''
  end type pack77_options

  type unpack77_options
     integer :: thread_index=1
     logical :: record_hashes=.true.
     logical :: record_recent_calls=.true.
  end type unpack77_options

  contains

type(pack77_result) function pack77_no_match() result(encoded)
  encoded=pack77_result()
end function pack77_no_match

type(pack77_result) function pack77_reject(status) result(encoded)
  integer, intent(in) :: status

  encoded=pack77_result()
  encoded%status=status
end function pack77_reject

type(pack77_result) function pack77_accept(i3,n3,c77) result(encoded)
  integer, intent(in) :: i3,n3
  character(len=77), intent(in) :: c77

  encoded=pack77_result()
  encoded%encoded=.true.
  encoded%status=PACK77_STATUS_ENCODED
  encoded%i3=i3
  encoded%n3=n3
  encoded%c77=c77
end function pack77_accept

subroutine pack77_accept_candidate_encoded(candidate,i3,n3,c77,ok)
  type(pack77_candidate), intent(inout) :: candidate
  integer, intent(in) :: i3,n3
  character(len=77), intent(in) :: c77
  logical, intent(in) :: ok

  candidate%result=pack77_no_match()
  if(ok) candidate%result=pack77_accept(i3,n3,c77)
end subroutine pack77_accept_candidate_encoded

logical function take_candidate(candidate,msg,encoded) result(won)
  type(pack77_candidate), intent(in) :: candidate
  character(len=*), intent(in) :: msg
  type(pack77_encode_core_result), intent(inout) :: encoded
  type(unpack77_core_result) :: decoded
  logical :: messages_match,resolved_render_fits

! A candidate must round-trip through the neutral decoder before it wins; a
! syntactically shaped but gated-out candidate only records its family so
! fallback callers can report what kind of exact encoding was rejected.
  won=candidate%result%encoded
  if(won) then
     decoded=pack77_decode_neutral(candidate%result%c77)
     call pack77_gate_message_checks(msg,decoded%msg,candidate%hash_facts%calls, &
          candidate%hash_facts%ncalls,messages_match,resolved_render_fits)
     if(.not.(decoded%success .and. decoded%i3.eq.candidate%result%i3 .and. &
          decoded%n3.eq.candidate%result%n3 .and. messages_match .and. &
          resolved_render_fits)) then
        won=.false.
        encoded%result=pack77_reject(PACK77_STATUS_INTERNAL_ROUNDTRIP_REJECTED)
     endif
  endif
  if(.not.won) return
  encoded%result=candidate%result
  encoded%hash_facts=candidate%hash_facts
end function take_candidate

type(unpack77_core_result) function pack77_decode_neutral(c77) result(decoded)
  character(len=77), intent(in) :: c77
  type(unpack77_context) :: context

  context=unpack77_context()
  decoded=unpack77_core(c77,context)
end function pack77_decode_neutral

integer function pack77_wspr_payload_type(c77) result(wspr_type)
  character(len=77), intent(in) :: c77

! Bit columns 49/50 are the j49/j50 subtype selector fields fixed by the
! WSPR schemas in packjt77_schema; keep in sync with those field offsets.
  wspr_type=0
  if(c77(50:50).eq.'1') then
     wspr_type=2
  else if(c77(49:49).eq.'0') then
     wspr_type=1
  else if(c77(49:49).eq.'1') then
     ! Invalid selector 110 reaches Type 3; the schema decoder rejects it.
     wspr_type=3
  endif
end function pack77_wspr_payload_type




subroutine hash10(n10,c13)

  character*13 c13

  c13='<...>'
  if(n10.lt.0 .or. n10.gt.1023) return
  if(len(trim(calls10(n10))).gt.0) then
     c13=calls10(n10)
     c13='<'//trim(c13)//'>'
  endif
  return

end subroutine hash10

subroutine hash12(n12,c13)

  character*13 c13

  c13='<...>'
  if(n12.lt.0 .or. n12.gt.4095) return
  if(len(trim(calls12(n12))).gt.0) then
     c13=calls12(n12)
     c13='<'//trim(c13)//'>'
  endif
  return

end subroutine hash12


subroutine hash22(n22,c13)

  character*13 c13

  c13='<...>'
  do i=1,nzhash
     if(ihash22(i).eq.n22) then
        c13=calls22(i)
        c13='<'//trim(c13)//'>'
        go to 900
     endif
  enddo

900 return
end subroutine hash22


integer function ihashcall(c0,m)
  implicit none

  character(len=13), intent(in)       :: c0
  integer, intent(in)                 :: m
  integer(kind=8)                     :: n8
  integer                             :: i,j

  n8=0_8
  do i=1,11
     j=index(PACK77_BASE38,c0(i:i)) - 1
     n8=38_8*n8 + j
  enddo

  ihashcall=ihashcall_from_n8(n8,m)

  return
end function ihashcall

integer function ihashcall_from_n8(n8,m)
  implicit none

  integer(kind=8), intent(in) :: n8
  integer, intent(in)         :: m
  integer(kind=8), parameter  :: hash_factor(0:3) = (/2419_8, 62655_8, 10_8, 0_8/)
  integer(kind=8)             :: b(0:3), p(0:3)
  integer(kind=8)             :: carry, term
  integer(kind=8)             :: x
  integer                     :: i,j

  x=n8
  do i=0,3
     b(i)=mod(x,65536_8)
     x=x/65536_8
  enddo

  ! Return the top m bits of the low 64 bits of 47055833459*n8.
  carry=0_8
  do i=0,3
     term=carry
     do j=0,i
        term=term+hash_factor(j)*b(i-j)
     enddo
     p(i)=mod(term,65536_8)
     carry=term/65536_8
  enddo

  if(m.le.16) then
     ihashcall_from_n8=int(p(3)/(2_8**(16-m)))
  else
     ihashcall_from_n8=int(p(3)*(2_8**(m-16)) + p(2)/(2_8**(32-m)))
  endif

  return
end function ihashcall_from_n8

subroutine save_hash_call(c13,n10,n12,n22)

  character*13 c13,cw

  cw=c13
  if(cw(1:1).eq.' ' .or. cw(1:5).eq.'<...>') return
  if(cw(1:1).eq.'<') cw=cw(2:)
  i=index(cw,'>')
  if(i.gt.0) cw(i:)='         '

  if(len(trim(cw)) .lt. 3) return

  n10=ihashcall(cw,10)
  if(n10.ge.0 .and. n10 .le. 1023 .and. cw.ne.mycall13) calls10(n10)=cw

  n12=ihashcall(cw,12)
  if(n12.ge.0 .and. n12 .le. 4095 .and. cw.ne.mycall13) calls12(n12)=cw

  n22=ihashcall(cw,22)
  if(any(ihash22.eq.n22)) then   ! If entry exists, make sure callsign is the most recently received one
    where(ihash22.eq.n22) calls22=cw
    go to 900
  endif

! New entry: move table down, making room for new one at the top
  ihash22(MAXHASH:2:-1)=ihash22(MAXHASH-1:1:-1)

! Add the new entry
  calls22(MAXHASH:2:-1)=calls22(MAXHASH-1:1:-1)
  ihash22(1)=n22
  calls22(1)=cw
  if(nzhash.lt.MAXHASH) nzhash=nzhash+1
900 continue
  return
end subroutine save_hash_call

subroutine normalize_hash_call(c13,cw,ok)

  character(len=*), intent(in) :: c13
  character(len=13), intent(out) :: cw
  logical, intent(out) :: ok
  integer :: i

  cw=c13
  ok=.false.
  if(cw(1:1).eq.' ' .or. cw(1:5).eq.'<...>') return
  if(cw(1:1).eq.'<') then
     if(.not.pack77_valid_hash_call_token(cw)) return
     cw=cw(2:)
     i=index(cw,'>')
     if(i.gt.0) cw(i:)='         '
  endif
  if(len(trim(cw)) .lt. 3) return
  ok=.true.

  return
end subroutine normalize_hash_call

subroutine stage_hash_call(hash_facts,c13)

  type(pack77_hash_facts), intent(inout) :: hash_facts
  character(len=*), intent(in) :: c13
  character(len=13) :: cw
  logical :: ok

! Do not update the global hash table while testing encode candidates. The
! accepted candidate's facts are committed once by pack77.
  call normalize_hash_call(c13,cw,ok)
  if(.not.ok) return
  if(hash_facts%ncalls.ge.size(hash_facts%calls)) return
  hash_facts%ncalls=hash_facts%ncalls+1
  hash_facts%calls(hash_facts%ncalls)=cw

  return
end subroutine stage_hash_call

subroutine pack77_hash_index(hash_facts,token,nbits,n,ok)
  type(pack77_hash_facts), intent(inout) :: hash_facts
  character(len=*), intent(in) :: token
  integer, intent(in) :: nbits
  integer, intent(out) :: n
  logical, intent(out) :: ok
  character(len=13) :: hash_call

  call normalize_hash_call(token,hash_call,ok)
  if(.not.ok) return
  n=ihashcall(hash_call,nbits)
  call stage_hash_call(hash_facts,hash_call)

  return
end subroutine pack77_hash_index

subroutine commit_hash_facts(hash_facts)

  type(pack77_hash_facts), intent(in) :: hash_facts
  integer :: i,n10,n12,n22

! All hash-producing encode paths feed the same shared tables here.
  do i=1,hash_facts%ncalls
     call save_hash_call(hash_facts%calls(i),n10,n12,n22)
  enddo

  return
end subroutine commit_hash_facts

character(len=37) function pack77_expand_dx_macro(msg0,dx_base) result(msg)
  character(len=*), intent(in) :: msg0,dx_base
  character(len=37) :: msg1
  integer :: i1

  msg=msg0
  msg1=adjustl(msg0)
  if(.not.pack77_first_word_is_dx_macro(msg1)) return
  if(len_trim(dx_base).eq.0) return

  i1=index(msg1,' ')
  if(i1.gt.0) then
     msg=trim(dx_base)//' '//adjustl(msg1(i1+1:))
  else
     msg=trim(dx_base)
  endif
end function pack77_expand_dx_macro

logical function pack77_first_word_is_dx_macro(msg0) result(is_macro)
  character(len=*), intent(in) :: msg0
  character(len=37) :: first_word,msg1
  integer :: i,i1

  msg1=adjustl(msg0)
  if(len_trim(msg1).eq.0) then
     is_macro=.false.
     return
  endif

  i1=index(msg1,' ')
  if(i1.gt.1) then
     first_word=msg1(1:i1-1)
  else
     first_word=trim(msg1)
  endif

  do i=1,len_trim(first_word)
     if(first_word(i:i).ge.'a' .and. first_word(i:i).le.'z') &
          first_word(i:i)=char(ichar(first_word(i:i))-32)
  enddo

  is_macro=first_word.eq.'$DX' .or. first_word.eq.'$DXCALL'
end function pack77_first_word_is_dx_macro

logical function pack77_failed_preferred_wspr_shape(msg0,options) result(reject)

  character(len=*), intent(in) :: msg0
  type(pack77_options), intent(in), optional :: options
  character(len=13) :: source_tokens(2)
  integer :: nwords
  logical :: split_ok

! FST4W callers use this option to request a WSPR/50-bit payload, not merely
! to try WSPR first. If the source has WSPR shape but is not valid WSPR, reject
! instead of falling through to another pack77 family or free text.
  reject=.false.
  if(.not.present(options)) return
  if(.not.options%prefer_wspr_50bit) return

  call pack77_split_source_tokens(msg0,2,nwords,source_tokens,split_ok)
  reject=split_ok .and. nwords.eq.2 .and. &
       len_trim(source_tokens(1)).ge.5 .and. &
       len_trim(source_tokens(1)).le.12 .and. &
       len_trim(source_tokens(2)).le.6
end function pack77_failed_preferred_wspr_shape

type(pack77_encode_core_result) function pack77_encode_core(msg0,options) &
     result(encoded)

  character*37 msg,msg0
  character(len=13) :: source_tokens(19)
  type(pack77_options), intent(in), optional :: options
  type(pack77_candidate) :: candidate
  character(len=6) :: dx_macro_base
  integer :: nwords
  logical prefer_wspr_50bit
  logical expand_dx_macro
  logical split_ok
  logical word_overflow
  logical starts_with_special_token

  encoded%result=pack77_no_match()
  prefer_wspr_50bit=.false.
  expand_dx_macro=.false.
  dx_macro_base=''
  if(present(options)) then
     prefer_wspr_50bit=options%prefer_wspr_50bit
     expand_dx_macro=options%expand_dx_macro
     dx_macro_base=options%dx_macro_base
  endif
  msg=msg0
  if(expand_dx_macro) msg=pack77_expand_dx_macro(msg,dx_macro_base)

! Convert msg to upper case; collapse multiple blanks; parse into words.
  call pack77_split_source_tokens(msg,19,nwords,source_tokens,split_ok)
  word_overflow=.not.split_ok
  starts_with_special_token=msg(1:3).eq.'CQ ' .or. msg(1:3).eq.'DE ' .or. &
       msg(1:4).eq.'QRZ '
  if(word_overflow) then
     if(.not.starts_with_special_token) then
        encoded%result=pack77_05(msg)
        if(encoded%result%encoded) return
     endif
     encoded%result=pack77_free_text_exact(msg)
     return
  endif
  if(.not.starts_with_special_token) then

! Check 0.1 (DXpedition mode)
     candidate=pack77_01_candidate(msg)
     if(take_candidate(candidate,msg,encoded)) return
! Check 0.2 (EU VHF contest exchange)
!  call pack77_02(nwords,w,i3,n3,c77)
!  if(i3.ge.0) go to 900

! Check 0.3 and 0.4 (ARRL Field Day exchange)
     candidate=pack77_03_candidate(msg)
     if(take_candidate(candidate,msg,encoded)) return
     if(nwords.lt.2) then

! Check 0.5 (telemetry)
        encoded%result=pack77_05(msg)
        if(encoded%result%encoded) return
     endif
  endif

  candidate=pack77_06_candidate(msg,prefer_wspr_50bit)
  if(take_candidate(candidate,msg,encoded)) return

! Check Type 1 (Standard 77-bit message) or Type 2, with optional "/P"
  candidate=pack77_1_candidate(msg)
  if(take_candidate(candidate,msg,encoded)) return

! Check Type 3 (ARRL RTTY contest exchange)
  candidate=pack77_3_candidate(msg)
  if(take_candidate(candidate,msg,encoded)) return

! Check Type 4 (One nonstandard call and one hashed call)
  candidate=pack77_4_candidate(msg)
  if(take_candidate(candidate,msg,encoded)) return

! Check Type 5 (EU VHF Contest with 2 hashed calls, report, serial, and grid6)
  candidate=pack77_5_candidate(msg)
  if(take_candidate(candidate,msg,encoded)) return

! It defaults to exactly representable free text.
  encoded%result=pack77_free_text_exact(msg)
end function pack77_encode_core

type(pack77_result) function pack77_encode_result(msg0,options) result(encoded)

  character*37 msg0
  type(pack77_options), intent(in), optional :: options
  type(pack77_encode_core_result) :: core
  logical :: record_tx_hashes

  record_tx_hashes=.true.
  if(present(options)) record_tx_hashes=options%record_tx_hashes

  core=pack77_encode_core(msg0,options)
  encoded=core%result
  if(.not.encoded%encoded .and. &
       encoded%status.ne.PACK77_STATUS_INTERNAL_ROUNDTRIP_REJECTED .and. &
       pack77_failed_preferred_wspr_shape(pack77_normalized_message(msg0), &
       options)) encoded=pack77_reject(PACK77_STATUS_PREFERRED_FAMILY_REJECTED)
  if(record_tx_hashes) call commit_hash_facts(core%hash_facts)
end function pack77_encode_result

subroutine pack77(msg0,i3,n3,c77,options,status)
  character(len=*), intent(in) :: msg0
  integer, intent(out) :: i3,n3
  character(len=77), intent(out) :: c77
  type(pack77_options), intent(in), optional :: options
  integer, intent(out), optional :: status
  type(pack77_result) :: packed

  packed=pack77_encode_result(msg0,options)
  i3=packed%i3
  n3=packed%n3
  c77=packed%c77
  if(present(status)) status=packed%status
end subroutine pack77

subroutine pack77_legacy_truncating_fallback(msg0,i3,n3,c77,options,status)
  character(len=*), intent(in) :: msg0
  integer, intent(out) :: i3,n3
  character(len=77), intent(out) :: c77
  type(pack77_options), intent(in), optional :: options
  integer, intent(out), optional :: status
  type(pack77_result) :: packed

  packed=pack77_legacy_truncating_result(msg0,options)
  i3=packed%i3
  n3=packed%n3
  c77=packed%c77
  if(present(status)) status=packed%status
end subroutine pack77_legacy_truncating_fallback

type(pack77_result) function pack77_legacy_truncating_result(msg0,options) &
     result(encoded)
  character(len=*), intent(in) :: msg0
  type(pack77_options), intent(in), optional :: options
  type(pack77_result) :: fallback
  character(len=37) :: msg
  character(len=13) :: payload

  encoded=pack77_encode_result(msg0,options)
  if(encoded%encoded) return
  if(encoded%status.eq.PACK77_STATUS_PREFERRED_FAMILY_REJECTED) return
  if(encoded%status.eq.PACK77_STATUS_INTERNAL_ROUNDTRIP_REJECTED) return

  msg=msg0
  if(present(options)) then
     if(options%expand_dx_macro) msg=pack77_expand_dx_macro(msg, &
          options%dx_macro_base)
  endif
  msg=pack77_normalized_message(msg)
  if(len_trim(msg).lt.1) return

  payload=msg(1:13)
  if(.not.pack77_free_text_alphabet_ok(payload)) return

  fallback=pack77_free_text_accept(payload,PACK77_STATUS_ENCODED)
  if(fallback%encoded) encoded=fallback
end function pack77_legacy_truncating_result

type(pack77_result) function pack77_05(msg) result(encoded)
  character(len=*), intent(in) :: msg
  character*77 c77
  character*37 text
  character*18 c18
  character*1 ch
  integer ntel(3)
  logical ok

  encoded=pack77_no_match()
  text=adjustl(msg)
  n=len_trim(text)
  if(n.lt.1 .or. n.gt.18) return
  do i=1,n
     ch=text(i:i)
     if(ch.ge.'a' .and. ch.le.'z') ch=char(ichar(ch)-32)
     if(index('0123456789ABCDEF',ch).eq.0) return
     text(i:i)=ch
  enddo
  c18=text(1:n)
  c18=adjustr(c18)
  ntel=-99
  read(c18,1005,err=6) ntel
1005 format(3z6)
 6 if(ntel(1).ge.0 .and. ntel(2).ge.0 .and. ntel(3).ge.0) then
     call encode_pack77_telemetry(ntel(1),ntel(2),ntel(3),c77,ok)
     if(ok) then
        encoded=pack77_accept(0,5,c77)
     endif
  endif
end function pack77_05

type(pack77_result) function pack77_free_text_exact(msg) result(encoded)
  character(len=*), intent(in) :: msg
  character*37 msg0

  msg0=pack77_normalized_message(msg)
  if(.not.pack77_exact_free_text_ok(msg0)) then
     encoded=pack77_reject(pack77_free_text_reject_status(msg0))
     return
  endif
  encoded=pack77_free_text_accept(msg0,PACK77_STATUS_ENCODED)
end function pack77_free_text_exact

type(pack77_result) function pack77_free_text_accept(msg,status) result(encoded)
  character(len=*), intent(in) :: msg
  integer, intent(in) :: status
  character*37 msg0
  character*71 text_bits
  character*77 c77
  logical ok

  msg0=msg
  msg0(14:)='                        '
  call packtext77(msg0(1:13),text_bits)
  call encode_pack77_free_text(text_bits,c77,ok)
  encoded=pack77_no_match()
  if(ok) then
     encoded=pack77_accept(0,0,c77)
     encoded%status=status
  endif
end function pack77_free_text_accept

integer function pack77_free_text_reject_status(msg) result(status)
  character(len=*), intent(in) :: msg

  if(.not.pack77_free_text_alphabet_ok(msg)) then
     status=PACK77_STATUS_FREE_TEXT_INVALID
  else if(len_trim(msg).gt.13) then
     status=PACK77_STATUS_FREE_TEXT_TOO_LONG
  else
     status=PACK77_STATUS_FREE_TEXT_INVALID
  endif
end function pack77_free_text_reject_status

logical function pack77_exact_free_text_ok(msg) result(ok)
  character(len=*), intent(in) :: msg

  ok=.false.
  if(len_trim(msg).gt.13) return
  ok=pack77_free_text_alphabet_ok(msg(1:len_trim(msg)))
end function pack77_exact_free_text_ok

logical function pack77_free_text_alphabet_ok(msg) result(ok)
  character(len=*), intent(in) :: msg
  character(len=42), parameter :: alphabet=' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?'
  integer :: i

  ok=.false.
  do i=1,len_trim(msg)
     if(index(alphabet,msg(i:i)).eq.0) return
  enddo
  ok=.true.
end function pack77_free_text_alphabet_ok

! Must mirror the character canonicalization of pack77_split_source_tokens
! (NUL->space, uppercase fold, blank collapse) so free text and structured
! parsing agree on the canonical form.
character(len=37) function pack77_normalized_message(msg) result(normalized)
  character(len=*), intent(in) :: msg
  character(len=1) :: c,c0
  integer :: i,j,iz

  normalized='                                     '
  c0=' '
  j=0
  iz=len_trim(msg)
  do i=1,min(iz,len(normalized))
     c=msg(i:i)
     if(ichar(c).eq.0) c=' '
     if(c.eq.' ' .and. c0.eq.' ') cycle
     if(c.ge.'a' .and. c.le.'z') c=char(ichar(c)-32)
     j=j+1
     normalized(j:j)=c
     c0=c
  enddo
end function pack77_normalized_message

subroutine unpack77(c77,nrx,msg,unpk77_success)
!
! nrx=1 when unpacking a received message
! nrx=0 when unpacking a to-be-transmitted message
!
  character*77 c77
  character*37 msg
  integer, intent(in) :: nrx
  logical unpk77_success
  character*13 mycall13_0,dxcall13_0
  integer hashmy10,hashmy12,hashmy22,hashdx10,hashdx12,hashdx22
  logical dxcall13_set,mycall13_set
  type(unpack77_context) :: context
  type(unpack77_effect_policy) :: policy

  data dxcall13_set/.false./
  data mycall13_set/.false./
  data mycall13_0/''/
  data dxcall13_0/' '/

  save hashmy10,hashmy12,hashmy22,hashdx10,hashdx12,hashdx22
  save dxcall13_set,mycall13_set,mycall13_0,dxcall13_0

  if(mycall13.ne.mycall13_0) then
    if(len(trim(mycall13)).gt.2) then
       mycall13_set=.true.
       mycall13_0=mycall13
       call save_hash_call(mycall13,hashmy10,hashmy12,hashmy22)
    else
       mycall13_set=.false.
    endif
  endif

  if(dxcall13.ne.dxcall13_0) then
    if(len(trim(dxcall13)).gt.2) then
      dxcall13_set=.true.
      dxcall13_0=dxcall13
      hashdx10=ihashcall(dxcall13,10)
      hashdx12=ihashcall(dxcall13,12)
      hashdx22=ihashcall(dxcall13,22)
    else
      dxcall13_set=.false.
    endif
  endif

  context%nrx=nrx
  context%configured=.false.
  context%strict_var_guards=.false.
  context%mycall_set=mycall13_set
  context%dxcall_set=dxcall13_set
  context%mycall=mycall13
  context%dxcall=dxcall13
  context%hashmy10=hashmy10
  context%hashmy12=hashmy12
  context%hashmy22=hashmy22
  context%hashdx10=hashdx10

  policy=unpack77_effect_policy()
  call run_unpack77(c77,context,policy,msg,unpk77_success)

  return
end subroutine unpack77

subroutine pack28(c13,n28)
! Callers must pass a well-formed callsign or special/hash token. Malformed
! tokens are encoded as 22-bit hashes, never coerced to standard calls, per the
! c28 shape predicate in packjt77_grammar.
  character*13 c13
  integer, intent(out) :: n28
  type(pack77_hash_facts) :: hash_facts

  call pack28_core(c13,n28,hash_facts)
  call commit_hash_facts(hash_facts)

  return
end subroutine pack28

subroutine pack28_core(c13,n28,hash_facts)

! Pack a special token, a 22-bit hash code, or a valid base call into a 28-bit
! integer.

  character*13 c13
  integer, intent(out) :: n28
  type(pack77_hash_facts), intent(inout) :: hash_facts
  character*6 callsign
  character*1 c
  character*4 c4
  character*37 a1
  character*36 a2
  character*10 a3
  character*27 a4
  data a1/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
  data a2/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
  data a3/'0123456789'/
  data a4/' ABCDEFGHIJKLMNOPQRSTUVWXYZ'/

  n28=-1
! Work-around for Swaziland prefix:
  if(c13(1:4).eq.'3DA0') callsign='3D0'//c13(5:7)
! Work-around for Guinea prefixes:
  if(c13(1:2).eq.'3X' .and. c13(3:3).ge.'A' .and.          &
       c13(3:3).le.'Z') callsign='Q'//c13(3:6)

! Check for special tokens first
  if(c13(1:3).eq.'DE ') then
     n28=0
     go to 900
  endif

  if(c13(1:4).eq.'QRZ ') then
     n28=1
     go to 900
  endif

  if(c13(1:3).eq.'CQ ') then
     n28=2
     go to 900
  endif

  if(c13(1:3).eq.'CQ_') then
     n=len(trim(c13))
! The modifier grammar (1-4 letters or exactly 3 digits) is owned by
! pack77_valid_cq_modifier; only the number-space arithmetic lives here.
     if(n.ge.4 .and. pack77_valid_cq_modifier(c13(4:n))) then
        if(pack77_all_digits(c13(4:n))) then
           nqsy=100*(iachar(c13(4:4))-iachar('0')) + &
                10*(iachar(c13(5:5))-iachar('0')) + &
                iachar(c13(6:6))-iachar('0')
           n28=3+nqsy
           go to 900
        endif
        c4=c13(4:n)
        c4=adjustr(c4)
        m=0
        do i=1,4
           j=0
           c=c4(i:i)
           if(c.ge.'A' .and. c.le.'Z') j=ichar(c)-ichar('A')+1
           m=27*m + j
        enddo
        n28=3+1000+m
        go to 900
     endif
  endif

! Check for <...> callsign
  if(c13(1:1).eq.'<')then
     if(.not.pack77_valid_hash_call_token(c13)) then
        n28=PACK77_NTOKENS
        go to 900
     endif
     call stage_hash_call(hash_facts,c13)
     i2=index(c13,'>')
     c13=c13(2:i2-1)
     n22=ihashcall(c13,22)
     n28=PACK77_NTOKENS + n22
     go to 900
  endif

! Check for standard callsign
  if(.not.pack77_c28_standard_shape(c13)) then
! Treat this as a nonstandard callsign: compute its 22-bit hash
     call stage_hash_call(hash_facts,c13)
     n22=ihashcall(c13,22)
     n28=PACK77_NTOKENS + n22
     go to 900
  endif

  n=len(trim(c13))
  do i=n,2,-1
     if(pack77_is_digit_char(c13(i:i))) exit
  enddo
  iarea=i
! This is a standard callsign
  call stage_hash_call(hash_facts,c13)
  if(iarea.eq.2) callsign=' '//c13(1:5)
  if(iarea.eq.3) callsign=c13(1:6)
  i1=index(a1,callsign(1:1))-1
  i2=index(a2,callsign(2:2))-1
  i3=index(a3,callsign(3:3))-1
  i4=index(a4,callsign(4:4))-1
  i5=index(a4,callsign(5:5))-1
  i6=index(a4,callsign(6:6))-1
  if(i1.lt.0 .or. i2.lt.0 .or. i3.lt.0 .or. i4.lt.0 .or. &
       i5.lt.0 .or. i6.lt.0) then
     n22=ihashcall(c13,22)
     n28=PACK77_NTOKENS + n22
     go to 900
  endif
  n28=36*10*27*27*27*i1 + 10*27*27*27*i2 + 27*27*27*i3 + 27*27*i4 + &
       27*i5 + i6
  n28=n28 + PACK77_NTOKENS + PACK77_MAX22

900 n28=iand(n28,ishft(1,28)-1)
  return
end subroutine pack28_core

subroutine unpack28(n28_0,c13,success)
  integer, intent(in) :: n28_0
  character*13 c13
  logical success

  call unpack28_core(n28_0,c13,success,.false.)

  return
end subroutine unpack28

subroutine unpack28_core(n28_0,c13,success,reject_two_digit_prefix)
  logical success,callok
  logical, intent(in) :: reject_two_digit_prefix
  character*13 c13
  character*37 c1
  character*36 c2
  character*10 c3
  character*27 c4
  data c1/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
  data c2/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'/
  data c3/'0123456789'/
  data c4/' ABCDEFGHIJKLMNOPQRSTUVWXYZ'/

  success=.true.
  n28=n28_0
  if(n28.lt.PACK77_NTOKENS) then
! Special tokens DE, QRZ, CQ, CQ_nnn, CQ_aaaa
     if(n28.eq.0) c13='DE           '
     if(n28.eq.1) c13='QRZ          '
     if(n28.eq.2) c13='CQ           '
     if(n28.le.2) go to 900
     if(n28.le.1002) then
        write(c13,1002) n28-3
1002    format('CQ_',i3.3)
        go to 900
     endif
     if(n28.le.532443) then
        n=n28-1003
        n0=n
        i1=n/(27*27*27)
        n=n-27*27*27*i1
        i2=n/(27*27)
        n=n-27*27*i2
        i3=n/27
        i4=n-27*i3
        c13=c4(i1+1:i1+1)//c4(i2+1:i2+1)//c4(i3+1:i3+1)//c4(i4+1:i4+1)
        c13=adjustl(c13)
        c13='CQ_'//c13(1:10)
        go to 900
     endif
  endif
  n28=n28-PACK77_NTOKENS
  if(n28.lt.PACK77_MAX22) then
! This is a 22-bit hash of a callsign
     n22=n28
     call hash22(n22,c13)     !Retrieve callsign from hash table
     go to 900
  endif

! Standard callsign
  n=n28 - PACK77_MAX22
  i1=n/(36*10*27*27*27)
  n=n-36*10*27*27*27*i1
  i2=n/(10*27*27*27)
  n=n-10*27*27*27*i2
  i3=n/(27*27*27)
  n=n-27*27*27*i3
  i4=n/(27*27)
  n=n-27*27*i4
  i5=n/27
  i6=n-27*i5
  c13=c1(i1+1:i1+1)//c2(i2+1:i2+1)//c3(i3+1:i3+1)//c4(i4+1:i4+1)//     &
       c4(i5+1:i5+1)//c4(i6+1:i6+1)
  c13=adjustl(c13)

  if(.not.callok(trim(c13))) then
     ! Keep the legacy placeholder text for callers that still inspect c13
     ! when success is false.
     c13='QU1RK'
     success=.false.
  endif

900 i0=index(c13,' ')
  if((i0.ne.0 .and. i0.lt.len(trim(c13))) .or. &
       (reject_two_digit_prefix .and. pack77_is_digit_char(c13(1:1)) .and. &
       pack77_is_digit_char(c13(2:2)))) then
     ! Keep the legacy placeholder text for callers that still inspect c13
     ! when success is false.
     c13='QU1RK'
     success=.false.
  endif

  return
end subroutine unpack28_core

subroutine split77(msg,nwords,nw,w)

! Convert msg to upper case; collapse multiple blanks; parse into words.

  character*37 msg
  character*13 w(19)
  character*1 c,c0
  character*6 bcall_1
  logical ok1
  integer nw(19)

  iz=len(trim(msg))
  j=0
  k=0
  n=0
  c0=' '
  w='             '
  nw=0
  do i=1,iz
     if(ichar(msg(i:i)).eq.0) msg(i:i)=' '
     c=msg(i:i)                                 !Single character
     if(c.eq.' ' .and. c0.eq.' ') cycle         !Skip leading/repeated blanks
     if(c.ne.' ' .and. c0.eq.' ') then
        k=k+1                                   !New word
        n=0
     endif
     j=j+1                                      !Index in msg
     n=n+1                                      !Index in word
     if(c.ge.'a' .and. c.le.'z') c=char(ichar(c)-32)  !Force upper case
     msg(j:j)=c
     if(k.le.19) then
        nw(k)=n
        if(n.le.13) w(k)(n:n)=c                 !Copy character c into word
     endif
     c0=c
  enddo
  iz=j                                          !Message length
  nwords=k                                      !Number of words in msg
  if(nwords.le.0) go to 900
  do i=1,min(nwords,19)
     if(nw(i).le.13) nw(i)=len(trim(w(i)))
  enddo
  msg(iz+1:)='                                     '
  if(nwords.lt.3) go to 900
  call chkcall(w(3),bcall_1,ok1)
  if(ok1 .and. w(1)(1:3).eq.'CQ ' .and. &
       pack77_valid_cq_modifier(w(2))) then
     w(1)='CQ_'//w(2)(1:10)             !Make "CQ " into "CQ_"
     w(2:12)=w(3:13)                    !Move all remaining words down by one
     nwords=nwords-1
     do i=1,nwords
        nw(i)=len(trim(w(i)))
     enddo
  endif

900 return
end subroutine split77


subroutine packtext77(c13,c71)

  character*13 c13,w
  character*71 c71
  character*42 c
  character*1 qa(10),qb(10)
  data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?'/

  call mp_short_init
  qa=char(0)
  w=adjustr(c13)
  do i=1,13
     j=index(c,w(i:i))-1
     if(j.lt.0) j=0
     call mp_short_mult(qb,qa(2:10),9,42)     !qb(1:9)=42*qa(2:9)
     call mp_short_add(qa,qb(2:10),9,j)      !qa(1:9)=qb(2:9)+j
  enddo

  write(c71,1010) qa(2:10)
1010 format(b7.7,8b8.8)

  return
end subroutine packtext77

subroutine unpacktext77(c71,c13)

  integer*1   ia(10)
  character*1 qa(10),qb(10)
  character*13 c13
  character*71 c71
  character*42 c
  equivalence (qa,ia),(qb,ib)
  data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?'/

  qa(1)=char(0)
  read(c71,1010) qa(2:10)
1010 format(b7.7,8b8.8)

  do i=13,1,-1
     call mp_short_div(qb,qa(2:10),9,42,ir)
     c13(i:i)=c(ir+1:ir+1)
     qa(2:10)=qb(1:9)
  enddo

  return
end subroutine unpacktext77

subroutine mp_short_ops(w,u)
  character*1 w(*),u(*)
  integer i,ireg,j,n,ir,iv,ii1,ii2
  character*1 creg(4)
  save ii1,ii2
  equivalence (ireg,creg)

  entry mp_short_init
  ireg=256*ichar('2')+ichar('1')
  do j=1,4
     if (creg(j).eq.'1') ii1=j
     if (creg(j).eq.'2') ii2=j
  enddo
  return

  entry mp_short_add(w,u,n,iv)
  ireg=256*iv
  do j=n,1,-1
     ireg=ichar(u(j))+ichar(creg(ii2))
     w(j+1)=creg(ii1)
  enddo
  w(1)=creg(ii2)
  return

  entry mp_short_mult(w,u,n,iv)
  ireg=0
  do j=n,1,-1
     ireg=ichar(u(j))*iv+ichar(creg(ii2))
     w(j+1)=creg(ii1)
  enddo
  w(1)=creg(ii2)
  return

  entry mp_short_div(w,u,n,iv,ir)
  ir=0
  do j=1,n
     i=256*ir+ichar(u(j))
     w(j)=char(i/iv)
     ir=mod(i,iv)
  enddo
  return

  return
end subroutine mp_short_ops

subroutine add_call_to_recent_calls(callsign)

  character*13 callsign
  logical ladd

! only add if the callsign is not already on the list
  ladd=.true.
  do i=1,MAXRECENT-1 ! if callsign is at the end of the list add it again
     if(recent_calls(i).eq.callsign) ladd=.false.
  enddo

  if(ladd) then
     do i=MAXRECENT,2,-1
        recent_calls(i)=recent_calls(i-1)
     enddo
     recent_calls(1)=callsign
  endif

! Recent-call ordering is separate from hash storage; decode paths stage both
! effects explicitly when a call should become recent and hash-resolvable.

  return
end subroutine add_call_to_recent_calls



subroutine queue_hash_call_for_thread(c13,nthr)
  character*13 c13
  integer, intent(in) :: nthr

! Configured/var decode workers queue hash facts; fillhashvar later folds them
! into the shared calls10/calls12/calls22 tables.
  call queue_call_for_thread(c13,nthr,queued_calls_by_thread, &
       nqueued_calls_by_thread)

  return
end subroutine queue_hash_call_for_thread

subroutine queue_recent_call_for_thread(c13,nthr)
  character*13 c13
  integer, intent(in) :: nthr

  call queue_call_for_thread(c13,nthr,queued_recent_calls_by_thread, &
       nqueued_recent_calls_by_thread)

  return
end subroutine queue_recent_call_for_thread

subroutine queue_call_for_thread(c13,nthr,queued_calls,nqueued_calls)
  character(len=*), intent(in) :: c13
  integer, intent(in) :: nthr
  character(len=13), intent(inout) :: queued_calls(:)
  integer, intent(inout) :: nqueued_calls(:)
  character*13 cw

  if(nthr.lt.1 .or. nthr.gt.24) return
  cw=c13
  if(cw(1:1).eq.' ' .or. cw(1:5).eq.'<...>') return
  if(cw(1:1).eq.'<') cw=cw(2:)
  i=index(cw,'>')
  if(i.gt.0) cw(i:)='         '
  if(len(trim(cw)) .lt. 3) return

  nposition=thread_call_index(nthr)+nqueued_calls(nthr)
  if(nposition.lt.thread_call_index(nthr+1)) then
    nqueued_calls(nthr)=nqueued_calls(nthr)+1
    queued_calls(nposition+1)=cw
  endif

  return
end subroutine queue_call_for_thread

subroutine fold_queued_recent_calls(numthreads)

  integer, intent(in) :: numthreads
  character*13 cw

  do i=1,numthreads
    do m=1,nqueued_recent_calls_by_thread(i)
      nposition=thread_call_index(i)+m
      cw=queued_recent_calls_by_thread(nposition)
      call add_call_to_recent_calls(cw)
    enddo
  enddo

  return
end subroutine fold_queued_recent_calls

subroutine sync_configured_calls_for_decode_start()

! Capture the operator-configured calls at decode-cycle boundaries. These
! hashes let configured decodes resolve hash-only messages without a separate
! var hash table.
  if(mycall13.ne.mycall13_configured_prev) then
    if(len(trim(mycall13)).gt.2) then
       call set_configured_mycall_from_current()
    else
       call clear_configured_mycall()
    endif
  endif
  call sync_configured_mycall_value_from_current()

  if(dxcall13.ne.dxcall13_configured_prev) then
    if(len(trim(dxcall13)).gt.2) then
       call set_configured_dxcall_from_current()
    else
       call clear_configured_dxcall()
    endif
  endif
  call sync_configured_dxcall_value_from_current()

  return
end subroutine sync_configured_calls_for_decode_start

subroutine set_configured_mycall_from_current()

  mycall13_configured_set=.true.
  mycall13_configured=mycall13
  mycall13_configured_prev=mycall13
  hashmy10_configured=ihashcall(mycall13,10)
  hashmy12_configured=ihashcall(mycall13,12)
  hashmy22_configured=ihashcall(mycall13,22)
  call save_hash_call(mycall13,ndum10,ndum12,ndum22)

  return
end subroutine set_configured_mycall_from_current

subroutine clear_configured_mycall()

  mycall13_configured_set=.false.
  mycall13_configured='             '
  mycall13_configured_prev='             '
  hashmy10_configured=-1
  hashmy12_configured=-1
  hashmy22_configured=-1

  return
end subroutine clear_configured_mycall

subroutine sync_configured_mycall_value_from_current()

  if(mycall13_configured_set) mycall13_configured=mycall13

  return
end subroutine sync_configured_mycall_value_from_current

subroutine set_configured_dxcall_from_current()

  dxcall13_configured_set=.true.
  dxcall13_configured=dxcall13
  dxcall13_configured_prev=dxcall13
  hashdx10_configured=ihashcall(dxcall13,10)
  ! Seed the manual DX Call before decode so first-cycle hash references to it
  ! can resolve even if the call has not yet been decoded over the air.
  call queue_hash_call_for_thread(dxcall13,1)

  return
end subroutine set_configured_dxcall_from_current

subroutine clear_configured_dxcall()

  dxcall13_configured_set=.false.
  dxcall13_configured='             '
  dxcall13_configured_prev='             '
  hashdx10_configured=-1

  return
end subroutine clear_configured_dxcall

subroutine sync_configured_dxcall_value_from_current()

  if(dxcall13_configured_set) dxcall13_configured=dxcall13

  return
end subroutine sync_configured_dxcall_value_from_current

type(configured_decode_state) function current_configured_decode_state() &
     result(state)

  state%mycall_set=mycall13_configured_set
  state%dxcall_set=dxcall13_configured_set
  state%mycall=mycall13_configured
  state%dxcall=dxcall13_configured
  state%hashmy10=hashmy10_configured
  state%hashmy12=hashmy12_configured
  state%hashmy22=hashmy22_configured
  state%hashdx10=hashdx10_configured

  return
end function current_configured_decode_state

type(unpack77_context) function configured_unpack77_context(nrx) result(context)
  integer, intent(in) :: nrx
  type(configured_decode_state) :: state

  state=current_configured_decode_state()
  context%nrx=nrx
  context%configured=.true.
  context%strict_var_guards=.true.
  context%mycall_set=state%mycall_set
  context%dxcall_set=state%dxcall_set
  context%mycall=state%mycall
  context%dxcall=state%dxcall
  context%hashmy10=state%hashmy10
  context%hashmy12=state%hashmy12
  context%hashmy22=state%hashmy22
  context%hashdx10=state%hashdx10
end function configured_unpack77_context

type(unpack77_effect_policy) function configured_unpack77_effect_policy(options) &
     result(policy)
  type(unpack77_options), intent(in) :: options

  policy=unpack77_effect_policy(queue_by_thread=.true., &
       nthr_hash=options%thread_index,nthr_recent=options%thread_index)
  policy%record_hashes=options%record_hashes
  policy%record_recent_calls=options%record_recent_calls
end function configured_unpack77_effect_policy

subroutine unpack77_configured(c77,nrx,msg,unpk77_successvar,options)
  character*77 c77
  character*37 msg
  integer, intent(in) :: nrx
  logical unpk77_successvar
  type(unpack77_options), intent(in) :: options
  type(unpack77_context) :: context
  type(unpack77_effect_policy) :: policy

  context=configured_unpack77_context(nrx)
  policy=configured_unpack77_effect_policy(options)
  call run_unpack77(c77,context,policy,msg,unpk77_successvar)

  return
end subroutine unpack77_configured

subroutine run_unpack77(c77,context,policy,msg,unpk77_success)
  character*77 c77
  type(unpack77_context), intent(in) :: context
  type(unpack77_effect_policy), intent(in) :: policy
  character*37 msg
  logical unpk77_success
  type(unpack77_core_result) :: decoded

  decoded=unpack77_core(c77,context)
  msg=decoded%msg
  unpk77_success=decoded%success
  if(unpk77_success) call apply_unpack77_effects(decoded%effects,policy)

  return
end subroutine run_unpack77

type(unpack77_core_result) function unpack77_core(c77,context) result(decoded)

  character*77 c77
  type(unpack77_context), intent(in) :: context
  logical ok

  decoded=unpack77_core_result()
  decoded%msg=repeat(' ',37)
  call decode_pack77_tag(c77,decoded%i3,decoded%n3,ok)
  if(.not.ok) then
     decoded%msg='failed unpack'
     return
  endif
  decoded%success=.true.

  if(decoded%i3.eq.0) then
     call unpack77_decode_i3_0(c77,context,decoded)
  else if(decoded%i3.eq.1 .or. decoded%i3.eq.2) then
     call unpack77_decode_type12(c77,context,decoded)
  else if(decoded%i3.eq.3) then
     call unpack77_decode_type3(c77,context,decoded)
  else if(decoded%i3.eq.4) then
     call unpack77_decode_type4(c77,context,decoded)
  else if(decoded%i3.eq.5) then
     call unpack77_decode_type5(c77,context,decoded)
  else if(decoded%i3.ge.6) then
     decoded%success=.false.
  endif
! CQ addressed to a hashed/nonstandard call is not a legal message form in
! any type; preserved verbatim from the legacy decoder.
  if(decoded%msg(1:4).eq.'CQ <') decoded%success=.false.

  return
end function unpack77_core

subroutine unpack77_decode_i3_0(c77,context,decoded)
  character*77 c77
  type(unpack77_context), intent(in) :: context
  type(unpack77_core_result), intent(inout) :: decoded
  integer ntel(3)
  character*13 call_1,call_2,call_3,call_1a
  character*3 crpt,cntx,cpfx
  character*6 grid6
  character*4 grid4
  logical unpk28_success,unpkg4_success,ok,is_prefix
  type(pack77_free_text_fields) :: free_text_fields
  type(pack77_telemetry_fields) :: telemetry_fields
  type(pack77_dxpedition_fields) :: dxpedition_fields
  type(pack77_field_day_fields) :: field_day_fields
  type(pack77_wspr_type1_fields) :: wspr1_fields
  type(pack77_wspr_type2_fields) :: wspr2_fields
  type(pack77_wspr_type3_fields) :: wspr3_fields
  integer :: n28a,n28b

  if(decoded%n3.eq.0) then
! 0.0  Free text
     call decode_pack77_free_text(c77,free_text_fields,ok,.true.)
     if(.not.ok) then
        decoded%success=.false.
        return
     endif
     call unpacktext77(free_text_fields%text_bits,decoded%msg(1:13))
     decoded%msg(14:)='                        '
     decoded%msg=adjustl(decoded%msg)
     if(decoded%msg(1:1).eq.' ') decoded%success=.false.

  else if(decoded%n3.eq.1) then
! 0.1  K1ABC RR73; W9XYZ <KH1/KH7Z> -11   28 28 10 5       71   DXpedition Mode
     call decode_pack77_dxpedition(c77,dxpedition_fields,ok,.true.)
     if(.not.ok) then
        decoded%success=.false.
        return
     endif
     n28a=dxpedition_fields%n28a
     n28b=dxpedition_fields%n28b
     n10=dxpedition_fields%n10
     n5=dxpedition_fields%n5
     call record_configured_n28_pair(context,n28a,n28b)
     irpt=2*n5 - 30
     crpt=pack77_format_snr_report(irpt)
     call unpack28_for_context(context,n28a,call_1,unpk28_success)
     if(.not.unpk28_success .or. n28a.le.2) decoded%success=.false.
     call unpack28_for_context(context,n28b,call_2,unpk28_success)
     if(.not.unpk28_success .or. n28b.le.2) decoded%success=.false.
     call hash10(n10,call_3)
     if(context%nrx.eq.1) then
        if(context%dxcall_set .and. len(trim(context%dxcall)).ge.3 .and. &
             context%hashdx10.eq.n10) then
           call_3='<'//trim(context%dxcall)//'>'
        endif
     endif
     if(context%nrx.eq.0 .and. context%mycall_set .and. &
          n10.eq.context%hashmy10) call_3='<'//trim(context%mycall)//'>'
     decoded%msg=trim(call_1)//' RR73; '//trim(call_2)//' '//trim(call_3)//' '//crpt

  else if(decoded%n3.eq.2) then
     decoded%success=.false.

  else if(decoded%n3.eq.3 .or. decoded%n3.eq.4) then
! 0.3   WA9XYZ KA1ABC R 16A EMA            28 28 1 4 3 7    71   ARRL Field Day
! 0.4   WA9XYZ KA1ABC R 32A EMA            28 28 1 4 3 7    71   ARRL Field Day
     if(decoded%n3.eq.3) then
        call decode_pack77_field_day_low(c77,field_day_fields,ok,.true.)
     else
        call decode_pack77_field_day_high(c77,field_day_fields,ok,.true.)
     endif
     if(.not.ok) then
        decoded%success=.false.
        return
     endif
     n28a=field_day_fields%n28a
     n28b=field_day_fields%n28b
     ir=field_day_fields%ir
     intx=field_day_fields%intx
     nclass=field_day_fields%nclass
     isec=field_day_fields%isec
     call record_configured_n28_pair(context,n28a,n28b)
     if(isec.gt.PACK77_NSEC .or. isec.lt.1) then
         decoded%success=.false.
         isec=1
     endif
     call unpack28_for_context(context,n28a,call_1,unpk28_success)
     if(.not.unpk28_success .or. n28a.le.2) decoded%success=.false.
     call unpack28_for_context(context,n28b,call_2,unpk28_success)
     if(.not.unpk28_success .or. n28b.le.2) decoded%success=.false.
     ntx=intx+1
     if(decoded%n3.eq.4) ntx=ntx+16
     write(cntx(1:2),'(i2)') ntx
     cntx(3:3)=char(ichar('A')+nclass)
     decoded%msg=pack77_format_exchange_message(call_1,call_2,ir, &
          trim(adjustl(cntx))//' '//pack77_arrl_section_name(isec),.true.)

  else if(decoded%n3.eq.5) then
! 0.5   0123456789abcdef01                 71               71   Telemetry (18 hex)
     call decode_pack77_telemetry(c77,telemetry_fields,ok,.true.)
     if(.not.ok) then
        decoded%success=.false.
        return
     endif
     ntel=(/telemetry_fields%ntel1,telemetry_fields%ntel2,telemetry_fields%ntel3/)
     write(decoded%msg,1007) ntel
1007 format(3z6.6)

  else if(decoded%n3.eq.6) then
     select case(pack77_wspr_payload_type(c77))
     case(2)
        call decode_pack77_wspr_type2(c77,wspr2_fields,ok,.true.)
        if(.not.ok) then
           decoded%success=.false.
           return
        endif
! WSPR Type 2
        n28=wspr2_fields%n28
        npfx=wspr2_fields%npfx
        idbm=nint(wspr2_fields%idbm*10.0/3.0)
        call unpack28_for_context(context,n28,call_1,unpk28_success)
        if(.not.unpk28_success) decoded%success=.false.
        write(crpt,'(i3)') idbm
        call pack77_wspr_affix_text(npfx,cpfx,is_prefix,ok)
        if(.not.ok) then
           decoded%success=.false.
           return
        endif
        if(is_prefix) then
           call_1a=trim(adjustl(cpfx))//'/'//trim(call_1)
        else
           call_1a=trim(call_1)//'/'//trim(adjustl(cpfx))
        endif
        decoded%msg=trim(call_1a)//' '//trim(adjustl(crpt))
        call stage_unpack_hash_call(decoded%effects,call_1a)

     case(1)
        call decode_pack77_wspr_type1(c77,wspr1_fields,ok,.true.)
        if(.not.ok) then
           decoded%success=.false.
           return
        endif
! WSPR Type 1
        n28=wspr1_fields%n28
        igrid4=wspr1_fields%igrid4
        idbm=nint(wspr1_fields%idbm*10.0/3.0)
        call unpack28_for_context(context,n28,call_1,unpk28_success)
        if(.not.unpk28_success) decoded%success=.false.
        call to_grid4(igrid4,grid4,unpkg4_success)
        if(.not.unpkg4_success) decoded%success=.false.
        write(crpt,'(i3)') idbm
        decoded%msg=trim(call_1)//' '//grid4//' '//trim(adjustl(crpt))
        call stage_unpack_hash_call(decoded%effects,call_1)

     case(3)
        call decode_pack77_wspr_type3(c77,wspr3_fields,ok,.true.)
        if(.not.ok) then
           decoded%success=.false.
           return
        endif
! WSPR Type 3
        n22=wspr3_fields%n22
        igrid6=wspr3_fields%igrid6
        n28=n22+PACK77_NTOKENS
        call unpack28_for_context(context,n28,call_1,unpk28_success)
        if(.not.unpk28_success) decoded%success=.false.
        call to_grid(igrid6,grid6,unpkg4_success)
        if(.not.unpkg4_success) decoded%success=.false.
        decoded%msg=trim(call_1)//' '//grid6

     case default
        decoded%success=.false.
        return
     end select

  else if(decoded%n3.gt.6) then
     decoded%success=.false.
  endif

  return
end subroutine unpack77_decode_i3_0

subroutine unpack77_decode_type12(c77,context,decoded)
  character*77 c77
  type(unpack77_context), intent(in) :: context
  type(unpack77_core_result), intent(inout) :: decoded
  character*13 call_1,call_2
  character*3 crpt
  character*4 grid4
  logical unpk28_success,unpkg4_success,ok
  type(pack77_type12_fields) :: type12_fields
  integer :: n28a,n28b
  ! Type 1 (standard message) or Type 2 ("/P" form for EU VHF contest)
  if(decoded%i3.eq.1) then
     call decode_pack77_type1(c77,type12_fields,ok,.true.)
  else
     call decode_pack77_type2(c77,type12_fields,ok,.true.)
  endif
  if(.not.ok) then
     decoded%success=.false.
     return
  endif
  n28a=type12_fields%n28a
  n28b=type12_fields%n28b
  ipa=type12_fields%ipa
  ipb=type12_fields%ipb
  ir=type12_fields%ir
  igrid4=type12_fields%igrid4
  call record_configured_n28_pair(context,n28a,n28b)
  call unpack28_for_context(context,n28a,call_1,unpk28_success)
  if(context%nrx.eq.1 .and. context%mycall_set .and. &
       context%hashmy22.eq.(n28a-PACK77_NTOKENS)) then
     call_1='<'//trim(context%mycall)//'>'
     unpk28_success=.true.
  endif
  if(.not.unpk28_success) decoded%success=.false.
  call unpack28_for_context(context,n28b,call_2,unpk28_success)
  if(.not.unpk28_success) decoded%success=.false.
  if(call_1(1:3).eq.'CQ_') call_1(3:3)=' '
  if(index(call_1,'<').le.0) then
     i=index(call_1,' ')
     if(i.ge.4 .and. ipa.eq.1 .and. decoded%i3.eq.1) call_1(i:i+1)='/R'
     if(i.ge.4 .and. ipa.eq.1 .and. decoded%i3.eq.2) call_1(i:i+1)='/P'
     if(i.ge.4) then
        call stage_unpack_recent_call(decoded%effects,call_1)
        if(context%configured) call stage_unpack_hash_call(decoded%effects,call_1)
     endif
  endif
  if(index(call_2,'<').le.0) then
     i=index(call_2,' ')
     if(i.ge.4 .and. ipb.eq.1 .and. decoded%i3.eq.1) call_2(i:i+1)='/R'
     if(i.ge.4 .and. ipb.eq.1 .and. decoded%i3.eq.2) call_2(i:i+1)='/P'
     if(i.ge.4) then
        call stage_unpack_recent_call(decoded%effects,call_2)
        call stage_unpack_hash_call(decoded%effects,call_2)
     endif
  endif
  if(igrid4.le.PACK77_MAXGRID4) then
     call to_grid4(igrid4,grid4,unpkg4_success)
     if(.not.unpkg4_success) decoded%success=.false.
     decoded%msg=pack77_format_exchange_message(call_1,call_2,ir,grid4,.true.)
     if(decoded%msg(1:3).eq.'CQ ' .and. ir.eq.1) decoded%success=.false.
     if(context%strict_var_guards .and. .not.unpack77_type12_var_guard_ok( &
          call_1,call_2,grid4,ir)) decoded%success=.false.
  else
     irpt=igrid4-PACK77_MAXGRID4
     if(irpt.ge.1 .and. irpt.le.4) &
          decoded%msg=pack77_format_qso_tail(call_1,call_2, &
               pack77_tail_from_type12_irpt(irpt))
     if(irpt.ge.5) then
        crpt=pack77_format_snr_report(pack77_snr_from_report_index(irpt))
        decoded%msg=pack77_format_exchange_message(call_1,call_2,ir,crpt,.false.)
     endif
     if(decoded%msg(1:3).eq.'CQ ' .and. irpt.ge.2) decoded%success=.false.
  endif

  return
end subroutine unpack77_decode_type12

subroutine unpack77_decode_type3(c77,context,decoded)
  character*77 c77
  type(unpack77_context), intent(in) :: context
  type(unpack77_core_result), intent(inout) :: decoded
  character*13 call_1,call_2
  character*4 exchange
  character crpt*3
  logical unpk28_success,ok
  type(pack77_type3_fields) :: type3_fields
  integer :: n28a,n28b
  ! Type 3: ARRL RTTY Contest
  call decode_pack77_type3(c77,type3_fields,ok,.true.)
  if(.not.ok) then
     decoded%success=.false.
     return
  endif
  itu=type3_fields%itu
  n28a=type3_fields%n28a
  n28b=type3_fields%n28b
  ir=type3_fields%ir
  irpt=type3_fields%irpt
  nexch=type3_fields%nexch
  call record_configured_n28_pair(context,n28a,n28b)
  crpt=pack77_format_rtty_report(irpt)
  call unpack28_for_context(context,n28a,call_1,unpk28_success)
  if(.not.unpk28_success) decoded%success=.false.
  call unpack28_for_context(context,n28b,call_2,unpk28_success)
  if(.not.unpk28_success) decoded%success=.false.
  if(nexch.gt.8000 .and. nexch-8000.le.PACK77_NUSCAN) then
     exchange=pack77_rtty_multiplier_name(nexch-8000)
     if(.not.pack77_type3_render_fits(itu,ir,call_1,call_2,exchange)) then
        decoded%success=.false.
        return
     endif
     decoded%msg=pack77_format_type3_message(itu,ir,call_1,call_2,crpt, &
          exchange)
  else if(nexch.ge.1 .and. nexch.le.7999) then
     write(exchange,'(i4.4)') nexch
     if(.not.pack77_type3_render_fits(itu,ir,call_1,call_2,exchange)) then
        decoded%success=.false.
        return
     endif
     decoded%msg=pack77_format_type3_message(itu,ir,call_1,call_2,crpt,exchange)
  else
     decoded%success=.false.
  endif

  return
end subroutine unpack77_decode_type3

subroutine unpack77_decode_type4(c77,context,decoded)
  character*77 c77
  type(unpack77_context), intent(in) :: context
  type(unpack77_core_result), intent(inout) :: decoded
  character*13 call_1,call_2,call_3
  character*11 c11
  logical ok
  type(pack77_type4_fields) :: type4_fields
  ! Type 4
  call decode_pack77_type4(c77,type4_fields,ok,.true.)
  if(.not.ok) then
     decoded%success=.false.
     return
  endif
  n12=type4_fields%n12
  c11=pack77_type4_call_text_from_n58(type4_fields%n58)
  iflip=type4_fields%iflip
  nrpt=type4_fields%nrpt
  icq=type4_fields%icq
  call hash12(n12,call_3)
  if(iflip.eq.0) then       ! 12 bit hash for TO call
     call_1=call_3
     call_2=adjustl(c11)//'  '
     call stage_unpack_recent_call(decoded%effects,call_2)
     call stage_unpack_hash_call(decoded%effects,call_2)
     if(context%nrx.eq.1 .and. context%dxcall_set .and. &
          context%mycall_set .and. call_2.eq.context%dxcall .and. &
          n12.eq.context%hashmy12) call_1='<'//trim(context%mycall)//'>'
     if(context%nrx.eq.1 .and. context%mycall_set .and. &
          index(call_1,'<...>').gt.0 .and. &
          n12.eq.context%hashmy12) call_1='<'//trim(context%mycall)//'>'
  else                      ! 12 bit hash for DE call
     call_1=adjustl(c11)
     call_2=call_3
     call stage_unpack_recent_call(decoded%effects,call_1)
     if(context%configured) call stage_unpack_hash_call(decoded%effects,call_1)
     if(context%nrx.eq.0 .and. context%mycall_set .and. &
          n12.eq.context%hashmy12) call_2='<'//trim(context%mycall)//'>'
  endif
  if(icq.eq.0) then
     decoded%msg=pack77_format_qso_tail(call_1,call_2, &
          pack77_tail_from_type4_nrpt(nrpt))
  else
     decoded%msg='CQ '//trim(call_2)
  endif
  if(context%strict_var_guards .and. .not.unpack77_type4_var_guard_ok( &
       call_1,call_2,decoded%msg,iflip,icq,nrpt)) decoded%success=.false.

  return
end subroutine unpack77_decode_type4

subroutine unpack77_decode_type5(c77,context,decoded)
  character*77 c77
  type(unpack77_context), intent(in) :: context
  type(unpack77_core_result), intent(inout) :: decoded
  character*13 call_1,call_2
  character*6 cexch,grid6
  logical ok
  type(pack77_type5_fields) :: type5_fields
  ! Type 5  <PA3XYZ> <G4ABC/P> R 590003 IO91NP      h12 h22 r1 s3 S11 g25
  ! EU VHF contest
  call decode_pack77_type5(c77,type5_fields,ok,.true.)
  if(.not.ok) then
     decoded%success=.false.
     return
  endif
  n12=type5_fields%n12
  n22=type5_fields%n22
  ir=type5_fields%ir
  irpt=type5_fields%irpt
  iserial=type5_fields%iserial
  igrid6=type5_fields%igrid6
  call hash12(n12,call_1)
  if(n12.eq.context%hashmy12) call_1='<'//trim(context%mycall)//'>'
  call hash22(n22,call_2)
  cexch=pack77_format_vhf_exchange(irpt,iserial)
  call to_grid6(igrid6,grid6,decoded%success)
  decoded%msg=pack77_format_exchange_message(call_1,call_2,ir,cexch//' '//grid6,.true.)

  return
end subroutine unpack77_decode_type5

subroutine stage_unpack_hash_call(effects,c13)
  type(unpack77_effects), intent(inout) :: effects
  character(len=*), intent(in) :: c13

  call stage_unpack_call(effects%hash_calls,effects%nhash,c13)

  return
end subroutine stage_unpack_hash_call

subroutine stage_unpack_recent_call(effects,c13)
  type(unpack77_effects), intent(inout) :: effects
  character(len=*), intent(in) :: c13

  call stage_unpack_call(effects%recent_calls,effects%nrecent,c13)

  return
end subroutine stage_unpack_recent_call

subroutine stage_unpack_call(calls,ncalls,c13)
  character(len=13), intent(inout) :: calls(:)
  integer, intent(inout) :: ncalls
  character(len=*), intent(in) :: c13
  character(len=13) :: cw
  logical :: ok

! Keep unpack77_core side-effect free; callers decide whether staged calls are
! written immediately or queued for fillhashvar.
  call normalize_hash_call(c13,cw,ok)
  if(.not.ok) return
  if(ncalls.ge.size(calls)) return
  ncalls=ncalls+1
  calls(ncalls)=cw

  return
end subroutine stage_unpack_call

subroutine apply_unpack77_effects(effects,policy)
  type(unpack77_effects), intent(in) :: effects
  type(unpack77_effect_policy), intent(in) :: policy
  integer :: i,n10,n12,n22

! Non-configured decodes write directly to shared state. Configured/var decodes
! queue by RX thread and let fillhashvar merge the successful decode effects.
  if(policy%record_hashes) then
     do i=1,effects%nhash
        if(policy%queue_by_thread) then
           call queue_hash_call_for_thread(effects%hash_calls(i),policy%nthr_hash)
        else
           call save_hash_call(effects%hash_calls(i),n10,n12,n22)
        endif
     enddo
  endif
  if(policy%record_recent_calls) then
     do i=1,effects%nrecent
        if(policy%queue_by_thread) then
           call queue_recent_call_for_thread(effects%recent_calls(i), &
                policy%nthr_recent)
        else
           call add_call_to_recent_calls(effects%recent_calls(i))
        endif
     enddo
  endif

  return
end subroutine apply_unpack77_effects

subroutine unpack28_for_context(context,n28,c13,success)
  type(unpack77_context), intent(in) :: context
  integer, intent(in) :: n28
  character*13 c13
  logical success

  if(context%configured) then
     call unpack28_configured(n28,c13,success)
  else
     call unpack28(n28,c13,success)
  endif

  return
end subroutine unpack28_for_context

subroutine record_configured_n28_pair(context,n28a,n28b)
  type(unpack77_context), intent(in) :: context
  integer, intent(in) :: n28a,n28b

  if(.not.context%configured) return
  n28a_configured=n28a
  n28b_configured=n28b

  return
end subroutine record_configured_n28_pair

logical function unpack77_type12_var_guard_ok(call_1,call_2,grid4,ir) result(ok)
! Configured decode rejects a few hash-collision renders that parse as valid
! source messages but cannot round-trip through the configured var encoder.
  character(len=*), intent(in) :: call_1,call_2,grid4
  integer, intent(in) :: ir
  type(pack77_type12_call_source) :: source
  logical :: source_ok

  ok=.true.
  if(ir.eq.0 .and. call_1(1:1).eq.'<' .and. grid4.ne.'RR73') then
     call pack77_parse_type12_call(call_2,.false.,source,source_ok)
     if(source_ok .and. source%suffix.eq.PACK77_TYPE12_SUFFIX_R) ok=.false.
  endif

  return
end function unpack77_type12_var_guard_ok

logical function unpack77_type4_var_guard_ok(call_1,call_2,msg,iflip,icq,nrpt) result(ok)
! Type 4 can decode arbitrary 58-bit text as plausible calls; configured var
! mode keeps only renders that also satisfy the source-side Type 4 grammar.
  character(len=*), intent(in) :: call_1,call_2,msg
  integer, intent(in) :: iflip,icq,nrpt
  integer :: nmsglen,indxp,nlencall2,nindxspace
  type(pack77_type4_source) :: source
  logical :: source_ok

  ok=.true.
  call pack77_parse_type4_source(msg,source,source_ok)
  if(.not.source_ok) ok=.false.
  if(.not.unpack77_type4_call_guard_ok(call_2)) ok=.false.
  if(msg(1:3).ne.'CQ ' .and. .not.unpack77_type4_call_guard_ok(call_1)) &
       ok=.false.
  nmsglen=len_trim(msg)
  if(ok .and. nmsglen.gt.0) then
     indxp=index(msg,'/P ')
     if((icq.eq.0 .and. nrpt.eq.0 .and. (indxp.lt.1 .or. indxp.gt.7)) .or. &
          icq.eq.1) then
        if(msg(nmsglen:nmsglen).eq.'>') ok=.false.
     endif
  endif
  if(ok .and. iflip.eq.0 .and. icq.eq.0 .and. nrpt.eq.0) then
     nlencall2=len_trim(call_2)
     if(nlencall2.gt.9) then
        if(call_2(1:1).eq.'/' .or. call_2(nlencall2:nlencall2).eq.'/') &
             ok=.false.
        nindxspace=index(call_2,' ')
        if(nindxspace.gt.0 .and. nindxspace.lt.nlencall2) ok=.false.
     endif
  endif

  return
end function unpack77_type4_var_guard_ok

logical function unpack77_type4_call_guard_ok(call_text) result(ok)
  character(len=*), intent(in) :: call_text
  integer :: ispace,islash

  ok=pack77_valid_hash_call_token(call_text) .or. &
       pack77_valid_type4_c11(call_text)
  if(.not.ok) return
  if(pack77_valid_hash_call_token(call_text)) return
  if(pack77_is_digit_char(call_text(1:1)) .and. &
       pack77_is_digit_char(call_text(2:2))) ok=.false.
  if(len_trim(call_text).eq.11) then
     ispace=index(call_text,' ')
     if(ispace.gt.0 .and. ispace.lt.12) ok=.false.
     islash=index(call_text,'/')
     if(islash.eq.1 .or. islash.eq.2 .or. islash.eq.11 .or. &
          (islash.eq.10 .and. pack77_is_letter_char(call_text(11:11)) .and. &
          call_text(11:11).ne.'P')) ok=.false.
     if(islash.lt.6 .and. pack77_is_digit_char(call_text(11:11))) ok=.false.
  endif

  return
end function unpack77_type4_call_guard_ok

subroutine lower_c28_slot(token,allow_auto_hash,n28,hash_facts,ok)
  character(len=*), intent(in) :: token
  logical, intent(in) :: allow_auto_hash
  integer, intent(out) :: n28
  character*13 cw
  type(pack77_hash_facts), intent(inout) :: hash_facts
  type(pack77_hash_facts) :: trial_facts
  logical, intent(out) :: ok

! Some c28 fields may auto-hash nonstandard calls; stricter fields require an
! explicit <CALL> hash token so source text cannot choose a different family.
  ok=.true.
  n28=-1
  cw=token
  if(cw(1:3).eq.'DE ' .or. cw(1:4).eq.'QRZ ' .or. &
       cw(1:3).eq.'CQ ' .or. cw(1:3).eq.'CQ_') then
     trial_facts=hash_facts
     call pack28_core(cw,n28,trial_facts)
     if(n28.lt.PACK77_NTOKENS) return
  endif

  cw=token
  if(pack77_valid_hash_call_token(cw) .or. pack77_c28_standard_shape(cw)) then
     call pack28_core(cw,n28,hash_facts)
     return
  endif

  if(.not.allow_auto_hash) then
     ok=.false.
     return
  endif

  call pack28_core(cw,n28,hash_facts)

  return
end subroutine lower_c28_slot


integer function pack77_c28_render_len(token) result(n)
  character(len=*), intent(in) :: token
  integer :: m

  m=len_trim(token)
  n=m
  if(pack77_valid_hash_call_token(token)) return
  if(.not.pack77_c28_standard_shape(token)) then
     if(m.gt.11) then
        n=-1
     else
        n=m+2
     endif
  endif
end function pack77_c28_render_len


logical function pack77_dxpedition_render_fits(source) result(ok)
  type(pack77_dxpedition_source), intent(in) :: source
  integer :: n,ncall

  ok=.false.
  n=pack77_c28_render_len(source%call_1)
  if(n.lt.0) return
  ncall=pack77_c28_render_len(source%call_2)
  if(ncall.lt.0) return
  n=n+1+5+1+ncall+1+len_trim(source%hash_token)+1+3
  ok=n.le.37
end function pack77_dxpedition_render_fits


logical function pack77_field_day_render_fits(source) result(ok)
  type(pack77_field_day_source), intent(in) :: source
  character(len=3) :: section
  integer :: n,ncall,nexch

  ok=.false.
  section=pack77_arrl_section_name(source%isec)
  nexch=2
  if(source%ntx.ge.10) nexch=3
  n=pack77_c28_render_len(source%call_1)
  if(n.lt.0) return
  ncall=pack77_c28_render_len(source%call_2)
  if(ncall.lt.0) return
  n=n+1+ncall+1+nexch+1+len_trim(section)
  if(source%ir.eq.1) n=n+2
  ok=n.le.37
end function pack77_field_day_render_fits


subroutine unpack28_configured(n28_0,c13,success)
  integer, intent(in) :: n28_0
  character*13 c13
  logical success

  call unpack28_core(n28_0,c13,success,.true.)

  return
end subroutine unpack28_configured

type(pack77_candidate) function pack77_01_candidate(msg) result(candidate)

! Pack a Type 0.1 message: DXpedition mode
! Example message:  "K1ABC RR73; W9XYZ <KH1/KH7Z> -11"   28 28 10 5

  character(len=*), intent(in) :: msg
  character*77 c77
  type(pack77_dxpedition_source) :: source
  integer :: n28a,n28b
  logical ok,matched_shape

  candidate%result=pack77_no_match()
  call pack77_parse_dxpedition_source(msg,source,ok,matched_shape)
  if(.not.ok) return
  if(.not.pack77_dxpedition_render_fits(source)) return

! Type 0.1:  K1ABC RR73; W9XYZ <KH1/KH7Z> -11   28 28 10 5       71   DXpedition special msg
  call lower_c28_slot(source%call_1,.true.,n28a,candidate%hash_facts,ok)
  if(.not.ok) return
  call lower_c28_slot(source%call_2,.true.,n28b,candidate%hash_facts,ok)
  if(.not.ok) return
  call pack77_hash_index(candidate%hash_facts,source%hash_token,10,n10,ok)
  if(.not.ok) return
  call encode_pack77_dxpedition(n28a,n28b,n10,source%n5,c77,ok)
  call pack77_accept_candidate_encoded(candidate,0,1,c77,ok)
end function pack77_01_candidate

type(pack77_candidate) function pack77_03_candidate(msg) result(candidate)

! Check 0.3 and 0.4 (ARRL Field Day exchange)
! Example message:  WA9XYZ KA1ABC R 16A EMA       28 28 1 4 3 7    71

  character(len=*), intent(in) :: msg
  character*77 c77
  type(pack77_field_day_source) :: source
  integer :: n28a,n28b
  logical ok,matched_shape

  candidate%result=pack77_no_match()
  call pack77_parse_field_day_source(msg,source,ok,matched_shape)
  if(.not.ok) return
  if(.not.pack77_field_day_render_fits(source)) return

! 0.3   WA9XYZ KA1ABC R 16A EMA            28 28 1 4 3 7    71   ARRL Field Day
! 0.4   WA9XYZ KA1ABC R 32A EMA            28 28 1 4 3 7    71   ARRL Field Day

  n3=3                                 !Type 0.3 ARRL Field Day
  intx=source%ntx-1
  if(intx.ge.16) then
     n3=4                              !Type 0.4 ARRL Field Day
     intx=source%ntx-17
  endif
  call lower_c28_slot(source%call_1,.true.,n28a,candidate%hash_facts,ok)
  if(.not.ok) return
  call lower_c28_slot(source%call_2,.true.,n28b,candidate%hash_facts,ok)
  if(.not.ok) return
  if(n3.eq.3) then
     call encode_pack77_field_day_low(n28a,n28b,source%ir,intx, &
          source%nclass,source%isec,c77,ok)
  else
     call encode_pack77_field_day_high(n28a,n28b,source%ir,intx, &
          source%nclass,source%isec,c77,ok)
  endif
  call pack77_accept_candidate_encoded(candidate,0,n3,c77,ok)

  return
end function pack77_03_candidate

type(pack77_candidate) function pack77_06_candidate(msg,prefer_wspr_50bit) result(candidate)

  character(len=*), intent(in) :: msg
  character*77 c77
  type(pack77_wspr_source) :: source
  logical, intent(in) :: prefer_wspr_50bit
  logical ok,matched_shape

  candidate%result=pack77_no_match()
  call pack77_parse_wspr_source(msg,source,ok,matched_shape)
  if(.not.ok) return

  if(source%subtype.eq.1) then
! WSPR Type 1
     call lower_c28_slot(source%call_token,.false.,n28,candidate%hash_facts,ok)
     if(.not.ok) return
     igrid4=pack77_grid4_index(source%grid4)
     call encode_pack77_wspr_type1(n28,igrid4,source%idbm,c77,ok)
     call pack77_accept_candidate_encoded(candidate,0,6,c77,ok)
     return
  endif

  if(source%subtype.eq.2) then
! WSPR Type 2
     call lower_c28_slot(source%base_call//'       ',.false.,n28, &
          candidate%hash_facts,ok)
     if(.not.ok) return
     call encode_pack77_wspr_type2(n28,source%npfx,source%idbm,c77,ok)
     call pack77_accept_candidate_encoded(candidate,0,6,c77,ok)
     return
  endif

  if(source%subtype.eq.3 .and. prefer_wspr_50bit) then
! WSPR Type 3

     ! n3=6 and i3=0 are an advisory preference for the caller's
     ! 50-bit encoding over the possible alternative n3=4 77-bit
     ! encoding.
     call pack77_hash_index(candidate%hash_facts,source%hash_token,22,n22,ok)
     if(.not.ok) return
     igrid6=pack77_grid6_wspr_index(source%grid6)
     call encode_pack77_wspr_type3(n22,igrid6,c77,ok)
     call pack77_accept_candidate_encoded(candidate,0,6,c77,ok)
  endif
end function pack77_06_candidate

type(pack77_candidate) function pack77_1_candidate(msg) result(candidate)

! Type 1/2 source grammar lowers only exact c28 call-slot forms.

  character(len=*), intent(in) :: msg
  character*77 c77
  type(pack77_type12_source) :: source
  integer :: n28a,n28b
  logical ok

  candidate%result=pack77_no_match()
  call pack77_parse_type12_source(msg,source,ok)
  if(.not.ok) return

  call lower_c28_slot(source%call_1%c28_token,.false.,n28a, &
       candidate%hash_facts,ok)
  if(.not.ok) return
  call lower_c28_slot(source%call_2%c28_token,.false.,n28b, &
       candidate%hash_facts,ok)
  if(.not.ok) return

  ipa=0
  ipb=0
  if(pack77_type12_call_has_suffix(source%call_1)) ipa=1
  if(pack77_type12_call_has_suffix(source%call_2)) ipb=1

  if(source%i3.eq.1) then
     call encode_pack77_type1(n28a,ipa,n28b,ipb, &
          source%ir,source%igrid4,c77,ok)
  else
     call encode_pack77_type2(n28a,ipa,n28b,ipb, &
          source%ir,source%igrid4,c77,ok)
  endif
  call pack77_accept_candidate_encoded(candidate,source%i3,0,c77,ok)
end function pack77_1_candidate


type(pack77_candidate) function pack77_3_candidate(msg) result(candidate)
! Check Type 3 (ARRL RTTY contest exchange)
!ARRL RTTY   - US/Can: rpt state/prov      R 579 MA
!     	     - DX:     rpt serial          R 559 0013
! Example message:  TU; W9XYZ K1ABC R 579 MA           1 28 28 1 3 13   74

  character(len=*), intent(in) :: msg
  character*77 c77
  character*4 exchange
  type(pack77_type3_source) :: source
  integer :: n28a,n28b
  logical ok,matched_shape

  candidate%result=pack77_no_match()
  call pack77_parse_type3_source(msg,source,ok,matched_shape)
  if(.not.ok) return

  call lower_c28_slot(source%call_1,.true.,n28a,candidate%hash_facts,ok)
  if(.not.ok) return
  call lower_c28_slot(source%call_2,.true.,n28b,candidate%hash_facts,ok)
  if(.not.ok) return
  exchange=' '
  if(source%nexch.gt.0 .and. source%nexch.lt.8000) &
       write(exchange,'(i4.4)') source%nexch
  if(source%nexch.gt.8000) &
       exchange=pack77_rtty_multiplier_name(source%nexch-8000)
  if(.not.pack77_type3_display_fits(source%itu,source%ir,n28a, &
       n28b,source%call_1,source%call_2,exchange)) return
! 3     TU; W9XYZ K1ABC R 579 MA             1 28 28 1 3 13       74   ARRL RTTY contest
! 3     TU; W9XYZ G8ABC R 559 0013           1 28 28 1 3 13       74   ARRL RTTY (DX)
  call encode_pack77_type3(source%itu,n28a,n28b,source%ir,source%irpt, &
       source%nexch,c77,ok)
  call pack77_accept_candidate_encoded(candidate,3,0,c77,ok)
end function pack77_3_candidate

type(pack77_candidate) function pack77_4_candidate(msg) result(candidate)

! Check Type 4 (One nonstandard call and one hashed call)
! Example message: <WA9XYZ> PJ4/KA1ABC RR73           12 58 1 2 1      74

  character(len=*), intent(in) :: msg
  integer*8 n58
  character*77 c77
  character*11 c11
  character*13 hash_token
  type(pack77_type4_source) :: source
  logical ok

  candidate%result=pack77_no_match()
  call pack77_parse_type4_source(msg,source,ok)
  if(.not.ok) return

  hash_token=source%hash_token
  if(source%icq.eq.1 .and. len_trim(hash_token).eq.0) hash_token=source%c11
  call pack77_hash_index(candidate%hash_facts,hash_token,12,n12,ok)
  if(.not.ok) return
  c11=adjustr(source%c11)
  n58=pack77_type4_n58_from_call_text(c11)
  nrpt=pack77_type4_nrpt_from_tail(source%itail)
  if(source%icq.eq.1) nrpt=0
  call encode_pack77_type4(n12,n58,source%iflip,nrpt,source%icq,c77,ok)
  call pack77_accept_candidate_encoded(candidate,4,0,c77,ok)
end function pack77_4_candidate

type(pack77_candidate) function pack77_5_candidate(msg) result(candidate)

! Pack a Type 0.2 message: EU VHF Contest mode
! Example message:  PA3XYZ/P R 590003 IO91NP           28 1 1 3 12 25
!                 <PA3XYZ> <G4ABC/P> R 590003 IO91NP   h10 h20 r1 s3 s12 g25

  character(len=*), intent(in) :: msg
  character*77 c77
  type(pack77_type5_source) :: source
  logical ok,matched_shape

  candidate%result=pack77_no_match()
  call pack77_parse_type5_source(msg,source,ok,matched_shape)
  if(.not.ok) return

! Type 0.2: <PA3XYZ> <G4ABC/P> R 590003 IO91NP     h10 h20 r1 s3 s12 g25

  call pack77_hash_index(candidate%hash_facts,source%hash_token_1,12,n12,ok)
  if(.not.ok) return
  call pack77_hash_index(candidate%hash_facts,source%hash_token_2,22,n22,ok)
  if(.not.ok) return

  igrid6=pack77_grid6_index(source%grid6)
  call encode_pack77_type5(n12,n22,source%ir,source%irpt,source%iserial, &
       igrid6,c77,ok)
  call pack77_accept_candidate_encoded(candidate,5,0,c77,ok)

  return
end function pack77_5_candidate

end module packjt77

logical function callok(w)

  character*(*) w
  character*1 c1
  character*2 pfx
  logical isdig,islet

  islet(c1)=(ichar(c1).ge.65 .and. ichar(c1).le.90) .or. &
            (ichar(c1).ge.97 .and. ichar(c1).le.122)
  isdig(c1)=(ichar(c1).ge.48 .and. ichar(c1).le.57)

  callok=.false.
  n=len(trim(w))
  if(n.lt.3) return                 !Must be at lkeast three characters
  if(w(1:1).eq.'Q') return          !Callsigns can't start with Q

  i0=0
  do i=n,1,-1
     if(isdig(w(i:i))) exit
  enddo
  i0=i                              !Call area position in word

  if(i0.ne.2 .and. i0.ne.3) return

  pfx=w(1:i0-1)                     !Prefix, without call area

  nlp=0
  ndp=0
  np=len(trim(pfx))
  do i=1,np
     if(isdig(pfx(i:i))) ndp=ndp+1
     if(islet(pfx(i:i))) nlp=nlp+1
  enddo
  if(nlp+ndp.ne.np) return
  if(nlp.eq.0) return              !Prefix must have at least one letter

  nls=0
  ns=n-i0
  if(ns.lt.1 .or. ns.gt.3) return
  do i=i0+1,n
     if(islet(w(i:i))) nls=nls+1
  enddo
  if(nls.lt.ns) return             !Suffix must be all letters

  callok=.true.

  return
end function callok
