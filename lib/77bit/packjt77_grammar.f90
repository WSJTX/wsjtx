module packjt77_grammar

  implicit none

  private :: pack77_gate_staged_call_matches

  integer, parameter :: PACK77_NSEC=86
  integer, parameter :: PACK77_NUSCAN=171
  integer, parameter :: PACK77_WSPR_NZZZ=46656
! c28 number-space partition: special tokens, then 22-bit hashes, then
! standard callsigns; grid4 indexes above PACK77_MAXGRID4 carry reports.
  integer, parameter :: PACK77_NTOKENS=2063592
  integer, parameter :: PACK77_MAX22=4194304
  integer, parameter :: PACK77_MAXGRID4=32400
  character(len=36), parameter :: PACK77_BASE36='0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'
  character(len=38), parameter :: PACK77_BASE38=' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/'
  character(len=3), parameter :: PACK77_ARRL_SECTIONS(PACK77_NSEC)=(/ &
       "AB ","AK ","AL ","AR ","AZ ","BC ","CO ","CT ","DE ","EB ",  &
       "EMA","ENY","EPA","EWA","GA ","GH ","IA ","ID ","IL ","IN ",  &
       "KS ","KY ","LA ","LAX","NS ","MB ","MDC","ME ","MI ","MN ",  &
       "MO ","MS ","MT ","NC ","ND ","NE ","NFL","NH ","NL ","NLI",  &
       "NM ","NNJ","NNY","TER","NTX","NV ","OH ","OK ","ONE","ONN",  &
       "ONS","OR ","ORG","PAC","PR ","QC ","RI ","SB ","SC ","SCV",  &
       "SD ","SDG","SF ","SFL","SJV","SK ","SNJ","STX","SV ","TN ",  &
       "UT ","VA ","VI ","VT ","WCF","WI ","WMA","WNY","WPA","WTX",  &
       "WV ","WWA","WY ","DX ","PE ","NB " /)
  character(len=3), parameter :: PACK77_RTTY_MULTIPLIERS(PACK77_NUSCAN)=(/ &
       "AL ","AK ","AZ ","AR ","CA ","CO ","CT ","DE ","FL ","GA ",  &
       "HI ","ID ","IL ","IN ","IA ","KS ","KY ","LA ","ME ","MD ",  &
       "MA ","MI ","MN ","MS ","MO ","MT ","NE ","NV ","NH ","NJ ",  &
       "NM ","NY ","NC ","ND ","OH ","OK ","OR ","PA ","RI ","SC ",  &
       "SD ","TN ","TX ","UT ","VT ","VA ","WA ","WV ","WI ","WY ",  &
       "NB ","NS ","QC ","ON ","MB ","SK ","AB ","BC ","NWT","NF ",  &
       "LB ","NU ","YT ","PEI","DC ","DR ","FR ","GD ","GR ","OV ",  &
       "ZH ","ZL ","X01","X02","X03","X04","X05","X06","X07","X08",  &
       "X09","X10","X11","X12","X13","X14","X15","X16","X17","X18",  &
       "X19","X20","X21","X22","X23","X24","X25","X26","X27","X28",  &
       "X29","X30","X31","X32","X33","X34","X35","X36","X37","X38",  &
       "X39","X40","X41","X42","X43","X44","X45","X46","X47","X48",  &
       "X49","X50","X51","X52","X53","X54","X55","X56","X57","X58",  &
       "X59","X60","X61","X62","X63","X64","X65","X66","X67","X68",  &
       "X69","X70","X71","X72","X73","X74","X75","X76","X77","X78",  &
       "X79","X80","X81","X82","X83","X84","X85","X86","X87","X88",  &
       "X89","X90","X91","X92","X93","X94","X95","X96","X97","X98",  &
       "X99" /)
  integer, parameter :: PACK77_TYPE12_SUFFIX_NONE=0
  integer, parameter :: PACK77_TYPE12_SUFFIX_R=1
  integer, parameter :: PACK77_TYPE12_SUFFIX_P=2


  type pack77_type12_call_source
     character(len=13) :: c28_token=''
     integer :: suffix=PACK77_TYPE12_SUFFIX_NONE
     logical :: is_hash=.false.
     logical :: is_cq=.false.
  end type pack77_type12_call_source

  type pack77_type12_source
     type(pack77_type12_call_source) :: call_1
     type(pack77_type12_call_source) :: call_2
     integer :: ir=0
     integer :: igrid4=0
     integer :: i3=1
  end type pack77_type12_source

  type pack77_dxpedition_source
     character(len=13) :: call_1='',call_2='',hash_token=''
     integer :: n5=0
  end type pack77_dxpedition_source

  type pack77_field_day_source
     character(len=13) :: call_1='',call_2=''
     integer :: ir=0,ntx=0,nclass=0,isec=-1
  end type pack77_field_day_source

  type pack77_wspr_source
     character(len=13) :: call_token='',hash_token=''
     character(len=6) :: base_call='',grid6=''
     character(len=4) :: grid4=''
     integer :: subtype=0,idbm=0,npfx=0
  end type pack77_wspr_source

  type pack77_type3_source
     character(len=13) :: call_1='',call_2=''
     integer :: itu=0,ir=0,irpt=-1,nexch=0
  end type pack77_type3_source

  type pack77_type4_source
     character(len=13) :: hash_token=''
     character(len=11) :: c11=''
     integer :: icq=0,iflip=0,itail=0
  end type pack77_type4_source

  type pack77_type5_source
     character(len=13) :: hash_token_1='',hash_token_2=''
     character(len=6) :: grid6=''
     integer :: ir=0,irpt=0,iserial=0
  end type pack77_type5_source
  contains

integer function pack77_arrl_section_index(section) result(isec)
  character(len=*), intent(in) :: section
  integer :: i

  isec=-1
  if(len(section).lt.3) return
  do i=1,PACK77_NSEC
     if(PACK77_ARRL_SECTIONS(i).eq.section(1:3)) then
        isec=i
        return
     endif
  enddo

  return
end function pack77_arrl_section_index

character(len=3) function pack77_arrl_section_name(isec) result(section)
  integer, intent(in) :: isec

  section='   '
  if(isec.ge.1 .and. isec.le.PACK77_NSEC) section=PACK77_ARRL_SECTIONS(isec)

  return
end function pack77_arrl_section_name

integer function pack77_rtty_multiplier_index(multiplier) result(imult)
  character(len=*), intent(in) :: multiplier
  integer :: i

  imult=-1
  if(len(multiplier).lt.3) return
  do i=1,PACK77_NUSCAN
     if(PACK77_RTTY_MULTIPLIERS(i).eq.multiplier(1:3)) then
        imult=i
        return
     endif
  enddo

  return
end function pack77_rtty_multiplier_index

character(len=3) function pack77_rtty_multiplier_name(imult) result(multiplier)
  integer, intent(in) :: imult

  multiplier='   '
  if(imult.ge.1 .and. imult.le.PACK77_NUSCAN) &
       multiplier=PACK77_RTTY_MULTIPLIERS(imult)

  return
end function pack77_rtty_multiplier_name

integer function pack77_report_index_from_snr(isnr) result(irpt)
  integer, intent(in) :: isnr

  if(isnr.lt.-50 .or. isnr.gt.50) then
     irpt=-1
     return
  endif
