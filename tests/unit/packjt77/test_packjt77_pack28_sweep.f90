program test_packjt77_pack28_sweep

  use packjt77
  use packjt77_grammar, only: PACK77_NTOKENS,PACK77_MAX22
  implicit none

  integer, parameter :: ncat=20
  integer, parameter :: nsweep=260
  integer, parameter :: expected_n28(nsweep) = (/ &
       5950126, 5950126, 5121162, 5121162, 3532835, 5950126, 5950126, &
       5950126, 2063592, 5950126, 4386540, 3225066, 3225066, 3959688, &
       5121162, 5002078, 5002078, 3532835, 5950126, 4907736, 4396757, &
       4636842, 3214849, 5375614, 6173770, 4396757, 4396757, 4396757, &
       2063592, 4396757, 4993936, 2, 2, 0, 3454934, &
       4168964, 4776360, 4253090, 4636842, 5755217, 8246608, 86387389, &
       11395888, 204642853, 22417639, 84419089, 8246608, 8246608, 2063592, &
       8246608, 194269912, 2195446, 2195446, 2111640, 1, 2913946, &
       3521341, 4940621, 86387389, 10214908, 8246635, 86388118, 11395915, &
       204661807, 22418368, 84419818, 4873537, 5750231, 2063592, 5750231, &
       6256420, 5239844, 1023, 4175765, 3318227, 2338993, 5010514, &
       2065856, 86388118, 10214962, 8246636, 86388145, 11395916, 204662509, &
       22418395, 84419872, 5574985, 2257374, 2063592, 2257374, 3991096, &
       5415206, 1548, 6216860, 5359321, 4380087, 4372313, 2416579, &
       86388172, 10214965, 6022918, 86388146, 5193953, 204662535, 22418396, &
       84419875, 3165162, 4041856, 2363507, 4041856, 5686611, 126, &
       15737, 2959280, 2101742, 6164569, 3835940, 4156578, 86388175, &
       4447486, 2074017, 3976499, 5439357, 4585084, 5513427, 2444633, &
       3432875, 4309569, 2063592, 4309569, 5932014, 4857335, 398841, &
       3204683, 2347145, 2260287, 4594460, 4335053, 4845483, 5206005, &
       2080475, 3982957, 5445815, 4591542, 5519885, 2464594, 3439920, &
       4316614, 2063592, 4316614, 5951388, 4863793, 2695765, 3211141, &
       2353603, 2280248, 4614421, 4340337, 4865444, 5225967, 2080645, &
       3983127, 5445985, 4591712, 5520055, 2465120, 3440106, 4316799, &
       2063592, 4316799, 5951558, 4863963, 2695935, 3211311, 2353773, &
       2280774, 4614946, 4340491, 4865970, 5226492, 2080650, 3983132, &
       5445989, 4591716, 5520059, 2465134, 3440111, 4316804, 2063592, &
       4316804, 5951563, 4863968, 2695939, 3211316, 2353778, 2280788, &
       4614960, 4340492, 4865984, 5226506, 2080650, 3983132, 5445989, &
       4591716, 5520059, 2465134, 3440111, 4316804, 2063592, 4316804, &
       5951563, 4863968, 2695940, 3211316, 2353778, 2280788, 4614960, &
       4340492, 4865984, 5226506, 2080650, 3983132, 5445989, 4591716, &
       5520059, 2465134, 3440111, 4316804, 2063592, 4316804, 5951563, &
       4863968, 2695940, 3211316, 2353778, 2280788, 4614960, 4340492, &
       4865984, 5226506, 2080650, 3983132, 5445989, 4591716, 5520059, &
       2465134, 3440111, 4316804, 2063592, 4316804, 5951563, 4863968, &
       2695940, 3211316, 2353778, 2280788, 4614960, 4340492, 4865984, &
       5226506 /)
  integer :: i,n28
  character(len=13) :: decoded, token
  logical :: ok

  call check_cq_modifier_edges()

  do i=1,nsweep
     token=sweep_token(i)
     call pack28(token,n28)
     if(n28.ne.expected_n28(i)) then
        write(*,1000) i, trim(token), expected_n28(i), n28
1000    format('pack28 sweep case ',i0,' "',a,'" expected ',i0,' got ',i0)
        error stop 1
     endif
     decoded='             '
     ok=.false.
     call unpack28(n28,decoded,ok)
  enddo

  print '(a,i0,a)', 'test_packjt77_pack28_sweep: ', nsweep, ' checks passed'

