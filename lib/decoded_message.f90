module decoded_message
  use, intrinsic :: iso_c_binding
  use packjt77_grammar
  implicit none
  private
  public :: decoder_message_fields,decoder_message_parse,parse_received_message

  integer(c_int), parameter :: call_missing=0,call_literal=1,call_resolved=2,call_unresolved=3,call_malformed=4
  integer(c_int), parameter :: message_none=0,message_text=1,message_cq=2,message_directed=3, &
       message_dual=4,message_contest=5,message_beacon=6
  integer(c_int), parameter :: exchange_none=0,exchange_other=1,exchange_grid=2,exchange_report=3,exchange_rreport=4
  integer(c_int), parameter :: exchange_ro=5,exchange_rrr=6,exchange_rr73=7,exchange_signoff=8,exchange_contest=9
  integer(c_int), parameter :: exchange_power=10,exchange_ooo=11

  type, bind(C) :: decoder_message_fields
     integer(c_int) :: kind=message_none,exchange_kind=exchange_none
     integer(c_int) :: cq=0,malformed=0,structured=0,complete_tail=0
     integer(c_int) :: report_present=0,report=0
     integer(c_int) :: destination_state=call_missing,sender_state=call_missing,acknowledged_recipient_state=call_missing
     integer(c_int) :: word_count=0,exchange_first_word=0
     character(c_char) :: destination(38)=c_null_char,sender(38)=c_null_char,exchange(38)=c_null_char
     character(c_char) :: acknowledged_recipient(38)=c_null_char,grid(7)=c_null_char,words(38,19)=c_null_char
  end type