! irpt 1..85 covers the legacy -30..+50 dB range; -50..-31 dB fold to
! irpt 86..105 above the old top so pre-extension decoders keep their mapping.
  irpt=isnr
  if(irpt.ge.-50 .and. irpt.le.-31) irpt=irpt+101
  irpt=irpt+35

  return
end function pack77_report_index_from_snr

integer function pack77_snr_from_report_index(irpt) result(isnr)
  integer, intent(in) :: irpt

  isnr=irpt-35
  if(isnr.gt.50) isnr=isnr-101

  return
end function pack77_snr_from_report_index

character(len=3) function pack77_format_snr_report(isnr) result(crpt)
  integer, intent(in) :: isnr

  write(crpt,'(i3.2)') isnr
  if(crpt(1:1).eq.' ') crpt(1:1)='+'

  return
end function pack77_format_snr_report

integer function pack77_rtty_report_index(report) result(irpt)
  character(len=*), intent(in) :: report

  irpt=-1
  if(len_trim(report).ne.3) return
  if(report(1:1).ne.'5') return
  if(report(2:2).lt.'2' .or. report(2:2).gt.'9') return
  if(report(3:3).ne.'9') return
  irpt=iachar(report(2:2))-iachar('2')
end function pack77_rtty_report_index

character(len=3) function pack77_format_rtty_report(irpt) result(crpt)
  integer, intent(in) :: irpt

  write(crpt,1001) irpt+2
1001 format('5',i1,'9')

  return
end function pack77_format_rtty_report

character(len=6) function pack77_format_vhf_exchange(irpt,iserial) result(cexch)
  integer, intent(in) :: irpt,iserial

  write(cexch,1002) 52+irpt,iserial
1002 format(i2,i4.4)

  return
end function pack77_format_vhf_exchange

