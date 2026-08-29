program test_superfox_pack

  implicit none

  integer :: ntests
  integer, parameter :: SFOX_PACK_OK=0
  integer, parameter :: SFOX_PACK_BAD_TOKEN=1
  integer, parameter :: SFOX_PACK_BAD_OTP=2
  integer, parameter :: SFOX_PACK_BAD_CQ=3
  integer, parameter :: SFOX_PACK_BAD_CALL=4
  integer, parameter :: SFOX_PACK_BAD_REPORT=5
  integer, parameter :: SFOX_PACK_BAD_FREE_TEXT=6

  ntests=0

  call expect_cq_round_trip('CQ K1JT FN20','K1JT','FN20')
  call expect_cq_round_trip('CQ PJ4/K1ABC FK52','PJ4/K1ABC','FK52')
  call expect_cq_round_trip('CQ K1JT AA00','K1JT','AA00')
  call expect_cq_round_trip('CQ K1JT RR99','K1JT','RR99')
  call expect_crc_vector()
  call expect_invalid_pack('CQ',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('CQ K1JT',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('CQ K1@JT FN20',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('CQ K1JT FN2@',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('CQ K1JT FN2',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('CQ K1JT FN200',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('CQ K1JT SS00',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('CQ AB FN20',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('CQ 123456789012 FN20',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('CQ k1jt FN20',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('CQ K1JT FN20 EXTRA',SFOX_PACK_BAD_CQ)
  call expect_invalid_pack('   ',SFOX_PACK_BAD_TOKEN)

  call expect_standard_message('K1JT W1AW','K1JT','W1AW')
  call expect_standard_report('K1JT W1AW +05','K1JT','W1AW',5)
  call expect_standard_report('K1JT W1AW +12','K1JT','W1AW',12)
  call expect_standard_report('K1JT W1AW -18','K1JT','W1AW',-18)
  call expect_standard_message('K1JT W1AW AA5AU K9AN','K1JT','W1AW')
  call expect_standard_message('K1JT W1AW AA5AU K9AN','K1JT','AA5AU')
  call expect_standard_message('K1JT W1AW AA5AU K9AN','K1JT','K9AN')
  call expect_invalid_pack('K1JT Q1ABC',SFOX_PACK_BAD_CALL)
  call expect_invalid_pack('K1JT W1AW +13',SFOX_PACK_BAD_REPORT)
  call expect_invalid_pack('K1JT W1AW -19',SFOX_PACK_BAD_REPORT)
  call expect_invalid_pack('K1JT W1AW +XX',SFOX_PACK_BAD_REPORT)
  call expect_invalid_pack('K1JT VERYLONGTOKEN1',SFOX_PACK_BAD_TOKEN)
  call expect_too_many_words()

  call expect_free_text_message('K1JT W1AW','HELLO SUPERFOX')
  call expect_free_text_message('K1JT W1AW','ABC+-./? 123')
  call expect_free_text_four_report_records()
  call expect_free_text_four_mixed_records()
  call expect_invalid_free_text_capacity()
  call expect_no_free_text_full_capacity()
  call expect_invalid_no_free_text_capacity()
  call expect_invalid_free_text('K1JT W1AW','hello superfox')
  call expect_invalid_free_text('K1JT W1AW','HELLO:SUPERFOX')
  call expect_invalid_otp('K1JT W1AW')

  call expect_more_cqs('K1JT')
  call expect_otp_bits('K1JT W1AW',123456)

  write(*,1000) ntests
1000 format('superfox pack tests passed: ',i0)

contains

  subroutine expect_crc_vector()
    character(len=120) :: line
    character(len=26) :: free_text
    character(len=10) :: ckey
    integer(kind=1) :: xin(0:49), raw(0:49)
    integer :: crc21, i, pack_error
    logical(kind=1) :: more_cqs, send_msg

    line='CQ K1JT FN20'
    free_text=' '
    ckey='0000000000'
    more_cqs=.false.
    send_msg=.false.
    call sfox_pack(line,ckey,more_cqs,send_msg,free_text,xin,pack_error)
    call assert_int('SuperFox CRC pack status',SFOX_PACK_OK,pack_error)
    do i=0,49
       raw(i)=xin(49-i)
    enddo
    crc21=128*128*raw(47)+128*raw(48)+raw(49)
    call assert_int('SuperFox CRC21',1361017,crc21)
    ntests=ntests+1
  end subroutine expect_crc_vector

  subroutine expect_standard_message(input,expected_fox,expected_hound)
    character(len=*), intent(in) :: input, expected_fox, expected_hound
    character(len=329) :: msgbits
    character(len=13) :: got_fox
    integer :: i3

    call pack_and_decode_bits(input,.false.,.false.,' ',0,msgbits,i3)
    call assert_int('SuperFox standard type',0,i3)
    call decode_pack28_field(msgbits,1,got_fox)
    call assert_text('SuperFox fox call',expected_fox,got_fox)
    call assert_hound_present(msgbits,expected_hound)

    ntests=ntests+1
  end subroutine expect_standard_message

  subroutine expect_standard_report(input,expected_fox,expected_hound, &
       expected_report)
    character(len=*), intent(in) :: input, expected_fox, expected_hound
    integer, intent(in) :: expected_report
    character(len=329) :: msgbits
    character(len=13) :: got_fox, got_hound
    integer :: i3, report

    call pack_and_decode_bits(input,.false.,.false.,' ',0,msgbits,i3)
    call assert_int('SuperFox report type',0,i3)
    call decode_pack28_field(msgbits,1,got_fox)
    call assert_text('SuperFox report fox call',expected_fox,got_fox)
    call decode_pack28_field(msgbits,169,got_hound)
    call assert_text('SuperFox report hound call',expected_hound,got_hound)
    read(msgbits(281:285),'(b5)') report
    call assert_int('SuperFox report',expected_report,report-18)

    ntests=ntests+1
  end subroutine expect_standard_report

  subroutine expect_free_text_message(input,free_text)
    use packjt77, only: unpacktext77
    character(len=*), intent(in) :: input, free_text
    character(len=329) :: msgbits
    character(len=26) :: decoded
    integer :: i3

    call pack_and_decode_bits(input,.false.,.true.,free_text,0,msgbits,i3)
    call assert_int('SuperFox free-text type',2,i3)
    call unpacktext77(msgbits(161:231),decoded(1:13))
    call unpacktext77(msgbits(232:302),decoded(14:26))
    call trim_dots(decoded)
    call assert_text('SuperFox free text',free_text,decoded)

    ntests=ntests+1
  end subroutine expect_free_text_message

  subroutine expect_invalid_free_text(input,free_text)
    character(len=*), intent(in) :: input, free_text
    character(len=329) :: msgbits
    integer :: i3

    call pack_and_decode_bits_allow_failure(input,.false.,.true.,free_text, &
         0,msgbits,i3,SFOX_PACK_BAD_FREE_TEXT)
    ntests=ntests+1
  end subroutine expect_invalid_free_text

  subroutine expect_free_text_four_report_records()
    character(len=329) :: msgbits
    integer :: i3

    call pack_and_decode_bits('K1JT K1RAA -01 K1RAB -02 K1RAC -03 K1RAD -04', &
         .false.,.true.,'HELLO SUPERFOX',0,msgbits,i3)
    call assert_int('SuperFox free-text four reports type',2,i3)
    call assert_hound_present(msgbits,'K1RAA')
    call assert_hound_present(msgbits,'K1RAB')
    call assert_hound_present(msgbits,'K1RAC')
    call assert_hound_present(msgbits,'K1RAD')

    ntests=ntests+1
  end subroutine expect_free_text_four_report_records

  subroutine expect_free_text_four_mixed_records()
    character(len=329) :: msgbits
    integer :: i3

    call pack_and_decode_bits('K1JT K1RRA K1RRB K1RPC -03 K1RPD -04', &
         .false.,.true.,'HELLO SUPERFOX',0,msgbits,i3)
    call assert_int('SuperFox free-text mixed type',2,i3)
    call assert_hound_present(msgbits,'K1RRA')
    call assert_hound_present(msgbits,'K1RRB')
    call assert_hound_present(msgbits,'K1RPC')
    call assert_hound_present(msgbits,'K1RPD')

    ntests=ntests+1
  end subroutine expect_free_text_four_mixed_records

  subroutine expect_invalid_free_text_capacity()
    call expect_invalid_pack_with_free_text( &
         'K1JT K1RAA -01 K1RAB -02 K1RAC -03 K1RAD -04 K1RAE -05', &
         SFOX_PACK_BAD_TOKEN)
  end subroutine expect_invalid_free_text_capacity

  subroutine expect_no_free_text_full_capacity()
    character(len=329) :: msgbits
    integer :: i3

    call pack_and_decode_bits( &
         'K1JT K1AAA K1AAB K1AAC K1AAD K1AAE K1AAF -01 K1AAG -02 K1AAH -03 K1AAI -04', &
         .false.,.false.,' ',0,msgbits,i3)
    call assert_int('SuperFox no-free-text full capacity type',0,i3)
    call assert_hound_present(msgbits,'K1AAA')
    call assert_hound_present(msgbits,'K1AAE')
    call assert_hound_present(msgbits,'K1AAF')
    call assert_hound_present(msgbits,'K1AAI')

    ntests=ntests+1
  end subroutine expect_no_free_text_full_capacity

  subroutine expect_invalid_no_free_text_capacity()
    call expect_invalid_pack( &
         'K1JT K1AAA K1AAB K1AAC K1AAD K1AAE K1AAF', &
         SFOX_PACK_BAD_TOKEN)
  end subroutine expect_invalid_no_free_text_capacity

  subroutine expect_more_cqs(input)
    character(len=*), intent(in) :: input
    character(len=329) :: msgbits
    integer :: i3

    call pack_and_decode_bits(input,.true.,.false.,' ',0,msgbits,i3)
    if(msgbits(306:306).ne.'1') then
       write(*,1040) trim(input)
1040   format('SuperFox more-CQs bit was not set for "',a,'"')
       error stop 1
    endif

    ntests=ntests+1
  end subroutine expect_more_cqs

  subroutine expect_otp_bits(input,expected_otp)
    character(len=*), intent(in) :: input
    integer, intent(in) :: expected_otp
    character(len=329) :: msgbits
    integer :: i3, got_otp

    call pack_and_decode_bits(input,.false.,.false.,' ',expected_otp,msgbits,i3)
    read(msgbits(307:326),'(b20)') got_otp
    call assert_int('SuperFox OTP bits',expected_otp,got_otp)

    ntests=ntests+1
  end subroutine expect_otp_bits

  subroutine expect_cq_round_trip(input,expected_call,expected_grid)
    character(len=*), intent(in) :: input, expected_call, expected_grid
    character(len=11) :: got_call
    character(len=4) :: got_grid
    integer :: i3

    call pack_and_decode_cq(input,i3,got_call,got_grid)
    call assert_int('SuperFox CQ type',3,i3)
    call assert_text('SuperFox CQ call',expected_call,got_call)
    call assert_text('SuperFox CQ grid',expected_grid,got_grid)

    ntests=ntests+1
  end subroutine expect_cq_round_trip

  subroutine expect_invalid_pack(input,expected_error)
    character(len=*), intent(in) :: input
    integer, intent(in) :: expected_error
    character(len=329) :: msgbits
    integer :: i3

    call pack_and_decode_bits_allow_failure(input,.false.,.false.,' ',0, &
         msgbits,i3,expected_error)
    ntests=ntests+1
  end subroutine expect_invalid_pack

  subroutine expect_invalid_pack_with_free_text(input,expected_error)
    character(len=*), intent(in) :: input
    integer, intent(in) :: expected_error
    character(len=329) :: msgbits
    integer :: i3

    call pack_and_decode_bits_allow_failure(input,.false.,.true., &
         'HELLO SUPERFOX',0,msgbits,i3,expected_error)
    ntests=ntests+1
  end subroutine expect_invalid_pack_with_free_text

  subroutine expect_too_many_words()
    call expect_invalid_pack( &
         'K1JT K1AA K1AB K1AC K1AD K1AE K1AF K1AG K1AH K1AI ' // &
         'K1AJ K1AK K1AL K1AM K1AN K1AO K1AP', &
         SFOX_PACK_BAD_TOKEN)
  end subroutine expect_too_many_words

  subroutine expect_invalid_otp(input)
    character(len=*), intent(in) :: input
    character(len=329) :: msgbits
    integer :: i3

    call pack_and_decode_bits_with_ckey(input,.false.,.false.,' ', &
         'OTP:BROKEN',msgbits,i3,SFOX_PACK_BAD_OTP)
    ntests=ntests+1
  end subroutine expect_invalid_otp

  subroutine pack_and_decode_cq(input,i3,call_out,grid_out)
    character(len=*), intent(in) :: input
    integer, intent(out) :: i3
    character(len=11), intent(out) :: call_out
    character(len=4), intent(out) :: grid_out
    character(len=329) :: msgbits
    character(len=38) :: c
    integer(kind=8) :: n58
    integer :: i, j, n15

    c=' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/'
    call pack_and_decode_bits(input,.false.,.false.,' ',0,msgbits,i3)

    call_out=' '
    grid_out=' '
    if(i3.ne.3) return

    read(msgbits(1:58),'(b58)') n58
    do i=11,1,-1
       j=mod(n58,38_8)+1
       call_out(i:i)=c(j:j)
       n58=n58/38_8
    enddo
    call_out=adjustl(call_out)
    read(msgbits(59:73),'(b15)') n15
    call unpackgrid(n15,grid_out)
  end subroutine pack_and_decode_cq

  subroutine pack_and_decode_bits(input,more_cqs,send_msg,free_text,otp, &
       msgbits,i3)
    character(len=*), intent(in) :: input, free_text
    logical, intent(in) :: more_cqs, send_msg
    integer, intent(in) :: otp
    character(len=329), intent(out) :: msgbits
    integer, intent(out) :: i3

    call pack_and_decode_bits_allow_failure(input,more_cqs,send_msg, &
         free_text,otp,msgbits,i3,SFOX_PACK_OK)
  end subroutine pack_and_decode_bits

  subroutine pack_and_decode_bits_allow_failure(input,more_cqs,send_msg, &
       free_text,otp,msgbits,i3,expected_error)
    character(len=*), intent(in) :: input, free_text
    logical, intent(in) :: more_cqs, send_msg
    integer, intent(in) :: otp
    integer, intent(in) :: expected_error
    character(len=329), intent(out) :: msgbits
    integer, intent(out) :: i3
    character(len=10) :: ckey

    write(ckey,1000) otp
1000 format('0000',i6.6)
    call pack_and_decode_bits_with_ckey(input,more_cqs,send_msg,free_text, &
         ckey,msgbits,i3,expected_error)
  end subroutine pack_and_decode_bits_allow_failure

  subroutine pack_and_decode_bits_with_ckey(input,more_cqs,send_msg, &
       free_text,ckey,msgbits,i3,expected_error)
    character(len=*), intent(in) :: input, free_text, ckey
    logical, intent(in) :: more_cqs, send_msg
    integer, intent(in) :: expected_error
    character(len=329), intent(out) :: msgbits
    integer, intent(out) :: i3
    character(len=120) :: line
    character(len=26) :: free_text_26
    integer(kind=1) :: xin(0:49), raw(0:49)
    logical(kind=1) :: more_cqs_1, send_msg_1
    integer :: i, pack_error

    line=' '
    line=input
    free_text_26=' '
    free_text_26=free_text
    more_cqs_1=more_cqs
    send_msg_1=send_msg
    xin=0
    raw=0

    call sfox_pack(line,ckey,more_cqs_1,send_msg_1,free_text_26,xin, &
         pack_error)
    if(pack_error.ne.expected_error) then
       write(*,1010) trim(input), expected_error, pack_error
1010   format('SuperFox pack status mismatch for "',a,'"; expected ',i0, &
           ' got ',i0)
       error stop 1
    endif
    if(expected_error.ne.SFOX_PACK_OK) then
       msgbits='0'
       i3=-1
       return
    endif

    do i=0,49
       raw(i)=xin(49-i)
    enddo
    write(msgbits,1020) raw(0:46)
1020 format(47b7.7)
    read(msgbits(327:329),'(b3)') i3
  end subroutine pack_and_decode_bits_with_ckey

  subroutine decode_pack28_field(msgbits,first_bit,call_out)
    use packjt77, only: unpack28
    character(len=*), intent(in) :: msgbits
    integer, intent(in) :: first_bit
    character(len=13), intent(out) :: call_out
    logical :: success
    integer :: n28

    call_out=' '
    read(msgbits(first_bit:first_bit+27),'(b28)') n28
    call unpack28(n28,call_out,success)
    if(.not.success) then
       write(*,1050) first_bit, n28
1050   format('SuperFox pack28 decode failed at bit ',i0,' n28=',i0)
       error stop 1
    endif
  end subroutine decode_pack28_field

  subroutine assert_hound_present(msgbits,expected_hound)
    character(len=*), intent(in) :: msgbits, expected_hound
    character(len=13) :: got_hound
    integer :: i, first_bit

    do i=1,9
       first_bit=28*i + 1
       call decode_pack28_field(msgbits,first_bit,got_hound)
       if(trim(got_hound).eq.expected_hound) return
    enddo

    write(*,1060) trim(expected_hound)
1060 format('SuperFox hound call "',a,'" was not present')
    error stop 1
  end subroutine assert_hound_present

  subroutine trim_dots(text)
    character(len=*), intent(inout) :: text
    integer :: i

    do i=len(text),1,-1
       if(text(i:i).ne.'.') exit
       text(i:i)=' '
    enddo
  end subroutine trim_dots

  subroutine assert_int(label,expected,got)
    character(len=*), intent(in) :: label
    integer, intent(in) :: expected, got

    if(got.ne.expected) then
       write(*,1070) trim(label), expected, got
1070   format(a,' failure; expected ',i0,' got ',i0)
       error stop 1
    endif
  end subroutine assert_int

  subroutine assert_text(label,expected,got)
    character(len=*), intent(in) :: label, expected, got

    if(trim(got).ne.expected) then
       write(*,1080) trim(label), trim(expected), trim(got)
1080   format(a,' failure; expected "',a,'" got "',a,'"')
       error stop 1
    endif
  end subroutine assert_text

end program test_superfox_pack