contains

  integer(c_int) function decoder_message_parse(mode,payload,length,result) bind(C) result(status)
    integer(c_int), value :: mode,length
    type(c_ptr), value :: payload,result
    type(decoder_message_fields), pointer :: fields
    character(c_char), pointer :: bytes(:)
    character(len=37) :: text
    integer :: i

    status=1
    if(.not.c_associated(result)) return
    call c_f_pointer(result,fields)
    fields=decoder_message_fields()
    if(length<0.or.length>37) return
    if(length>0.and..not.c_associated(payload)) return
    text=''
    if(length>0) then
       call c_f_pointer(payload,bytes,[int(length)])
       do i=1,length
          if(bytes(i)==c_null_char) return
          text(i:i)=bytes(i)
       enddo
    endif
    call parse_received_message(mode,text(:length),fields)
    status=0
  end function

  subroutine copy_text(text,output)
    character(len=*), intent(in) :: text
    character(c_char), intent(out) :: output(:)
    integer :: i

    output=c_null_char
    do i=1,min(len_trim(text),size(output)-1)
       output(i)=text(i:i)
    enddo
  end subroutine

  logical function literal_token(token) result(ok)
    character(len=*), intent(in) :: token
    integer :: i,n

    n=len_trim(token)
    ok=n>0
    do i=1,n
       if(index('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/',token(i:i))==0) ok=.false.
    enddo
  end function

  integer(c_int) function token_state(token) result(state)
    character(len=*), intent(in) :: token
    integer :: n

    n=len_trim(token)
    state=call_missing
    if(n==0) return
    state=call_unresolved
    if(token=='...'.or.token=='<...>') return
    state=call_literal
    if(literal_token(token)) return
    state=call_malformed
    if(n<3) return
    if(token(1:1)/='<'.or.token(n:n)/='>') return
    if(literal_token(token(2:n-1))) state=call_resolved
  end function

  function bare_token(token) result(bare)
    character(len=*), intent(in) :: token
    character(len=37) :: bare
    integer :: i,n

    bare=''
    n=0
    do i=1,len_trim(token)
       if(token(i:i)=='<'.or.token(i:i)=='>') cycle
       n=n+1
       bare(n:n)=token(i:i)
    enddo
  end function

  logical function modifier(token) result(ok)
    character(len=*), intent(in) :: token
    integer :: n

    n=len_trim(token)
    ok=.false.
    if(n>=1.and.n<=4) ok=verify(trim(token),'ABCDEFGHIJKLMNOPQRSTUVWXYZ')==0
    if(n==3) ok=ok.or.verify(trim(token),'0123456789')==0
  end function

  logical function receive_call(token) result(ok)
    character(len=*), intent(in) :: token
    character(len=37) :: bare
    integer :: state,n

    state=token_state(token)
    ok=state==call_unresolved
    if(ok) return
    if(state/=call_literal.and.state/=call_resolved) return
    bare=bare_token(token)
    n=len_trim(bare)
    if(n<3.or.n>11) return
    ok=verify(trim(bare),'ABCDEFGHIJKLMNOPQRSTUVWXYZ/')/=0.and. &
         verify(trim(bare),'0123456789/')/=0
  end function

  logical function grid_token(token) result(ok)
    character(len=*), intent(in) :: token
    integer :: n

    n=len_trim(token)
    ok=.false.
    if(token=='RR73') return
    if(n==4) ok=pack77_is_grid4(token)
    if(n==6) ok=pack77_is_grid6(token,.false.)
  end function

  subroutine classify_modern(text,structured,contest)
    character(len=*), intent(in) :: text
    logical, intent(out) :: structured,contest
    type(pack77_type12_source) :: type12
    type(pack77_type4_source) :: type4
    type(pack77_type5_source) :: type5
    type(pack77_dxpedition_source) :: dual
    type(pack77_field_day_source) :: field_day
    type(pack77_type3_source) :: type3
    type(pack77_wspr_source) :: wspr
    logical :: shape

    contest=.false.
    call pack77_parse_type12_source(text,type12,structured)
    if(structured) return
    call pack77_parse_type4_source(text,type4,structured)
    if(structured) return
    call pack77_parse_dxpedition_source(text,dual,structured,shape,allow_hash_calls=.true.)
    if(structured) return
    call pack77_parse_type5_source(text,type5,structured,shape)
    if(.not.structured) call pack77_parse_field_day_source(text,field_day,structured,shape)
    if(.not.structured) call pack77_parse_type3_source(text,type3,structured,shape)
    contest=structured
    if(.not.structured) call pack77_parse_wspr_source(text,wspr,structured,shape)
  end subroutine

  subroutine parse_beacon(mode,payload,raw,words,n,fields,matched)
    integer(c_int), intent(in) :: mode
    character(len=*), intent(in) :: payload,raw(:),words(:)
    integer, intent(in) :: n
    type(decoder_message_fields), intent(inout) :: fields
    logical, intent(out) :: matched
    type(pack77_wspr_source) :: source
    integer :: power
    logical :: shape,ok

    matched=.false.
    if(fields%malformed/=0.or.n<2.or.n>3) return
    call pack77_parse_wspr_source(payload,source,ok,shape)
    if(.not.ok.and.n==3) then
       if(.not.receive_call(raw(1))) return
       if(token_state(raw(1))/=call_literal.or.mode==241) then
          if(.not.grid_token(words(2))) return
          call pack77_parse_wspr_dbm(words(3),power,ok)
          if(ok) then
             source%subtype=1
             source%call_token=raw(1)
             source%grid4=words(2)(1:4)
          endif
       endif
    endif
    if(.not.ok) return
    if(.not.receive_call(raw(1))) return
    matched=.true.
    fields%kind=message_beacon
    fields%structured=1
    fields%complete_tail=1
    fields%sender_state=token_state(raw(1))
    call copy_text(words(1),fields%sender)
    if(source%subtype==3) then
       fields%exchange_kind=exchange_grid
       fields%exchange_first_word=1
       call copy_text(source%grid6,fields%grid)
       call copy_text(words(2),fields%exchange)
    else
       fields%exchange_kind=exchange_power
       fields%exchange_first_word=n-1
       if(source%subtype==1) call copy_text(words(2),fields%grid)
       call copy_text(words(n),fields%exchange)
    endif
  end subroutine

  subroutine parse_received_message(mode,text,fields)
    integer(c_int), intent(in) :: mode
    character(len=*), intent(in) :: text
    type(decoder_message_fields), intent(out) :: fields
    character(len=37) :: raw(19),words(19),payload,destination,tail,report_text,classifier_payload
    integer :: i,n,start,first,sender,ntail,value
    logical :: structured,contest,ok,identities,legacy,beacon

    fields=decoder_message_fields()
    if(len(text)>37) return
    payload=adjustl(text)
    do i=1,len_trim(payload)
       if(iachar(payload(i:i))>=9.and.iachar(payload(i:i))<=13) payload(i:i)=' '
    enddo
    raw=''
    n=0
    start=0
    do i=1,len_trim(payload)+1
       if(i<=len_trim(payload)) then
          if(payload(i:i)/=' ') then
             if(start==0) start=i
             cycle
          endif
       endif
       if(start==0) cycle
       n=n+1
       raw(n)=payload(start:i-1)
       start=0
    enddo
    if(n==0) return
    fields%kind=message_text
    if(raw(1)=='TU;') then
       raw(1:n-1)=raw(2:n)
       raw(n)=''
       n=n-1
    endif
    fields%word_count=n
    fields%exchange_first_word=n
    words=''
    do i=1,n
       words(i)=bare_token(raw(i))
       call copy_text(words(i),fields%words(:,i))
       if(index(raw(i),'<')>0.or.index(raw(i),'>')>0) then
          if(token_state(raw(i))==call_malformed) fields%malformed=1
       endif
    enddo
    call parse_beacon(mode,payload,raw,words,n,fields,beacon)
    if(beacon) return
    if(n<2) then
       if(n==1.and.(mode==4.or.mode==65)) then
          select case(trim(words(1)))
          case('RO'); fields%exchange_kind=exchange_ro
          case('RRR'); fields%exchange_kind=exchange_rrr
          case('73'); fields%exchange_kind=exchange_signoff
          case('OOO'); fields%exchange_kind=exchange_ooo
          end select
          if(fields%exchange_kind/=exchange_none) then
             fields%structured=1
             fields%complete_tail=1
             fields%exchange_first_word=0
             call copy_text(words(1),fields%exchange)
          endif
       endif
       return
    endif
    first=1
    if(n>=3.and.raw(2)=='RR73;') then
       first=3
       call copy_text(words(1),fields%acknowledged_recipient)
       fields%acknowledged_recipient_state=token_state(raw(1))
    endif
    if(first>=n) return
    if(.not.literal_token(words(first)).and.words(first)/='...') return
    destination=words(first)
    sender=first+1
    if(destination=='CQ'.or.destination=='DE'.or.destination=='QRZ') then
       if(n>=first+2.and.modifier(words(first+1))) then
          destination=trim(destination)//' '//trim(words(first+1))
          sender=first+2
       endif
    endif
    call copy_text(destination,fields%destination)
    fields%destination_state=token_state(raw(first))
    fields%cq=merge(1,0,destination=='CQ'.or.destination=='CQDX'.or.index(destination,'CQ ')==1)
    fields%kind=merge(message_cq,message_directed,fields%cq/=0)
    if(first==3) fields%kind=message_dual
    if(literal_token(words(sender)).or.words(sender)=='...') then
       call copy_text(words(sender),fields%sender)
       fields%sender_state=token_state(raw(sender))
    else
       return
    endif
    fields%exchange_first_word=sender
    ntail=n-sender
    if(ntail==0) then
       fields%exchange_first_word=n
    else
       tail=words(sender+1)
       call copy_text(tail,fields%exchange)
       fields%exchange_kind=exchange_other
       do i=sender+1,n
          if(.not.grid_token(words(i))) cycle
          call copy_text(words(i),fields%grid)
          exit
       enddo
       if(ntail==1) then
          tail=words(n)
          if(grid_token(tail)) fields%exchange_kind=exchange_grid
          select case(trim(tail))
          case('RO'); fields%exchange_kind=exchange_ro
          case('RRR'); fields%exchange_kind=exchange_rrr
          case('RR73'); fields%exchange_kind=exchange_rr73
          case('73'); fields%exchange_kind=exchange_signoff
          case('OOO'); fields%exchange_kind=exchange_ooo
          end select
          report_text=tail
          if(tail(1:1)=='R') report_text=tail(2:)
          call pack77_parse_signed_report(report_text,value,ok)
          if(ok.and.value>=-50.and.value<=49) then
             fields%exchange_kind=merge(exchange_rreport,exchange_report,tail(1:1)=='R')
             fields%report_present=1
             fields%report=value
          endif
       else if(ntail==2) then
          if(words(sender+1)=='R'.and.grid_token(words(n))) fields%exchange_kind=exchange_grid
          if(grid_token(words(sender+1)).and.words(n)=='OOO') fields%exchange_kind=exchange_grid
       endif
    endif
    identities=receive_call(raw(sender))
    if(fields%cq==0) identities=identities.and.(destination=='DE'.or.receive_call(raw(first)))
    if(first==3) identities=identities.and.receive_call(raw(1))
    if(fields%malformed==0) then
       classifier_payload=payload
       if(mode==65.and.ntail>=1) then
          if(words(n)=='OOO'.and.fields%exchange_kind/=exchange_other) then
             i=index(trim(payload),' ',back=.true.)
             if(i>0) classifier_payload=payload(:i-1)
          endif
       endif
       call classify_modern(classifier_payload,structured,contest)
       legacy=mode==4.or.mode==9.or.mode==65
       if(legacy.and.(index(payload,'<')>0.or.len_trim(classifier_payload)>22)) structured=.false.
       fields%structured=merge(1,0,structured)
       if(contest.and.structured) then
          fields%kind=message_contest
          fields%exchange_kind=exchange_contest
          fields%complete_tail=1
       endif
       if(identities) then
          if(fields%exchange_kind/=exchange_other) fields%complete_tail=1
          if(first==3) then
             fields%complete_tail=merge(1,0,n==5.and.fields%exchange_kind==exchange_report)
          endif
       endif
    endif
  end subroutine
end module decoded_message