contains

  character(len=13) function sweep_token(i) result(token)
    integer, intent(in) :: i
    integer :: category,n

    category=mod(i-1,ncat)+1
    n=(i-1)/ncat+1
    token='             '
    select case(category)
    case(1)
       if(n.lt.3) then
          token=take_prefix('AAAAAAAAAAAAA',n)
       else
          token=take_prefix('A1AAAAAAAAAAA',n)
       endif
    case(2)
       token=take_prefix('AB1AAAAAAAAAA',n)
    case(3)
       if(n.lt.3) then
          token=take_prefix('QQQQQQQQQQQQQ',n)
       else
          token=take_prefix('Q1AAAAAAAAAAA',n)
       endif
    case(4)
       token=take_prefix('QZ9ZZZAAAAAAA',n)
    case(5)
       token=take_prefix('1A1AAAAAAAAAA',n)
    case(6)
       if(n.lt.3) then
          token=take_prefix('AAAAAAAAAAAAA',n)
       else
          token=take_prefix('A11ABCXXXXXXX',n)
       endif
    case(7)
       if(n.lt.3) then
          token=take_prefix('AAAAAAAAAAAAA',n)
       else
          token=take_prefix('A1A/BBBBBBBBB',n)
       endif
    case(8)
       if(n.lt.3) then
          token=take_prefix('AAAAAAAAAAAAA',n)
       else
          token=take_prefix('A1A$BBBBBBBBB',n)
       endif
    case(9)
       token=take_prefix('<W1AW>AAAAAAA',n)
    case(10)
       if(n.lt.3) then
          token=take_prefix('AAAAAAAAAAAAA',n)
       else
          token=take_prefix('A1A>BBBBBBBBB',n)
       endif
    case(11)
       token=take_prefix('PJ2/W1AWAAAAA',n)
    case(12)
       token=take_prefix('CQ_123AAAAAAA',n)
    case(13)
       token=take_prefix('CQ_TESTAAAAAA',n)
    case(14)
       token=take_prefix('DEAAAAAAAAAAA',n)
    case(15)
       token=take_prefix('QRZAAAAAAAAAA',n)
    case(16)
       token=take_prefix('3DA0ABCXXXXXX',n)
    case(17)
       token=take_prefix('3XABCXXXXXXXX',n)
    case(18)
       token=take_prefix('1234567890123',n)
    case(19)
       token=take_prefix('AB1ABCXXXXXXX',n)
    case(20)
       if(n.lt.3) then
          token=take_prefix('KKKKKKKKKKKKK',n)
       else
          token=take_prefix('K1ABCXXXXXXXX',n)
       endif
    end select
  end function sweep_token

  character(len=13) function take_prefix(seed,n) result(token)
    character(len=*), intent(in) :: seed
    integer, intent(in) :: n

    token='             '
    token(1:n)=seed(1:n)
  end function take_prefix

  subroutine check_cq_modifier_edges()
    call assert_cq_modifier_special('CQ_A')
    call assert_cq_modifier_special('CQ_AB')
    call assert_cq_modifier_special('CQ_ABC')
    call assert_cq_modifier_special('CQ_ABCD')
    call assert_cq_modifier_special('CQ_000')
    call assert_cq_modifier_special('CQ_123')
    call assert_cq_modifier_special('CQ_999')

    call assert_cq_modifier_hash('CQ_')
    call assert_cq_modifier_hash('CQ_1')
    call assert_cq_modifier_hash('CQ_12')
    call assert_cq_modifier_hash('CQ_1234')
    call assert_cq_modifier_hash('CQ_ABCDE')
    call assert_cq_modifier_hash('CQ_A1')
    call assert_cq_modifier_hash('CQ_1A')
    call assert_cq_modifier_hash('CQ_AB12')
    call assert_cq_modifier_hash('CQ_A/B')
    call assert_cq_modifier_hash('CQ_A-B')
  end subroutine check_cq_modifier_edges

  subroutine assert_cq_modifier_special(input)
    character(len=*), intent(in) :: input
    character(len=13) :: c13
    integer :: got

    call clear_hash_state()
    c13='             '
    c13=input
    call pack28(c13,got)
    if(got.lt.3 .or. got.ge.PACK77_NTOKENS) then
       write(*,1010) trim(input), got
1010   format('CQ modifier "',a,'" expected special token, got ',i0)
       error stop 1
    endif
    if(nzhash.ne.0) then
       write(*,1011) trim(input), nzhash
1011   format('CQ modifier "',a,'" unexpectedly staged ',i0,' hashes')
       error stop 1
    endif
  end subroutine assert_cq_modifier_special

  subroutine assert_cq_modifier_hash(input)
    character(len=*), intent(in) :: input
    character(len=13) :: c13
    integer :: got

    call clear_hash_state()
    c13='             '
    c13=input
    call pack28(c13,got)
    if(got.lt.PACK77_NTOKENS .or. got.ge.PACK77_NTOKENS+PACK77_MAX22) then
       write(*,1020) trim(input), got
1020   format('CQ modifier "',a,'" expected hash token, got ',i0)
       error stop 1
    endif
    if(nzhash.ne.1 .or. calls22(1).ne.c13) then
       write(*,1021) trim(input), nzhash, trim(calls22(1))
1021   format('CQ modifier "',a,'" expected one committed hash, got nzhash=', &
             i0,' calls22(1)="',a,'"')
       error stop 1
    endif
  end subroutine assert_cq_modifier_hash

  subroutine clear_hash_state()
    calls10=''
    calls12=''
    calls22=''
    recent_calls=''
    ihash22=-1
    nzhash=0
  end subroutine clear_hash_state

end program test_packjt77_pack28_sweep
