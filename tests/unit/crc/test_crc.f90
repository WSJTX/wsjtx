program test_crc
  use, intrinsic :: iso_fortran_env, only: int8,int32
  use crc, only: crc10,crc13,crc14
  use ft8_codec_context, only: ft8_tones_from_77bits
  use ft4_codec, only: ft4_tones_from_77bits,ft4_scramble
  implicit none

  ! Golden CRCs and codewords come from Boost and the encoders at
  ! c1549a03ce89a429d2be7a1a8043cef68da8e1d1. Short bit strings were
  ! padded on the left for the byte-only Boost interface.
  integer, parameter :: widths(3)=[10,13,14]
  integer(int32), parameter :: basis_expected(96,3)=reshape([ &
    971, 930, 465, 687, 784, 392, 196, 98, &
    49, 607, 872, 436, 218, 109, 625, 895, &
    1016, 508, 254, 127, 632, 316, 158, 79, &
    608, 304, 152, 76, 38, 19, 590, 295, &
    724, 362, 181, 541, 841, 995, 950, 475, &
    682, 341, 749, 817, 991, 936, 468, 234, &
    117, 637, 889, 1019, 954, 477, 681, 787, &
    974, 487, 692, 346, 173, 529, 847, 992, &
    496, 248, 124, 62, 31, 584, 292, 146, &
    73, 611, 886, 443, 666, 333, 737, 823, &
    988, 494, 247, 572, 286, 143, 512, 256, &
    128, 64, 32, 16, 8, 4, 2, 1, &
    4822, 2411, 7774, 3887, 7548, 3774, 1887, 6468, &
    3234, 1617, 6595, 5642, 2821, 8041, 5471, 4164, &
    2082, 1041, 6371, 5786, 2893, 8013, 5453, 4173, &
    4813, 5005, 4909, 4989, 4949, 4929, 4939, 4942, &
    2471, 7736, 3868, 1934, 967, 6920, 3460, 1730, &
    865, 7003, 5958, 2979, 7994, 3997, 7461, 5241, &
    4311, 4736, 2368, 1184, 592, 296, 148, 74, &
    37, 6905, 6039, 4384, 2192, 1096, 548, 274, &
    137, 6831, 6076, 3038, 1519, 6172, 3086, 1543, &
    6632, 3316, 1658, 829, 7029, 5969, 4419, 4682, &
    2341, 7801, 5591, 4096, 2048, 1024, 512, 256, &
    128, 64, 32, 16, 8, 4, 2, 1, &
    11256, 5628, 2814, 1407, 12564, 6282, 3141, 13705, &
    10607, 10012, 5006, 2503, 14152, 7076, 3538, 1769, &
    12511, 11204, 5602, 2801, 14035, 10434, 5217, 14747, &
    12134, 6067, 14450, 7225, 15799, 11632, 5816, 2908, &
    1454, 727, 12992, 6496, 3248, 1624, 812, 406, &
    203, 13262, 6631, 16216, 8108, 4054, 2027, 12382, &
    6191, 16316, 8158, 4079, 13404, 6702, 3351, 13600, &
    6800, 3400, 1700, 850, 425, 13183, 10772, 5386, &
    2693, 14057, 10463, 10180, 5090, 2545, 14163, 10242, &
    5121, 14763, 12158, 6079, 14452, 7226, 3613, 13477, &
    10745, 10071, 8192, 4096, 2048, 1024, 512, 256, &
    128, 64, 32, 16, 8, 4, 2, 1 &
    ],[96,3])
  integer(int32), parameter :: pattern_expected(4,3)=reshape([ &
    0, 264, 893, 629, 0, 7323, 2953, 5906, &
    0, 4194, 4062, 8124 &
    ],[4,3])
  integer, parameter :: lengths(*)=[ &
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, &
    20, 21, 22, 23, 24, 77, 90, 91, 95, 96, 97, 103, 104, 127, 128, 129 ]
  integer(int32), parameter :: tail_expected(36,3)=reshape([ &
    0, 0, 0, 0, 0, 1, 2, 5, &
    10, 21, 42, 85, 170, 341, 682, 474, &
    948, 999, 833, 525, 148, 296, 593, 45, &
    91, 760, 3, 6, 97, 194, 389, 76, &
    152, 669, 437, 875, 0, 0, 0, 0, &
    0, 1, 2, 5, 10, 21, 42, 85, &
    170, 341, 682, 1365, 2730, 5460, 8063, 2857, &
    5715, 6513, 1844, 3688, 7377, 785, 2895, 5790, &
    4101, 5597, 7788, 1640, 3280, 1360, 2720, 5441, &
    0, 0, 0, 0, 0, 1, 2, 5, &
    10, 21, 42, 85, 170, 341, 682, 1365, &
    2730, 5460, 10920, 12807, 856, 1712, 3425, 6850, &
    13701, 14096, 14142, 2347, 15176, 4551, 9103, 5391, &
    10782, 5295, 10590, 13802 &
    ],[36,3])
  character(len=77), parameter :: payloads(3)=[ &
    '00001001101111011110001101010000011000010100100111011100000010100001100110001', &
    '11111111111111111111111111111111111111111111111111111111111111111111111111111', &
    '01010101010101010101010101010101010101010101010101010101010101010101010101010' &
    ]
  character(len=174), parameter :: ft8_codewords(3)=[ &
    '0000100110111101111000110101000001100001010010011101110000001010000110011000100010101' // &
    '1010001000110101010100111000000101001010001011111110100111001100011001010100100000010' // &
    '0001', &
    '1111111111111111111111111111111111111111111111111111111111111111111111111111100011110' // &
    '1100011111110110101101011110010111011000100111001110110011111100110001001111100111101' // &
    '0101', &
    '0101010101010101010101010101010101010101010101010101010101010101010101010101001010101' // &
    '0000111000011111001100101000001000101110100000101110100101000101011100011011110001001' // &
    '0000' &
    ]
  character(len=128), parameter :: msk_codewords(3)=[ &
    '0000100110111101111000110101000001100001010010011101110000001010000110011000110000001' // &
    '1010100101001011110101011101111110111001011', &
    '1111111111111111111111111111111111111111111111111111111111111111111111111111110101101' // &
    '1011101110001010011001101110100111111101001', &
    '0101010101010101010101010101010101010101010101010101010101010101010101010101000101101' // &
    '1111011110001011000111010000000110011101100' &
    ]
  character(len=174), parameter :: ft4_codewords(3)=[ &
    '0100001111100011011010101110010011010001110000111010010101011111101001111010011010000' // &
    '1000101100101101101101100110011110011001010100001011111100001110100010110001010011110' // &
    '1111', &
    '1011010110100001011101100100101101001111011101011000011010101010010000011101011011011' // &
    '1110111011101110010100000000001100001011111000111011101000111110001010101010010100001' // &
    '1011', &
    '0001111100001011110111001110000111100101110111110010110000000000111010110111110010000' // &
    '0010011100000111110101110110010011111101111111011011111110000111100111111110000010101' // &
    '1110' &
    ]
  character(len=79), parameter :: ft8_tones(3)=[ &
    '3140652032247523504061147006021531343140652531636140133537761420436100513140652', &
    '3140652777777777777777777777777741723140652077662375644114265771532717363140652', &
    '3140652363636363636363636363636356353140652140742130312601261313704753303140652' &
    ]
  character(len=103), parameter :: ft4_tones(3)=[ &
    '0132100223021333231021012002331111023122331233133010113113213202022310202033301122301' // &
    '210113033123223201', &
    '0132321133011213103210221211301331023333100121132122121212033000012310301122012121012' // &
    '201111103301323201', &
    '0132012200322120230123112122032001023000233212203001023002233232032310122322232122200' // &
    '220222200111233201' &
    ]
  character(len=144), parameter :: msk_tones(1)=[ &
    '1100001101001111100100110111000010100101111101101000111011000010001100010100101101111' // &
    '11111000001010000101000101110110110101001100101001100001000' &
    ]
  integer :: nfail,ntests

  nfail=0
  ntests=0
  call check_vectors()
  call check_codecs()
  if(nfail/=0) then
    print '(a,i0)', 'CRC checks failed: ',nfail
    error stop 1
  endif
  print '(a,i0)', 'CRC checks passed: ',ntests

