! SPDX-License-Identifier: GPL-3.0-or-later
! lib/streaming_jtty_width.f90 against the receiver itself: random runs of
! valid frames of every kind (TEXT5, each structured kind, with and without
! 599, at their widths), some starting two frame periods after the frame
! before (one period of silence), go through
! the receiver's start_message and append_active_message one frame at a time,
! as jtty_mdecode.f90 assembles them from unpack_jtty's text of each frame. While
! the width is at most 80 the receiver's message is exactly that long and owes
! a separator exactly when the width says so; past 80 the receiver's 80 are
! full and the rest is lost.
program test_streaming_jtty_width
  use jtty_mdec, only: decode, message_assembly, reset_decode_search_state, &
       discard_pending_updates, start_message, append_active_message
  use jtty_source_codec, only: jtty_source_atom, pack_jtty_atom, render_jtty_atom, &
       unpack_jtty_atom, JTTY_ATOM_CALL, JTTY_ATOM_EXCH_NUM, JTTY_ATOM_EXCH_LOC, &
       JTTY_ATOM_EXCH_PAIR, JTTY_ATOM_EXCH_NUM_TIME, JTTY_ATOM_CONTROL, &
       JTTY_ATOM_GRID4, JTTY_ATOM_TEXT5, JTTY_PAIR_ZONE_LOC3, JTTY_PAIR_CLASS_SECTION
  use jtty_mod, only: MAX_FRAMES, unpack_jtty
  use streaming_jtty_width, only: jtty_open_message, jtty_message_append, &
       JTTY_RECEIVER_COLUMNS
  implicit none

  integer, parameter :: MAX_POOL = 4000, RUNS = 20000, MAX_RUN = 30
  character(len=34) :: pool(MAX_POOL)
  logical :: pool_structured(MAX_POOL), pool_599(MAX_POOL)
  integer :: npool, kinds_seen(0:7), widest
  integer :: run, i, length, pick, checked, overflowed, gaps, starts599
  character(len=34) :: frames(MAX_RUN)
  logical :: paused(MAX_RUN)
  real :: r
  integer, allocatable :: seed(:)
  integer :: nseed

  call random_seed(size=nseed)
  allocate(seed(nseed))
  seed = 20261121
  call random_seed(put=seed)

  call build_pool()
  call expect(npool .gt. 300, 'the pool holds a few hundred frames')
  do i = 0, 7
     call expect(kinds_seen(i) .gt. 0, 'the pool holds every atom kind')
  end do
  ! The planner's 21-column reserve covers the first frame after a pause: five
  ! columns in place of the separator owed, then the widest frame.
  call expect(5 + widest .le. 21, 'a pause and the widest frame fit 21 columns')

  checked = 0
  overflowed = 0
  gaps = 0
  starts599 = 0
  do run = 1, RUNS
     call random_number(r)
     length = 1 + int(r * MAX_RUN)
     do i = 1, length
        call random_number(r)
        pick = 1 + int(r * npool)
        ! Some runs open with a 599 frame, for the leading ~.
        if (i .eq. 1 .and. mod(run, 7) .eq. 0) pick = first_599()
        frames(i) = pool(pick)
        call random_number(r)
        paused(i) = i .gt. 1 .and. r .lt. 0.15
     end do
     call check_run(frames, paused, length)
  end do
  call expect(overflowed .gt. 1000, 'many runs pass 80 columns')
  call expect(checked - overflowed .gt. 100000, 'many frames fit')
  call expect(gaps .gt. 10000, 'many frames follow a pause')
  call expect(starts599 .gt. 1000, 'many runs open with 599')

  print *, 'test_streaming_jtty_width: all checks passed'