character(len=37) function pack77_format_type3_message(itu,ir,call_1,call_2,crpt,exchange) result(msg)
  integer, intent(in) :: itu,ir
  character(len=*), intent(in) :: call_1,call_2,crpt,exchange
  character(len=37) :: body

  body=pack77_format_exchange_message(call_1,call_2,ir,crpt//' '//exchange,.true.)
  msg=body
  if(itu.eq.1) msg='TU; '//trim(body)

  return
end function pack77_format_type3_message

integer function pack77_type3_message_len(itu,ir,call_1_len,call_2_len,exchange_len) result(nmsg)
  integer, intent(in) :: itu,ir,call_1_len,call_2_len,exchange_len

  nmsg=call_1_len + 1 + call_2_len + 1 + 3 + 1 + exchange_len
  if(ir.eq.1) nmsg=call_1_len + 1 + call_2_len + 3 + 3 + 1 + exchange_len
  if(itu.eq.1) nmsg=nmsg+4
end function pack77_type3_message_len

integer function pack77_type3_display_call_len(n28,call_text) result(ncall)
  integer, intent(in) :: n28
  character(len=*), intent(in) :: call_text
  ncall=len_trim(call_text)
  if(n28.ge.PACK77_NTOKENS .and. n28.lt.PACK77_NTOKENS+PACK77_MAX22) then
     if(ncall.gt.11) then
        ncall=-1
     else
        ncall=ncall+2
     endif
  endif
end function pack77_type3_display_call_len

logical function pack77_type3_display_fits(itu,ir,n28a,n28b,call_1,call_2,exchange) result(ok)
  integer, intent(in) :: itu,ir,n28a,n28b
  character(len=*), intent(in) :: call_1,call_2,exchange
  integer :: ncall_1,ncall_2,nmsg

  ok=.false.
  ncall_1=pack77_type3_display_call_len(n28a,call_1)
  if(ncall_1.lt.0) return
  ncall_2=pack77_type3_display_call_len(n28b,call_2)
  if(ncall_2.lt.0) return
  nmsg=pack77_type3_message_len(itu,ir,ncall_1,ncall_2,len_trim(exchange))
  ok=nmsg.le.37
end function pack77_type3_display_fits

logical function pack77_type3_render_fits(itu,ir,call_1,call_2,exchange) result(ok)
  integer, intent(in) :: itu,ir
  character(len=*), intent(in) :: call_1,call_2,exchange
  integer :: ncall_1,ncall_2,nmsg

  ok=.false.
  ncall_1=len_trim(call_1)
  ncall_2=len_trim(call_2)
  if(call_1(1:1).eq.'<' .and. index(call_1,'>').lt.1) return
  if(call_2(1:1).eq.'<' .and. index(call_2,'>').lt.1) return
  nmsg=pack77_type3_message_len(itu,ir,ncall_1,ncall_2,len_trim(exchange))
  ok=nmsg.le.37
end function pack77_type3_render_fits

character(len=37) function pack77_format_qso_tail(call_1,call_2,itail) result(msg)
  character(len=*), intent(in) :: call_1,call_2
  integer, intent(in) :: itail

  msg=trim(call_1)//' '//trim(call_2)
  if(itail.eq.1) msg=trim(call_1)//' '//trim(call_2)//' RRR'
  if(itail.eq.2) msg=trim(call_1)//' '//trim(call_2)//' RR73'
  if(itail.eq.3) msg=trim(call_1)//' '//trim(call_2)//' 73'

  return
end function pack77_format_qso_tail

integer function pack77_qso_tail_index(token) result(itail)
  character(len=*), intent(in) :: token

  itail=0
  if(trim(token).eq.'RRR') itail=1
  if(trim(token).eq.'RR73') itail=2
  if(trim(token).eq.'73') itail=3
end function pack77_qso_tail_index

integer function pack77_type12_irpt_from_tail(itail) result(irpt)
  integer, intent(in) :: itail

  irpt=0
  if(itail.ge.1 .and. itail.le.3) irpt=itail+1
end function pack77_type12_irpt_from_tail

integer function pack77_tail_from_type12_irpt(irpt) result(itail)
  integer, intent(in) :: irpt

  itail=0
  if(irpt.ge.2 .and. irpt.le.4) itail=irpt-1
end function pack77_tail_from_type12_irpt

integer function pack77_type4_nrpt_from_tail(itail) result(nrpt)
  integer, intent(in) :: itail

  nrpt=0
  if(itail.ge.1 .and. itail.le.3) nrpt=itail
end function pack77_type4_nrpt_from_tail

integer function pack77_tail_from_type4_nrpt(nrpt) result(itail)
  integer, intent(in) :: nrpt

  itail=0
  if(nrpt.ge.1 .and. nrpt.le.3) itail=nrpt
end function pack77_tail_from_type4_nrpt

character(len=37) function pack77_format_exchange_message(call_1,call_2,ir,exchange,reply_has_separate_token) result(msg)
  character(len=*), intent(in) :: call_1,call_2,exchange
  integer, intent(in) :: ir
  logical, intent(in) :: reply_has_separate_token

  msg=trim(call_1)//' '//trim(call_2)//' '//exchange
  if(ir.eq.1) then
     if(reply_has_separate_token) then
        msg=trim(call_1)//' '//trim(call_2)//' R '//exchange
     else
        msg=trim(call_1)//' '//trim(call_2)//' R'//exchange
     endif
  endif

  return
end function pack77_format_exchange_message

logical function pack77_is_digit_char(c) result(is_digit)
  character(len=1), intent(in) :: c

  is_digit=c.ge.'0' .and. c.le.'9'

  return
end function pack77_is_digit_char

logical function pack77_is_letter_char(c) result(is_letter)
  character(len=1), intent(in) :: c

  is_letter=c.ge.'A' .and. c.le.'Z'

  return
end function pack77_is_letter_char

logical function pack77_all_digits(token) result(ok)
  character(len=*), intent(in) :: token
  integer :: i,n

  ok=.false.
  n=len_trim(token)
  if(n.lt.1) return
  do i=1,n
     if(.not.pack77_is_digit_char(token(i:i))) return
  enddo
  ok=.true.
end function pack77_all_digits

subroutine pack77_parse_uint(token,value,ok)
  character(len=*), intent(in) :: token
  integer, intent(out) :: value
  logical, intent(out) :: ok

  value=0
  ok=pack77_all_digits(token)
  if(.not.ok) return
  read(token,*,err=10) value
  return
10 ok=.false.
end subroutine pack77_parse_uint

subroutine pack77_parse_signed_report(token,value,ok)
  character(len=*), intent(in) :: token
  integer, intent(out) :: value
  logical, intent(out) :: ok

  value=0
  ok=.false.
  if(len_trim(token).ne.3) return
  if(token(1:1).ne.'+' .and. token(1:1).ne.'-') return
  if(.not.pack77_is_digit_char(token(2:2))) return
  if(.not.pack77_is_digit_char(token(3:3))) return
  read(token,*,err=10) value
  ok=.true.
  return
10 ok=.false.
end subroutine pack77_parse_signed_report

subroutine pack77_parse_dxpedition_report(token,n5,ok)
  character(len=*), intent(in) :: token
  integer, intent(out) :: n5
  integer :: isnr
  logical, intent(out) :: ok

  n5=0
  call pack77_parse_signed_report(token,isnr,ok)
  if(.not.ok) return
  if(isnr.lt.-30 .or. isnr.gt.32) then
     ok=.false.
     return
  endif
  n5=(isnr+30)/2
  ok=(2*n5-30).eq.isnr
end subroutine pack77_parse_dxpedition_report

subroutine pack77_parse_type3_serial(token,nserial,ok)
  character(len=*), intent(in) :: token
  integer, intent(out) :: nserial
  logical, intent(out) :: ok

  nserial=0
  ok=.false.
  if(len_trim(token).ne.4) return
  call pack77_parse_uint(token,nserial,ok)
  if(.not.ok) return
  ok=nserial.ge.1 .and. nserial.le.7999
end subroutine pack77_parse_type3_serial

subroutine pack77_parse_wspr_dbm(token,idbm,ok)
  character(len=*), intent(in) :: token
  integer, intent(out) :: idbm
  integer :: dbm,decoded_dbm
  logical, intent(out) :: ok

  idbm=0
  call pack77_parse_uint(token,dbm,ok)
  if(.not.ok) return
  if(len_trim(token).gt.1 .and. token(1:1).eq.'0') then
     ok=.false.
     return
  endif
  if(dbm.lt.0 .or. dbm.gt.60) then
     ok=.false.
     return
  endif
  idbm=nint(0.3*dbm)
  decoded_dbm=nint(idbm*10.0/3.0)
  ok=decoded_dbm.eq.dbm
end subroutine pack77_parse_wspr_dbm

subroutine pack77_parse_field_day_exchange(token,ntx,nclass,ok)
  character(len=*), intent(in) :: token
  integer, intent(out) :: ntx,nclass
  integer :: n
  logical, intent(out) :: ok

  ntx=-1
  nclass=-1
  ok=.false.
  n=len_trim(token)
  if(n.lt.2) return
  if(n.gt.2 .and. token(1:1).eq.'0') return
  call pack77_parse_uint(token(1:n-1),ntx,ok)
  if(.not.ok) return
  if(ntx.lt.1 .or. ntx.gt.32) then
     ok=.false.
     return
  endif
  nclass=ichar(token(n:n))-ichar('A')
  ok=nclass.ge.0 .and. nclass.le.5
end subroutine pack77_parse_field_day_exchange

logical function pack77_valid_hash_call_token(token) result(ok)
  character(len=*), intent(in) :: token
  integer :: n

  ok=.false.
  n=len_trim(token)
  if(n.lt.5 .or. n.gt.13) return
  if(token(1:1).ne.'<' .or. token(n:n).ne.'>') return
  if(index(token(2:n-1),'<').gt.0) return
  if(index(token(2:n-1),'>').gt.0) return
  ok=.true.
end function pack77_valid_hash_call_token

logical function pack77_valid_cq_modifier(token) result(ok)
  character(len=*), intent(in) :: token
  integer :: i,n,nletters,ndigits

  ok=.false.
  n=len_trim(token)
  if(n.lt.1 .or. n.gt.4) return
  nletters=0
  ndigits=0
  do i=1,n
     if(token(i:i).ge.'A' .and. token(i:i).le.'Z') then
        nletters=nletters+1
     else if(pack77_is_digit_char(token(i:i))) then
        ndigits=ndigits+1
     else
        return
     endif
  enddo
  ok=(ndigits.eq.3 .and. nletters.eq.0) .or. &
       (nletters.ge.1 .and. nletters.le.4 .and. ndigits.eq.0)
end function pack77_valid_cq_modifier

pure subroutine pack77_split_source_tokens(msg,max_words,nwords,w,ok)
  character(len=*), intent(in) :: msg
  integer, intent(in) :: max_words
  integer, intent(out) :: nwords
  character(len=*), intent(out) :: w(:)
  logical, intent(out) :: ok
  character(len=1) :: c,c0
  integer :: i,n,iz

  nwords=0
  n=0
  c0=' '
  w='                                     '
  ok=.true.
  iz=len_trim(msg)
  do i=1,iz
     c=msg(i:i)
     if(ichar(c).eq.0) c=' '
     if(c.ge.'a' .and. c.le.'z') c=char(ichar(c)-32)
     if(c.eq.' ' .and. c0.eq.' ') cycle
     if(c.ne.' ' .and. c0.eq.' ') then
        nwords=nwords+1
        n=0
        if(nwords.gt.max_words .or. nwords.gt.size(w)) then
           ok=.false.
           return
        endif
     endif
     if(c.ne.' ') then
        n=n+1
        if(n.gt.len(w(1))) then
           ok=.false.
           return
        endif
        w(nwords)(n:n)=c
     endif
     c0=c
  enddo
end subroutine pack77_split_source_tokens

pure subroutine pack77_gate_message_checks(input_msg,decoded_msg,staged_calls, &
     nstaged,messages_match,resolved_render_fits)
  character(len=*), intent(in) :: input_msg,decoded_msg
  character(len=13), intent(in) :: staged_calls(:)
  integer, intent(in) :: nstaged
  logical, intent(out) :: messages_match,resolved_render_fits
  character(len=37) :: input_tokens(19),decoded_tokens(19),input_body
  integer :: input_nwords,decoded_nwords,i,nrender,nbody
  logical :: input_ok,decoded_ok,token_match

  messages_match=.false.
  resolved_render_fits=.false.
  call pack77_split_source_tokens(input_msg,19,input_nwords,input_tokens,input_ok)
  call pack77_split_source_tokens(decoded_msg,19,decoded_nwords,decoded_tokens, &
       decoded_ok)
  if(.not.input_ok .or. .not.decoded_ok) return
  if(input_nwords.ne.decoded_nwords) return

  messages_match=.true.
  resolved_render_fits=.true.
! nrender is the display length after hash resolution: a decoded <...> token
! renders as the staged call body plus its two brackets, and separators are
! counted only between tokens.
  nrender=0
  do i=1,input_nwords
     if(i.gt.1) nrender=nrender+1
     token_match=.false.
     if(trim(decoded_tokens(i)).eq.trim(input_tokens(i))) then
        token_match=.true.
        nrender=nrender+len_trim(decoded_tokens(i))
     else
        input_body='                                     '
        input_body='<'//trim(input_tokens(i))//'>'
        if(trim(decoded_tokens(i)).eq.trim(input_body)) then
           token_match=.true.
           nrender=nrender+len_trim(decoded_tokens(i))
        else if(trim(decoded_tokens(i)).eq.'<...>') then
           input_body=input_tokens(i)
           nbody=len_trim(input_body)
           if(nbody.ge.2 .and. input_body(1:1).eq.'<' .and. &
                input_body(nbody:nbody).eq.'>') then
              input_body='                                     '
              if(nbody.gt.2) input_body=input_tokens(i)(2:nbody-1)
           endif
           token_match=pack77_gate_staged_call_matches(input_body,staged_calls, &
                nstaged)
           if(len_trim(input_body).gt.11) resolved_render_fits=.false.
           nrender=nrender+len_trim(input_body)+2
        else
           nrender=nrender+len_trim(decoded_tokens(i))
        endif
     endif
     if(.not.token_match) messages_match=.false.
     if(trim(decoded_tokens(i)).eq.'<...>' .and. .not.token_match) &
          resolved_render_fits=.false.
     if(nrender.gt.37) resolved_render_fits=.false.
  enddo
end subroutine pack77_gate_message_checks

! Canonicalization accepted by the encode/decode gate is exhaustive:
! 1. The tokenizer folds case and collapses blanks before parsing.
! 2. A compound/nonstandard call in a c28 slot transmits as a hash and decodes bracketed.
! 3. A bracketed hash decodes as <...> when no hash-table call is available and the staged source call matches.
! 4. Type 4 CQ accepts only unbracketed c11 CQ targets; bracketed CQ targets are rejected and decoded CQ targets stay unbracketed.
! 5. DX-macro expansion is performed by callers through pack77_options before this gate.
! Any other source-vs-decode difference is rejected by this gate.
pure logical function pack77_gate_messages_match(input_msg,decoded_msg, &
     staged_calls,nstaged) result(ok)
  character(len=*), intent(in) :: input_msg,decoded_msg
  character(len=13), intent(in) :: staged_calls(:)
  integer, intent(in) :: nstaged
  logical :: resolved_render_fits

  call pack77_gate_message_checks(input_msg,decoded_msg,staged_calls,nstaged, &
       ok,resolved_render_fits)
end function pack77_gate_messages_match

pure logical function pack77_gate_staged_call_matches(input_body,staged_calls, &
     nstaged) result(ok)
  character(len=*), intent(in) :: input_body
  character(len=13), intent(in) :: staged_calls(:)
  integer, intent(in) :: nstaged
  integer :: i,n

  ok=.false.
  n=min(max(nstaged,0),size(staged_calls))
  do i=1,n
     if(trim(staged_calls(i)).eq.trim(input_body)) then
        ok=.true.
        return
     endif
  enddo
end function pack77_gate_staged_call_matches

pure logical function pack77_gate_resolved_render_fits(input_msg,decoded_msg, &
     staged_calls,nstaged) result(ok)
  character(len=*), intent(in) :: input_msg,decoded_msg
  character(len=13), intent(in) :: staged_calls(:)
  integer, intent(in) :: nstaged
  logical :: messages_match

  call pack77_gate_message_checks(input_msg,decoded_msg,staged_calls,nstaged, &
       messages_match,ok)
end function pack77_gate_resolved_render_fits

logical function pack77_c28_standard_shape(token) result(ok)
  character(len=*), intent(in) :: token
  integer :: i,n,iarea,npdig,nplet,nslet
  character(len=1) :: c

  ok=.false.
  n=len_trim(token)
  if(n.lt.3 .or. n.gt.6) return
  do i=1,n
     c=token(i:i)
     if(.not.((c.ge.'A' .and. c.le.'Z') .or. pack77_is_digit_char(c))) return
  enddo

  iarea=0
  do i=n,2,-1
     if(pack77_is_digit_char(token(i:i))) then
        iarea=i
        exit
     endif
  enddo
  if(iarea.ne.2 .and. iarea.ne.3) return

  npdig=0
  nplet=0
  do i=1,iarea-1
     c=token(i:i)
     if(pack77_is_digit_char(c)) then
        npdig=npdig+1
     else if(c.ge.'A' .and. c.le.'Z') then
        nplet=nplet+1
     else
        return
     endif
  enddo
  if(nplet.eq.0 .or. npdig.ge.iarea-1) return

  nslet=0
  do i=iarea+1,n
     c=token(i:i)
     if(c.ge.'A' .and. c.le.'Z') then
        nslet=nslet+1
     else
        return
     endif
  enddo
  ok=nslet.le.3
end function pack77_c28_standard_shape

logical function pack77_type12_standard_call(token) result(ok)
  character(len=*), intent(in) :: token

  ok=.false.
  if(.not.pack77_c28_standard_shape(token)) return
  ! Q-prefix rejection is a source-grammar policy, not a c28 wire shape fact.
  if(token(1:1).eq.'Q') return
  ok=.true.
end function pack77_type12_standard_call


subroutine pack77_parse_type12_call(token,allow_special,call_source,ok)
  character(len=*), intent(in) :: token
  logical, intent(in) :: allow_special
  type(pack77_type12_call_source), intent(out) :: call_source
  logical, intent(out) :: ok
  character(len=37) :: base,modifier
  integer :: n,slash

  call_source=pack77_type12_call_source()
  ok=.false.
  n=len_trim(token)
  if(n.lt.1 .or. n.gt.13) return

  if(allow_special) then
     if(trim(token).eq.'CQ') then
        call_source%c28_token='CQ'
        call_source%is_cq=.true.
        ok=.true.
        return
     endif
     if(trim(token).eq.'DE' .or. trim(token).eq.'QRZ') then
        call_source%c28_token=trim(token)
        ok=.true.
        return
     endif
     if(n.ge.4 .and. token(1:3).eq.'CQ_') then
        modifier='                                     '
        modifier=token(4:n)
        if(.not.pack77_valid_cq_modifier(modifier)) return
        call_source%c28_token=trim(token)
        call_source%is_cq=.true.
        ok=.true.
        return
     endif
  endif

  if(token(1:1).eq.'<' .or. index(token(1:n),'>').gt.0) then
     if(.not.pack77_valid_hash_call_token(token)) return
     call_source%c28_token=trim(token)
     call_source%is_hash=.true.
     ok=.true.
     return
  endif

  slash=index(token(1:n),'/')
  base='                                     '
  if(slash.gt.0) then
     if(slash.lt.2 .or. slash.ne.n-1) return
     if(index(token(slash+1:n),'/').gt.0) return
     if(token(slash:n).eq.'/R') then
        call_source%suffix=PACK77_TYPE12_SUFFIX_R
     else if(token(slash:n).eq.'/P') then
        call_source%suffix=PACK77_TYPE12_SUFFIX_P
     else
        return
     endif
     base=token(1:slash-1)
  else
     base=token(1:n)
  endif

  if(.not.pack77_type12_standard_call(base)) return
  call_source%c28_token=trim(base)
  ok=.true.
end subroutine pack77_parse_type12_call

logical function pack77_type12_call_has_suffix(call_source) result(ok)
  type(pack77_type12_call_source), intent(in) :: call_source

  ok=call_source%suffix.ne.PACK77_TYPE12_SUFFIX_NONE
end function pack77_type12_call_has_suffix

subroutine pack77_parse_type12_source(msg,source,ok)
  character(len=*), intent(in) :: msg
  type(pack77_type12_source), intent(out) :: source
  logical, intent(out) :: ok
  character(len=37) :: w(5),call_1_token,tail
  integer :: nwords,call_2_index,tail_start,ntail,itail,isnr,irpt
  logical :: split_ok,call_ok,tail_is_grid

  source=pack77_type12_source()
  ok=.false.
  call pack77_split_source_tokens(msg,5,nwords,w,split_ok)
  if(.not.split_ok) return
  if(nwords.lt.2) return

  call_1_token=w(1)
  call_2_index=2
  tail_start=3
  if(trim(w(1)).eq.'CQ' .and. nwords.ge.3 .and. &
       pack77_valid_cq_modifier(w(2))) then
     call_1_token='                                     '
     call_1_token='CQ_'//trim(w(2))
     call_2_index=3
     tail_start=4
  endif

  call pack77_parse_type12_call(call_1_token,.true.,source%call_1,call_ok)
  if(.not.call_ok) return
  call pack77_parse_type12_call(w(call_2_index),.false.,source%call_2,call_ok)
  if(.not.call_ok) return
  if(source%call_1%is_cq .and. source%call_2%is_hash) return
  if(source%call_1%is_hash .and. &
       (pack77_type12_call_has_suffix(source%call_2) .or. &
       index(source%call_2%c28_token,'/').gt.0)) return
  if(source%call_2%is_hash .and. &
       (pack77_type12_call_has_suffix(source%call_1) .or. &
       index(source%call_1%c28_token,'/').gt.0)) return

  if(source%call_1%suffix.eq.PACK77_TYPE12_SUFFIX_R .and. &
       source%call_2%suffix.eq.PACK77_TYPE12_SUFFIX_P) return
  if(source%call_1%suffix.eq.PACK77_TYPE12_SUFFIX_P .and. &
       source%call_2%suffix.eq.PACK77_TYPE12_SUFFIX_R) return

  source%i3=1
  if(source%call_1%suffix.eq.PACK77_TYPE12_SUFFIX_P .or. &
       source%call_2%suffix.eq.PACK77_TYPE12_SUFFIX_P) source%i3=2

  ntail=nwords-tail_start+1
  if(ntail.lt.0 .or. ntail.gt.2) return
  if(ntail.eq.0) then
     source%ir=0
     source%igrid4=PACK77_MAXGRID4+1
     ok=.true.
     return
  endif

  if(ntail.eq.2) then
     if(trim(w(tail_start)).ne.'R') return
     if(source%call_1%is_cq) return
     if(len_trim(w(tail_start+1)).ne.4) return
     if(.not.pack77_is_grid4(w(tail_start+1))) return
     source%ir=1
     source%igrid4=pack77_grid4_index(w(tail_start+1)(1:4))
     ok=.true.
     return
  endif

  tail=w(tail_start)
  tail_is_grid=.false.
  if(len_trim(tail).eq.4) tail_is_grid=pack77_is_grid4(tail)
  if(tail_is_grid) then
     source%ir=0
     source%igrid4=pack77_grid4_index(tail(1:4))
     ok=.true.
     return
  endif
  if(source%call_1%is_cq) return

  if(tail(1:1).eq.'+' .or. tail(1:1).eq.'-') then
     call pack77_parse_signed_report(tail,isnr,ok)
     if(.not.ok) return
     irpt=pack77_report_index_from_snr(isnr)
     if(irpt.lt.1) then
        ok=.false.
        return
     endif
     source%ir=0
     source%igrid4=PACK77_MAXGRID4+irpt
     ok=.true.
     return
  endif
  if(tail(1:2).eq.'R+' .or. tail(1:2).eq.'R-') then
     call pack77_parse_signed_report(tail(2:),isnr,ok)
     if(.not.ok) return
     irpt=pack77_report_index_from_snr(isnr)
     if(irpt.lt.1) then
        ok=.false.
        return
     endif
     source%ir=1
     source%igrid4=PACK77_MAXGRID4+irpt
     ok=.true.
     return
  endif

  itail=pack77_qso_tail_index(tail)
  if(itail.lt.1) return
  source%ir=0
  source%igrid4=PACK77_MAXGRID4+pack77_type12_irpt_from_tail(itail)
  ok=.true.
end subroutine pack77_parse_type12_source
logical function pack77_valid_c28_call_slot(token) result(ok)
  character(len=*), intent(in) :: token
  integer :: i,n,slash

  ok=.false.
  n=len_trim(token)
  if(n.lt.3 .or. n.gt.13) return
  if(pack77_valid_hash_call_token(token)) return
  do i=1,n
     if(.not.((token(i:i).ge.'A' .and. token(i:i).le.'Z') .or. &
          pack77_is_digit_char(token(i:i)) .or. token(i:i).eq.'/')) return
  enddo
  if(pack77_type12_standard_call(token)) then
     ok=.true.
     return
  endif
  slash=index(token(1:n),'/')
  if(slash.lt.2 .or. slash.ge.n) return
  if(index(token(slash+1:n),'/').gt.0) return
  ok=.true.
end function pack77_valid_c28_call_slot

subroutine pack77_parse_dxpedition_source(msg,source,ok,matched_shape)
! DXpedition messages encode two c28 calls, a 10-bit hashed callsign, and a
! report in the fixed "CALL RR73; CALL <HASH> REPORT" token shape.
  character(len=*), intent(in) :: msg
  type(pack77_dxpedition_source), intent(out) :: source
  logical, intent(out) :: ok
  logical, intent(out) :: matched_shape
  character(len=37) :: w(5)
  integer :: nwords
  logical :: split_ok

  source=pack77_dxpedition_source()
  ok=.false.
  matched_shape=.false.
  call pack77_split_source_tokens(msg,5,nwords,w,split_ok)
  if(.not.split_ok) return
  if(nwords.ne.5) return
  if(trim(w(2)).ne.'RR73;') return
  matched_shape=.true.
  if(.not.pack77_valid_c28_call_slot(w(1))) return
  if(.not.pack77_valid_c28_call_slot(w(3))) return
  if(.not.pack77_valid_hash_call_token(w(4))) return
  call pack77_parse_dxpedition_report(w(5),source%n5,ok)
  if(.not.ok) return
  source%call_1=trim(w(1))
  source%call_2=trim(w(3))
  source%hash_token=trim(w(4))
end subroutine pack77_parse_dxpedition_source

subroutine pack77_parse_field_day_source(msg,source,ok,matched_shape)
  character(len=*), intent(in) :: msg
  type(pack77_field_day_source), intent(out) :: source
  logical, intent(out) :: ok
  logical, intent(out) :: matched_shape
  character(len=37) :: w(5),section
  integer :: nwords,exchange_index,section_index
  logical :: split_ok

  source=pack77_field_day_source()
  ok=.false.
  matched_shape=.false.
  call pack77_split_source_tokens(msg,5,nwords,w,split_ok)
  if(.not.split_ok) return
  if(nwords.lt.4 .or. nwords.gt.5) return
  section_index=nwords
  source%isec=pack77_arrl_section_index(w(section_index))
  if(source%isec.eq.-1) return
  section=pack77_arrl_section_name(source%isec)
  if(len_trim(w(section_index)).ne.len_trim(section)) return
  matched_shape=.true.
  if(.not.pack77_valid_c28_call_slot(w(1))) return
  if(.not.pack77_valid_c28_call_slot(w(2))) return
  if(nwords.eq.5) then
     if(trim(w(3)).ne.'R') return
     source%ir=1
  endif
  exchange_index=nwords-1
  call pack77_parse_field_day_exchange(w(exchange_index), &
       source%ntx,source%nclass,ok)
  if(.not.ok) return
  source%call_1=trim(w(1))
  source%call_2=trim(w(2))
  ok=.true.
end subroutine pack77_parse_field_day_source

subroutine pack77_parse_wspr_source(msg,source,ok,matched_shape)
  character(len=*), intent(in) :: msg
  type(pack77_wspr_source), intent(out) :: source
  logical, intent(out) :: ok
  logical, intent(out) :: matched_shape
  character(len=37) :: w(3),base
  character(len=3) :: cpfx
  integer :: nwords,m1,m2,slash
  logical :: split_ok,is_prefix,grid4_ok

  source=pack77_wspr_source()
  ok=.false.
  matched_shape=.false.
  call pack77_split_source_tokens(msg,3,nwords,w,split_ok)
  if(.not.split_ok) return
  if(nwords.lt.2 .or. nwords.gt.3) return
  m1=len_trim(w(1))
  m2=len_trim(w(2))

  grid4_ok=.false.
  if(nwords.eq.3) then
     grid4_ok=m2.eq.4 .and. pack77_is_grid4(w(2))
     matched_shape=grid4_ok .and. pack77_all_digits(w(3))
  else if(nwords.eq.2) then
     matched_shape=index(w(1)(1:m1),'/').gt.0 .and. pack77_all_digits(w(2))
  endif

  if(nwords.eq.3) then
     if(.not.pack77_type12_standard_call(w(1))) return
     if(.not.grid4_ok) return
     call pack77_parse_wspr_dbm(w(3),source%idbm,ok)
     if(.not.ok) return
     source%subtype=1
     source%call_token=trim(w(1))
     source%grid4=w(2)(1:4)
     ok=.true.
     return
  endif

  if(m1.ge.5 .and. m1.le.10 .and. m2.le.2) then
     slash=index(w(1)(1:m1),'/')
     if(slash.ge.2 .and. slash.lt.m1 .and. &
          index(w(1)(slash+1:m1),'/').eq.0) then
        base='                                     '
        if(slash.le.4) then
           base=w(1)(slash+1:m1)
        else
           base=w(1)(1:slash-1)
        endif
        if(pack77_type12_standard_call(base)) then
           call pack77_parse_wspr_dbm(w(2),source%idbm,ok)
           if(.not.ok) return
           source%npfx=pack77_wspr_affix_index(w(1),slash,m1,ok)
           if(.not.ok) return
           call pack77_wspr_affix_text(source%npfx,cpfx,is_prefix,ok)
           if(.not.ok) return
           ok=.false.
           cpfx=adjustl(cpfx)
           if(slash.le.4) then
              if(.not.is_prefix) return
              if(trim(cpfx).ne.w(1)(1:slash-1)) return
           else
              if(is_prefix) return
              if(trim(cpfx).ne.w(1)(slash+1:m1)) return
           endif
           source%subtype=2
           source%call_token=trim(w(1))
           source%base_call=trim(base)
           ok=.true.
           return
        endif
     endif
  endif

  if(m1.ge.5 .and. m1.le.12 .and. m2.le.6) then
     if(.not.pack77_valid_hash_call_token(w(1))) return
     if(.not.pack77_is_grid6(w(2),.true.)) return
     source%subtype=3
     source%hash_token=trim(w(1))
     source%grid6=w(2)(1:6)
     ok=.true.
  endif
end subroutine pack77_parse_wspr_source

subroutine pack77_parse_type3_source(msg,source,ok,matched_shape)
  character(len=*), intent(in) :: msg
  type(pack77_type3_source), intent(out) :: source
  logical, intent(out) :: ok
  logical, intent(out) :: matched_shape
  character(len=37) :: w(6),exchange
  integer :: nwords,report_index,exchange_index,nserial,imult
  logical :: split_ok,serial_ok

  source=pack77_type3_source()
  ok=.false.
  matched_shape=.false.
  call pack77_split_source_tokens(msg,6,nwords,w,split_ok)
  if(.not.split_ok) return
  if(nwords.lt.4 .or. nwords.gt.6) return

  if(trim(w(1)).eq.'TU;') source%itu=1
  if(trim(w(3+source%itu)).eq.'R') source%ir=1
  report_index=3+source%itu+source%ir
  if(report_index.le.nwords) &
       matched_shape=pack77_rtty_report_index(w(report_index)).ge.0
  if(.not.pack77_valid_c28_call_slot(w(1+source%itu))) return
  if(.not.pack77_valid_c28_call_slot(w(2+source%itu))) return
  source%call_1=trim(w(1+source%itu))
  source%call_2=trim(w(2+source%itu))

  exchange_index=report_index+1
  if(exchange_index.ne.nwords) return

  source%irpt=pack77_rtty_report_index(w(report_index))
  if(source%irpt.lt.0) return

  call pack77_parse_type3_serial(w(exchange_index),nserial,serial_ok)
  if(serial_ok) then
     source%nexch=nserial
     ok=.true.
     return
  endif

  imult=pack77_rtty_multiplier_index(w(exchange_index))
  if(imult.le.0) return
  exchange=pack77_rtty_multiplier_name(imult)
  if(len_trim(w(exchange_index)).ne.len_trim(exchange)) return
  source%nexch=8000+imult
  ok=.true.
end subroutine pack77_parse_type3_source

subroutine pack77_parse_type4_source(msg,source,ok)
  character(len=*), intent(in) :: msg
  type(pack77_type4_source), intent(out) :: source
  logical, intent(out) :: ok
  character(len=37) :: w(3),c11_token
  integer :: nwords
  logical :: split_ok,hash_1,hash_2

  source=pack77_type4_source()
  ok=.false.
  call pack77_split_source_tokens(msg,3,nwords,w,split_ok)
  if(.not.split_ok) return
  if(nwords.lt.2 .or. nwords.gt.3) return

  hash_1=pack77_valid_hash_call_token(w(1))
  hash_2=pack77_valid_hash_call_token(w(2))

  if(trim(w(1)).eq.'CQ') then
     if(nwords.ne.2) return
     if(hash_2) return
     c11_token=w(2)
     if(len_trim(c11_token).lt.5) return
     if(.not.pack77_valid_type4_c11(c11_token)) return
     if(pack77_type12_standard_call(c11_token)) return
     source%c11=c11_token(1:11)
     source%icq=1
     ok=.true.
     return
  endif

  if(nwords.eq.3) then
     source%itail=pack77_qso_tail_index(w(3))
     if(source%itail.eq.0) return
  endif

  if(hash_1 .and. .not.hash_2) then
     if(.not.pack77_valid_type4_c11(w(2))) return
     if(pack77_type12_standard_call(w(2))) return
     source%hash_token=trim(w(1))
     source%c11=w(2)(1:11)
     source%iflip=0
     ok=.true.
     return
  endif

  if(hash_2 .and. .not.hash_1) then
     if(.not.pack77_valid_type4_c11(w(1))) return
     if(pack77_type12_standard_call(w(1))) return
     source%hash_token=trim(w(2))
     source%c11=w(1)(1:11)
     source%iflip=1
     ok=.true.
     return
  endif
end subroutine pack77_parse_type4_source

subroutine pack77_parse_type5_source(msg,source,ok,matched_shape)
! Type 5 EU VHF messages carry two explicit hash tokens, an optional R,
! report+serial as 52..59 plus 0001..2047, and a required six-character grid.
  character(len=*), intent(in) :: msg
  type(pack77_type5_source), intent(out) :: source
  logical, intent(out) :: ok
  logical, intent(out) :: matched_shape
  character(len=37) :: w(5)
  integer :: nwords,exchange_index,grid_index,nx
  logical :: split_ok

  source=pack77_type5_source()
  ok=.false.
  matched_shape=.false.
  call pack77_split_source_tokens(msg,5,nwords,w,split_ok)
  if(.not.split_ok) return
  if(nwords.lt.4 .or. nwords.gt.5) return
  matched_shape=pack77_valid_hash_call_token(w(1)) .and. &
       pack77_valid_hash_call_token(w(2))
  if(.not.pack77_valid_hash_call_token(w(1))) return
  if(.not.pack77_valid_hash_call_token(w(2))) return
  if(nwords.eq.5) then
     if(trim(w(3)).ne.'R') return
     source%ir=1
  endif

  exchange_index=nwords-1
  grid_index=nwords
  call pack77_valid_type5_exchange(w(exchange_index),nx,ok)
  if(.not.ok) return
  if(nx.lt.520001 .or. nx.gt.592047) then
     ok=.false.
     return
  endif
  source%iserial=mod(nx,10000)
  if(source%iserial.lt.1 .or. source%iserial.gt.2047) then
     ok=.false.
     return
  endif
  if(len_trim(w(grid_index)).ne.6) then
     ok=.false.
     return
  endif
  if(.not.pack77_is_grid6(w(grid_index),.false.)) then
     ok=.false.
     return
  endif

  source%hash_token_1=trim(w(1))
  source%hash_token_2=trim(w(2))
  source%irpt=nx/10000 - 52
  source%grid6=w(grid_index)(1:6)
  ok=.true.
end subroutine pack77_parse_type5_source

logical function pack77_valid_type4_c11(token) result(ok)
  character(len=*), intent(in) :: token
  integer :: i,n

  ok=.false.
  n=len_trim(token)
  if(n.lt.1 .or. n.gt.11) return
  do i=1,n
     if(index(PACK77_BASE38,token(i:i)).lt.1) return
  enddo
  ok=.true.
end function pack77_valid_type4_c11

subroutine pack77_valid_type5_exchange(token,nx,ok)
  character(len=*), intent(in) :: token
  integer, intent(out) :: nx
  logical, intent(out) :: ok

  nx=-1
  ok=.false.
  if(len_trim(token).ne.6) return
  call pack77_parse_uint(token,nx,ok)
end subroutine pack77_valid_type5_exchange

integer function pack77_wspr_affix_index(token,slash_index,token_len,ok) result(npfx)
  character(len=*), intent(in) :: token
  integer, intent(in) :: slash_index,token_len
  logical, intent(out) :: ok
  integer :: i

  ok=.true.
  npfx=0
  if(slash_index.le.4 .and. token_len-slash_index.lt.3) then
     ok=.false.
     return
  endif
  if(slash_index.gt.4 .and. token_len-slash_index.gt.3) then
     ok=.false.
     return
  endif
  do i=1,slash_index-1
     if(index(PACK77_BASE36,token(i:i)).lt.1) then
        ok=.false.
        return
     endif
  enddo
  do i=slash_index+1,token_len
     if(index(PACK77_BASE36,token(i:i)).lt.1) then
        ok=.false.
        return
     endif
  enddo
  if(slash_index.le.4) then
     npfx=index(PACK77_BASE36,token(1:1))-1
     if(slash_index.ge.3) npfx=36*npfx + index(PACK77_BASE36,token(2:2))-1
     if(slash_index.eq.4) npfx=36*npfx + index(PACK77_BASE36,token(3:3))-1
  else
     if((token_len-slash_index).eq.1) &
          npfx=index(PACK77_BASE36,token(slash_index+1:slash_index+1))-1
     if((token_len-slash_index).eq.2) &
          npfx=36*(index(PACK77_BASE36,token(slash_index+1:slash_index+1))-1) + &
          index(PACK77_BASE36,token(slash_index+2:slash_index+2))-1
     if((token_len-slash_index).eq.3) then
        if(.not.pack77_is_digit_char(token(slash_index+3:slash_index+3))) then
           ok=.false.
           return
        endif
        npfx=36*10*(index(PACK77_BASE36,token(slash_index+1:slash_index+1))-1) + &
             10*(index(PACK77_BASE36,token(slash_index+2:slash_index+2))-1) + &
             index(PACK77_BASE36,token(slash_index+3:slash_index+3))-1
     endif
     npfx=npfx + PACK77_WSPR_NZZZ
  endif

  return
end function pack77_wspr_affix_index

subroutine pack77_wspr_affix_text(npfx,cpfx,is_prefix,ok)
  integer, intent(in) :: npfx
  character(len=3), intent(out) :: cpfx
  logical, intent(out) :: is_prefix,ok
  integer :: n,i,j

  n=npfx
  cpfx='   '
  ok=.true.
  is_prefix=n.lt.PACK77_WSPR_NZZZ
  if(is_prefix) then
     do i=3,1,-1
        j=mod(n,36)+1
        cpfx(i:i)=PACK77_BASE36(j:j)
        n=n/36
        if(n.eq.0) exit
     enddo
  else
     n=n-PACK77_WSPR_NZZZ
     if(n.le.35) then
        cpfx(1:1)=PACK77_BASE36(n+1:n+1)
     else if(n.gt.35 .and. n.le.1295) then
        cpfx(1:1)=PACK77_BASE36(n/36+1:n/36+1)
        cpfx(2:2)=PACK77_BASE36(mod(n,36)+1:mod(n,36)+1)
     else if(n.gt.1295 .and. n.le.12959) then
        cpfx(1:1)=PACK77_BASE36(n/360+1:n/360+1)
        cpfx(2:2)=PACK77_BASE36(mod(n/10,36)+1:mod(n/10,36)+1)
        cpfx(3:3)=PACK77_BASE36(mod(n,10)+1:mod(n,10)+1)
     else
        ok=.false.
     endif
  endif

  return
end subroutine pack77_wspr_affix_text

character(len=11) function pack77_type4_call_text_from_n58(n58) result(c11)
  integer*8, intent(in) :: n58
  integer*8 :: n
  integer :: i,j

  c11='           '
  n=n58
  do i=11,1,-1
     j=mod(n,38_8)+1
     c11(i:i)=PACK77_BASE38(j:j)
     n=n/38
  enddo

  return
end function pack77_type4_call_text_from_n58

integer*8 function pack77_type4_n58_from_call_text(c11) result(n58)
  character(len=*), intent(in) :: c11
  integer :: i

  n58=0_8
  do i=1,11
     n58=n58*38_8 + index(PACK77_BASE38,c11(i:i)) - 1
  enddo

  return
end function pack77_type4_n58_from_call_text

subroutine to_grid4(n,grid4,ok)
  character*4 grid4
  logical ok
  integer :: n,j1,j2,j3,j4

  ok=.false.
  j1=n/(18*10*10)
  if (j1.lt.0.or.j1.gt.17) goto 900
  n=n-j1*18*10*10
  j2=n/(10*10)
  if (j2.lt.0.or.j2.gt.17) goto 900
  n=n-j2*10*10
  j3=n/10
  if (j3.lt.0.or.j3.gt.9) goto 900
  j4=n-j3*10
  if (j4.lt.0.or.j4.gt.9) goto 900
  grid4(1:1)=char(j1+ichar('A'))
  grid4(2:2)=char(j2+ichar('A'))
  grid4(3:3)=char(j3+ichar('0'))
  grid4(4:4)=char(j4+ichar('0'))
  ok=.true.

900 return
end subroutine to_grid4

subroutine to_grid6(n,grid6,ok)
  character*6 grid6
  logical ok
  integer :: n,j1,j2,j3,j4,j5,j6

  ok=.false.
  j1=n/(18*10*10*24*24)
  if (j1.lt.0.or.j1.gt.17) goto 900
  n=n-j1*18*10*10*24*24
  j2=n/(10*10*24*24)
  if (j2.lt.0.or.j2.gt.17) goto 900
  n=n-j2*10*10*24*24
  j3=n/(10*24*24)
  if (j3.lt.0.or.j3.gt.9) goto 900
  n=n-j3*10*24*24
  j4=n/(24*24)
  if (j4.lt.0.or.j4.gt.9) goto 900
  n=n-j4*24*24
  j5=n/24
  if (j5.lt.0.or.j5.gt.23) goto 900
  j6=n-j5*24
  if (j6.lt.0.or.j6.gt.23) goto 900
  grid6(1:1)=char(j1+ichar('A'))
  grid6(2:2)=char(j2+ichar('A'))
  grid6(3:3)=char(j3+ichar('0'))
  grid6(4:4)=char(j4+ichar('0'))
  grid6(5:5)=char(j5+ichar('A'))
  grid6(6:6)=char(j6+ichar('A'))
  ok=.true.

900 return
end subroutine to_grid6

subroutine to_grid(n,grid6,ok)
  ! 4-, or 6-character grid
  character*6 grid6
  logical ok
  integer :: n,j1,j2,j3,j4,j5,j6

  ok=.false.
  j1=n/(18*10*10*25*25)
  if (j1.lt.0.or.j1.gt.17) goto 900
  n=n-j1*18*10*10*25*25
  j2=n/(10*10*25*25)
  if (j2.lt.0.or.j2.gt.17) goto 900
  n=n-j2*10*10*25*25
  j3=n/(10*25*25)
  if (j3.lt.0.or.j3.gt.9) goto 900
  n=n-j3*10*25*25
  j4=n/(25*25)
  if (j4.lt.0.or.j4.gt.9) goto 900
  n=n-j4*25*25
  j5=n/25
  if (j5.lt.0.or.j5.gt.24) goto 900
  j6=n-j5*25
  if (j6.lt.0.or.j6.gt.24) goto 900
  grid6=''
  grid6(1:1)=char(j1+ichar('A'))
  grid6(2:2)=char(j2+ichar('A'))
  grid6(3:3)=char(j3+ichar('0'))
  grid6(4:4)=char(j4+ichar('0'))
  if (j5.ne.24.or.j6.ne.24) then
     grid6(5:5)=char(j5+ichar('A'))
     grid6(6:6)=char(j6+ichar('A'))
  endif
  ok=.true.

900 return
end subroutine to_grid

logical function pack77_is_grid4(grid4) result(ok)
  character(len=*), intent(in) :: grid4

  ok=.false.
  if(len(grid4).lt.4) return
  if(len(trim(grid4)).ne.4) return
  ok=grid4(1:1).ge.'A' .and. grid4(1:1).le.'R' .and. &
       grid4(2:2).ge.'A' .and. grid4(2:2).le.'R' .and. &
       grid4(3:3).ge.'0' .and. grid4(3:3).le.'9' .and. &
       grid4(4:4).ge.'0' .and. grid4(4:4).le.'9'

  return
end function pack77_is_grid4

logical function pack77_is_grid6(grid6,allow_grid4) result(ok)
  character(len=*), intent(in) :: grid6
  logical, intent(in) :: allow_grid4
  integer :: ngrid

  ok=.false.
  if(len(grid6).lt.4) return
  ngrid=len(trim(grid6))
  if(ngrid.eq.4) then
     ok=allow_grid4 .and. pack77_is_grid4(grid6(1:4))
     return
  endif
  if(len(grid6).lt.6 .or. ngrid.ne.6) return
  ok=pack77_is_grid4(grid6(1:4)) .and. &
       grid6(5:5).ge.'A' .and. grid6(5:5).le.'X' .and. &
       grid6(6:6).ge.'A' .and. grid6(6:6).le.'X'

  return
end function pack77_is_grid6

integer function pack77_grid4_index(grid4) result(igrid4)
  character(len=*), intent(in) :: grid4

  igrid4=(ichar(grid4(1:1))-ichar('A'))*18*10*10 + &
       (ichar(grid4(2:2))-ichar('A'))*10*10 + &
       (ichar(grid4(3:3))-ichar('0'))*10 + &
       (ichar(grid4(4:4))-ichar('0'))

  return
end function pack77_grid4_index

integer function pack77_grid6_index(grid6) result(igrid6)
  character(len=*), intent(in) :: grid6

  igrid6=pack77_grid4_index(grid6(1:4))*24*24 + &
       (ichar(grid6(5:5))-ichar('A'))*24 + &
       (ichar(grid6(6:6))-ichar('A'))

  return
end function pack77_grid6_index

integer function pack77_grid6_wspr_index(grid6) result(igrid6)
  character(len=*), intent(in) :: grid6

  igrid6=pack77_grid4_index(grid6(1:4))*25*25
  if(grid6(5:6).eq.'  ') then
     igrid6=igrid6 + 24*25 + 24
  else
     igrid6=igrid6 + (ichar(grid6(5:5))-ichar('A'))*25 + &
          (ichar(grid6(6:6))-ichar('A'))
  endif

  return
end function pack77_grid6_wspr_index

end module packjt77_grammar