contains

  function remainder(width,bits) result(value)
    integer, intent(in) :: width
    integer(int8), intent(in) :: bits(:)
    integer(int32) :: value
    select case(width)
    case(10)
      value=crc10(bits)
    case(13)
      value=crc13(bits)
    case(14)
      value=crc14(bits)
    case default
      error stop 'Unsupported CRC test width'
    end select
  end function

  subroutine expect_equal(actual,expected,label)
    integer, intent(in) :: actual,expected
    character(len=*), intent(in) :: label
    ntests=ntests+1
    if(actual==expected) return
    nfail=nfail+1
    print '(a,a,a,i0,a,i0)',trim(label),': ','got ',actual,', expected ',expected
  end subroutine

  subroutine expect_bits(actual,expected,label)
    integer(int8), intent(in) :: actual(:)
    character(len=*), intent(in) :: expected,label
    integer :: i
    ntests=ntests+1
    do i=1,size(actual)
      if(actual(i)==iachar(expected(i:i))-iachar('0')) cycle
      nfail=nfail+1
      print '(a,a,i0)',trim(label),': first differing bit ',i
      return
    enddo
  end subroutine

  subroutine expect_tones(actual,expected,label)
    integer, intent(in) :: actual(:)
    character(len=*), intent(in) :: expected,label
    integer :: i
    ntests=ntests+1
    do i=1,size(actual)
      if(actual(i)==iachar(expected(i:i))-iachar('0')) cycle
      nfail=nfail+1
      print '(a,a,i0)',trim(label),': first differing tone ',i
      return
    enddo
  end subroutine

  subroutine expect_remainder(bits,expected,label)
    integer(int8), intent(in) :: bits(:)
    integer(int32), intent(in) :: expected(:)
    character(len=*), intent(in) :: label
    character(len=100) :: context
    integer :: w,legacy
    do w=1,size(widths)
      write(context,'(a,i0,2a)') 'CRC',widths(w),' ',label
      call expect_equal(remainder(widths(w),bits),expected(w),context)
    enddo
    if(size(bits)>=15) then
      call get_crc14(bits,size(bits),legacy)
      call expect_equal(legacy,expected(3),'get_crc14 '//label)
    endif
  end subroutine

  subroutine check_vectors()
    integer(int8) :: bits(129),strided(192)
    integer(int32) :: expected(3)
    integer :: i,j,value,w
    character(len=60) :: label
    do i=1,96
      bits=0
      bits(i)=1
      write(label,'(a,i0)') 'single bit at position ',i
      call expect_remainder(bits(:96),basis_expected(i,:),label)
    enddo
    ! Zero-initialized CRCs are linear over XOR.
    do value=0,255
      bits=0
      expected=0
      do i=1,8
        if(.not.btest(value,8-i)) cycle
        bits(i)=1
        expected=ieor(expected,basis_expected(i,:))
      enddo
      write(label,'(a,i0)') 'leading byte ',value
      call expect_remainder(bits(:96),expected,label)
    enddo
    bits=0
    call expect_remainder(bits(:96),pattern_expected(1,:),'all zeros')
    bits=1
    call expect_remainder(bits(:96),pattern_expected(2,:),'all ones')
    do i=1,96
      bits(i)=int(mod(i+1,2),int8)
    enddo
    call expect_remainder(bits(:96),pattern_expected(3,:),'alternating 01')
    strided=1
    strided(1:192:2)=bits(:96)
    do w=1,size(widths)
      write(label,'(a,i0,a)') 'CRC',widths(w),' strided alternating 01'
      call expect_equal(remainder(widths(w),strided(1:192:2)),pattern_expected(3,w),label)
    enddo
    bits(:96)=1_int8-bits(:96)
    call expect_remainder(bits(:96),pattern_expected(4,:),'alternating 10')
    do i=1,size(bits)
      bits(i)=merge(1_int8,0_int8,mod(i*17+i/3,5)<2)
    enddo
    do j=1,size(lengths)
      write(label,'(a,i0)') 'input length ',lengths(j)
      call expect_remainder(bits(:lengths(j)),tail_expected(j,:),label)
    enddo
  end subroutine

  subroutine check_augmented(payload)
    integer(int8), intent(in) :: payload(77)
    integer(int8) :: augmented(96),changed(96)
    integer(int32) :: value
    integer :: w,i,bit
    character(len=80) :: label
    do w=1,size(widths)
      augmented=0
      augmented(:77)=payload
      value=remainder(widths(w),augmented)
      do bit=1,widths(w)
        augmented(96-widths(w)+bit)=merge(1_int8,0_int8,btest(value,widths(w)-bit))
      enddo
      write(label,'(a,i0,a)') 'CRC',widths(w),' augmented codeword'
      call expect_equal(remainder(widths(w),augmented),0,label)
      do i=1,96
        changed=augmented
        changed(i)=1_int8-changed(i)
        write(label,'(a,i0,a,i0)') 'CRC',widths(w),' detects flipped augmented bit ',i
        call expect_equal(merge(1,0,remainder(widths(w),changed)/=0),1,label)
      enddo
    enddo
  end subroutine

  subroutine check_checker(decoded,width)
    integer(int8), intent(in) :: decoded(:)
    integer, intent(in) :: width
    integer(int8) :: changed(size(decoded))
    integer :: i,bad
    character(len=80) :: label
    if(width==14) then
      call chkcrc14a(decoded,bad)
    else
      call chkcrc13a(decoded,bad)
    endif
    call expect_equal(bad,0,'checker accepts frozen codeword')
    do i=1,size(decoded)
      changed=decoded
      changed(i)=1_int8-changed(i)
      if(width==14) then
        call chkcrc14a(changed,bad)
      else
        call chkcrc13a(changed,bad)
      endif
      write(label,'(a,i0,a,i0)') 'CRC',width,' checker detects flipped bit ',i
      call expect_equal(bad,1,label)
    enddo
  end subroutine

  subroutine check_codecs()
    integer(int8) :: payload(77),scrambled(77),cw174(174),cw128(128)
    integer :: t8(79),t4(103),tmsk(144),i,itype
    character(len=37) :: message,sent
    character(len=77) :: payload_text
    do i=1,size(payloads)
      payload_text=payloads(i)
      read(payload_text,'(77i1)') payload
      call encode174_91(payload,cw174)
      call expect_bits(cw174,ft8_codewords(i),'FT8 codeword')
      call check_checker(cw174(:91),14)
      call encode174_91var(payload,cw174)
      call expect_bits(cw174,ft8_codewords(i),'FT8 variant codeword')
      scrambled=payload
      call ft4_scramble(scrambled)
      call encode174_91(scrambled,cw174)
      call expect_bits(cw174,ft4_codewords(i),'FT4 scrambled codeword')
      call check_checker(cw174(:91),14)
      call encode_128_90(payload,cw128)
      call expect_bits(cw128,msk_codewords(i),'MSK144 codeword')
      call check_checker(cw128(:90),13)
      call ft8_tones_from_77bits(payload,t8)
      call expect_tones(t8,ft8_tones(i),'FT8 tones')
      call ft4_tones_from_77bits(payload,t4)
      call expect_tones(t4,ft4_tones(i),'FT4 tones')
      call check_augmented(payload)
    enddo
    message='K1ABC W9XYZ FN42'
    call genmsk_128_90(message,0,sent,tmsk,itype)
    call expect_equal(itype,1,'MSK144 standard message type')
    call expect_tones(tmsk,msk_tones(1),'MSK144 tones')
  end subroutine

end program test_crc