contains

  integer function first_599()
    integer :: j
    first_599 = 1
    do j = 1, npool
       if (pool_599(j)) then
          call random_number(r)
          if (r .lt. 0.2) then
             first_599 = j
             return
          end if
          first_599 = j
       end if
    end do
  end function first_599

  subroutine check_run(frames, paused, n)
    character(len=34), intent(in) :: frames(:)
    logical, intent(in) :: paused(:)
    integer, intent(in) :: n
    type(jtty_open_message) :: state
    type(decode) :: candidate
    type(message_assembly) :: message
    character(len=34) :: c32(MAX_FRAMES)
    logical :: accepted, valid, source_valid
    integer :: j

    call reset_decode_search_state()
    call discard_pending_updates()
    state = jtty_open_message()
    do j = 1, n
       c32(1) = frames(j)
       candidate = decode()
       call unpack_jtty(c32, 1, candidate%decoded, candidate%trailing_sep, &
            candidate%is_last_frame, source_valid)
       call expect(source_valid, 'the receiver takes every pool frame')
       if (j .eq. 1) then
          call start_message(candidate, message, accepted)
          if (candidate%decoded(1:4) .eq. '599 ') starts599 = starts599 + 1
       else
          call append_active_message(1, candidate, merge(2, 1, paused(j)), message, accepted)
          if (paused(j)) gaps = gaps + 1
       end if
       call expect(accepted, 'the receiver accepts the frame')
       call jtty_message_append(state, frames(j), paused(j), valid)
       call expect(valid, 'the width takes every pool frame')
       checked = checked + 1
       if (state%columns .le. JTTY_RECEIVER_COLUMNS) then
          if (len_trim(message%decoded) .ne. state%columns .or.               &
               (message%trailing_sep .neqv. state%separator)) then
             write(*, '(a,i0,a,i0,a,l1,a,l1)') 'width ', state%columns,        &
                  ', receiver ', len_trim(message%decoded), ', separator ',    &
                  state%separator, ' vs ', message%trailing_sep
             write(*, '(a)') 'receiver: "' // trim(message%decoded) // '"'
             call expect(.false., 'the width is the receiver''s')
          end if
       else
          overflowed = overflowed + 1
          call expect(len_trim(message%decoded) .ge. JTTY_RECEIVER_COLUMNS - 1, &
               'past 80 the receiver''s message is full')
       end if
    end do
  end subroutine check_run

  subroutine add(atom)
    type(jtty_source_atom), intent(in) :: atom
    character(len=34) :: frame
    character(len=80) :: rendered
    type(jtty_source_atom) :: back
    logical :: valid, eom

    if (npool .ge. MAX_POOL) return
    call pack_jtty_atom(atom, frame, .false., valid)
    if (.not. valid) return
    call unpack_jtty_atom(frame, back, valid, eom)
    if (.not. valid) return
    call render_jtty_atom(back, rendered, valid)
    if (.not. valid) return
    npool = npool + 1
    pool(npool) = frame
    pool_structured(npool) = back%kind .ne. JTTY_ATOM_TEXT5
    pool_599(npool) = pool_structured(npool) .and. rendered(1:4) .eq. '599 '
    kinds_seen(back%kind) = kinds_seen(back%kind) + 1
    if (pool_structured(npool)) widest = max(widest, len_trim(rendered))
  end subroutine add

  subroutine build_pool()
    character(len=*), parameter :: alphabet = &
         '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!"#$%,&*()_''=[]{}<>|:;'
    character(len=6), parameter :: calls(10) = [character(len=6) :: 'K1ABC', &
         'W9XYZ', 'KJ5HST', 'G4ABC', 'K1JT', 'N1M', 'VK9AB', 'JA1ABC', 'K1AB', 'AB1CDE']
    character(len=3), parameter :: tokens(6) = [character(len=3) :: 'MA', 'ONT', &
         'EMA', 'DX', 'ABC', 'NY']
    character(len=4), parameter :: grids(4) = ['FN42', 'EM10', 'AA00', 'RR99']
    integer, parameter :: numbers(10) = [0, 1, 5, 40, 99, 123, 999, 1000, 16383, 131071]
    integer, parameter :: minutes(3) = [0, 59, 1439]
    type(jtty_source_atom) :: atom
    integer :: s, role, v, t, k, c, j
    character(len=5) :: text5

    npool = 0
    kinds_seen = 0
    widest = 0
    do s = 0, 5
       do c = 1, size(calls)
          atom = jtty_source_atom(kind=JTTY_ATOM_CALL, subtype=s, text=calls(c))
          call add(atom)
       end do
    end do
    do role = 0, 1
       do s = 0, 7
          do v = 1, size(numbers)
             atom = jtty_source_atom(kind=JTTY_ATOM_EXCH_NUM, subtype=s, role=role, value=numbers(v))
             call add(atom)
          end do
       end do
       do s = 0, 4
          do t = 1, size(tokens)
             atom = jtty_source_atom(kind=JTTY_ATOM_EXCH_LOC, subtype=s, role=role, text=tokens(t))
             call add(atom)
          end do
       end do
       do v = 1, size(numbers)
          do t = 1, 3
             atom = jtty_source_atom(kind=JTTY_ATOM_EXCH_NUM_TIME, role=role, &
                  value=numbers(v), value2=minutes(t))
             call add(atom)
          end do
       end do
       do t = 1, size(grids)
          atom = jtty_source_atom(kind=JTTY_ATOM_GRID4, role=role, text=grids(t))
          call add(atom)
       end do
    end do
    do v = 1, 40, 13
       do t = 1, size(tokens)
          atom = jtty_source_atom(kind=JTTY_ATOM_EXCH_PAIR, subtype=JTTY_PAIR_ZONE_LOC3, &
               value=v, text=tokens(t))
          call add(atom)
       end do
    end do
    do v = 1, 32, 10
       do k = 0, 5
          do t = 1, 85, 21
             atom = jtty_source_atom(kind=JTTY_ATOM_EXCH_PAIR, subtype=JTTY_PAIR_CLASS_SECTION, &
                  value=v, value2=t, text=achar(iachar('A') + k))
             call add(atom)
          end do
       end do
    end do
    do s = 0, 17
       atom = jtty_source_atom(kind=JTTY_ATOM_CONTROL, subtype=s)
       call add(atom)
    end do
    do j = 1, 400
       do k = 1, 5
          call random_number(r)
          text5(k:k) = alphabet(1 + int(r * len(alphabet)):1 + int(r * len(alphabet)))
       end do
       ! Leading and trailing blanks, which the receiver shows as ~.
       if (mod(j, 5) .eq. 0) text5(1:1) = ' '
       if (mod(j, 7) .eq. 0) text5(4:5) = '  '
       atom = jtty_source_atom(kind=JTTY_ATOM_TEXT5, text=text5)
       call add(atom)
    end do
  end subroutine build_pool

  subroutine expect(condition, message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message
    if (.not. condition) then
       write(*, '(a)') 'test_streaming_jtty_width: FAILED: ' // message
       error stop 1
    end if
  end subroutine expect

end program test_streaming_jtty_width
