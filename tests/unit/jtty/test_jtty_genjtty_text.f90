! SPDX-License-Identifier: GPL-3.0-or-later
program test_jtty_genjtty_text
  use iso_c_binding, only: c_char, c_int
  use jtty_mod, only: MAX_FRAMES, JTTY_EXCHANGE_UNKNOWN, JTTY_EXCHANGE_FIELD_DAY, &
       JTTY_EXCHANGE_RTTY, JTTY_ENCODE_OK, JTTY_ENCODE_UNENCODABLE
  implicit none

  interface
     subroutine genjtty_text_c(msg,exchange_profile,is_final,itone,nsym,frames,nframes, &
          frame_starts,status) bind(C,name='genjtty_text_c')
       use iso_c_binding, only: c_char,c_int
       use jtty_mod, only: MAX_FRAMES
       character(kind=c_char), intent(inout) :: msg(80)
       integer(c_int), value, intent(in) :: exchange_profile,is_final
       integer(c_int), intent(out) :: itone(59*MAX_FRAMES),nsym
       character(kind=c_char), intent(out) :: frames(34*MAX_FRAMES)
       integer(c_int), intent(out) :: nframes,frame_starts(MAX_FRAMES),status
     end subroutine genjtty_text_c
     subroutine genjtty_profile(umsg,exchange_profile,itone,nsym,frame_starts,is_final)
       use jtty_mod, only: MAX_FRAMES
       character(len=80), intent(inout) :: umsg
       integer, intent(in) :: exchange_profile
       integer, intent(out) :: itone(59*MAX_FRAMES),nsym
       integer, intent(out), optional :: frame_starts(MAX_FRAMES)
       integer, intent(in), optional :: is_final
     end subroutine genjtty_profile
  end interface

  type :: example
     character(len=80) :: text
     integer :: profile
  end type example

  type(example), parameter :: examples(8)=[ &
       example('CQ K1ABC CQ',JTTY_EXCHANGE_UNKNOWN), &
       example('cq k1abc cq 599 fn42',JTTY_EXCHANGE_UNKNOWN), &
       example('THANKS FOR THE QSO, BOB. RIG HERE IS A HOME-BREW TRANSCEIVER RUNNING 5 WATTS', &
               JTTY_EXCHANGE_UNKNOWN), &
       example('  leading and  trailing  ',JTTY_EXCHANGE_UNKNOWN), &
       example('HE SAID "73" OK',JTTY_EXCHANGE_UNKNOWN), &
       example('1D EMA',JTTY_EXCHANGE_FIELD_DAY), &
       example('599 05',JTTY_EXCHANGE_RTTY), &
       example('K1ABC 599 123 TU',JTTY_EXCHANGE_RTTY)]
  integer :: i,final

  do i=1,size(examples)
     do final=0,1
        call check(examples(i)%text,examples(i)%profile,final)
     enddo
  enddo

  call expect_unencodable(' ',JTTY_EXCHANGE_UNKNOWN,'a blank message does not encode')
  call expect_unencodable('CQ K1ABC CQ',3,'an unknown exchange profile does not encode')
  call expect_unencodable('599 1 599 2 599 3 599 4 599 5 599 6 599 7 599 8 599 9 599 10 599 11 599 12 599', &
       JTTY_EXCHANGE_RTTY,'serials that outgrow 80 characters do not encode')

  print *, 'test_jtty_genjtty_text: all checks passed'

contains

  ! genjtty_text_c transmits what genjtty_profile does, and its frames are
  ! the frames behind its tones.
  subroutine check(text,profile,final)
    character(len=80), intent(in) :: text
    integer, intent(in) :: profile,final
    character(kind=c_char) :: msg(80),frames(34*MAX_FRAMES)
    character(len=34) :: packed(MAX_FRAMES)
    character(len=80) :: reference
    integer(c_int) :: tones(59*MAX_FRAMES),nsym,nframes,starts(MAX_FRAMES),status
    integer :: expected(59*MAX_FRAMES),expected_nsym,expected_starts(MAX_FRAMES)
    integer :: rebuilt(59*MAX_FRAMES),rebuilt_nsym,j,k
    character(len=80) :: rewritten

    do j=1,80
       msg(j)=text(j:j)
    enddo
    call genjtty_text_c(msg,int(profile,c_int),int(final,c_int),tones,nsym,frames,nframes, &
         starts,status)
    reference=text
    expected_starts=0
    call genjtty_profile(reference,profile,expected,expected_nsym,expected_starts,final)
    do j=1,80
       rewritten(j:j)=msg(j)
    enddo

    call expect(status.eq.JTTY_ENCODE_OK .and. nsym.gt.0,'the message encodes: '//trim(text))
    call expect(nsym.eq.expected_nsym .and. all(tones(1:nsym).eq.expected(1:nsym)), &
         'the tones are genjtty_profile''s: '//trim(text))
    call expect(rewritten.eq.reference,'the message is rewritten as genjtty_profile rewrites it: '//trim(text))
    call expect(nframes*59.eq.nsym .and. all(starts(1:nframes).eq.expected_starts(1:nframes)), &
         'the frame starts are genjtty_profile''s: '//trim(text))

    do j=1,nframes
       do k=1,34
          packed(j)(k:k)=frames(34*(j-1)+k)
       enddo
       call expect(packed(j)(34:34).eq.merge('1','0',j.eq.nframes .and. final.eq.1), &
            'only a final message''s last frame ends it: '//trim(text))
    enddo
    call genjtty_frames(packed,int(nframes),rebuilt,rebuilt_nsym)
    call expect(rebuilt_nsym.eq.nsym .and. all(rebuilt(1:nsym).eq.tones(1:nsym)), &
         'the frames are the frames behind the tones: '//trim(text))
  end subroutine check

  subroutine expect_unencodable(text,profile,message)
    character(len=*), intent(in) :: text,message
    integer, intent(in) :: profile
    character(kind=c_char) :: msg(80),frames(34*MAX_FRAMES)
    character(len=80) :: padded
    integer(c_int) :: tones(59*MAX_FRAMES),nsym,nframes,starts(MAX_FRAMES),status
    integer :: j

    padded=text
    do j=1,80
       msg(j)=padded(j:j)
    enddo
    call genjtty_text_c(msg,int(profile,c_int),1_c_int,tones,nsym,frames,nframes,starts,status)
    call expect(status.eq.JTTY_ENCODE_UNENCODABLE .and. nsym.eq.0 .and. nframes.eq.0,message)
  end subroutine expect_unencodable

  subroutine expect(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if(.not.condition) then
       write(*,'(a)') message
       error stop 1
    endif
  end subroutine expect

end program test_jtty_genjtty_text
