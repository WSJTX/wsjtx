! SPDX-License-Identifier: GPL-3.0-or-later
! How wide a JTTY receiver's message grows as frames reach it, for the codec's
! transmit planning (lib/jtty/JttyTransmitHost.cpp): a receiver keeps 80
! characters of a message (jtty_mdecode.f90, message_assembly%decoded) and
! drops the rest without notice, so the codec ends a message before its text
! would pass them.
!
! The rules are the receiver's (jtty_mdecode.f90 start_message and
! append_active_message, on the frame text jtty_mod.f90's unpack_jtty gives a
! single frame), read with its own unpack_jtty_atom and render_jtty_atom:
!   - a TEXT5 frame adds its five characters, blanks shown as ~;
!   - any other frame adds its rendered text, and owes one blank column before
!     the frame after it;
!   - a message whose first frame renders as "599 ..." starts with a ~;
!   - a frame that starts two or three frame periods after the one before it
!     (after one or two periods of silence) adds ~~~~~ in place of the column
!     owed, less one leading ~ of its own.
! columns is the length the receiver's string would have with no 80-character
! cut, so text is lost exactly when it passes 80.
module streaming_jtty_width
  use iso_c_binding, only: c_char, c_int
  use jtty_source_codec, only: jtty_source_atom, unpack_jtty_atom, &
       render_jtty_atom, JTTY_ATOM_TEXT5
  implicit none
  private

  public :: jtty_open_message, jtty_message_append, jtty_message_fresh
  public :: JTTY_RECEIVER_COLUMNS

  integer, parameter :: JTTY_RECEIVER_COLUMNS = 80

  ! A message in progress at the receiver: columns as above, and separator
  ! when its last frame owes a blank column. Both zero is no message.
  type :: jtty_open_message
     integer :: columns = 0
     logical :: separator = .false.
  end type jtty_open_message

contains

  logical function jtty_message_fresh(state)
    type(jtty_open_message), intent(in) :: state
    jtty_message_fresh = state%columns .eq. 0 .and. .not. state%separator
  end function jtty_message_fresh

  ! Appends frame to state as the receiver does when it starts a frame period
  ! after the frame before (gap false) or two or three periods after (gap
  ! true, which a fresh state ignores, as the receiver then starts a message). valid is false, and state unchanged,
  ! for a frame the receiver drops.
  subroutine jtty_message_append(state, frame, gap, valid)
    type(jtty_open_message), intent(inout) :: state
    character(len=34),       intent(in)    :: frame
    logical,                 intent(in)    :: gap
    logical,                 intent(out)   :: valid
    type(jtty_source_atom) :: atom
    character(len=80) :: rendered
    logical :: eom, structured
    integer :: n

    call unpack_jtty_atom(frame, atom, valid, eom)
    if (.not. valid) return
    call render_jtty_atom(atom, rendered, valid)
    if (.not. valid) return
    structured = atom%kind .ne. JTTY_ATOM_TEXT5
    n = 5
    if (structured) n = len_trim(rendered)

    if (jtty_message_fresh(state)) then
       state%columns = n
       if (structured .and. rendered(1:4) .eq. '599 ') state%columns = n + 1
    else if (gap) then
       ! Only a TEXT5 frame can start with a blank, its ~.
       state%columns = state%columns + 5 + n
       if (.not. structured .and. rendered(1:1) .eq. ' ') state%columns = state%columns - 1
    else
       state%columns = state%columns + n
       if (state%separator) state%columns = state%columns + 1
    end if
    state%separator = structured
  end subroutine jtty_message_append

  ! The C++ planner's entry: appends nframes frames (34 characters '0' or '1'
  ! each, consecutive) to the state in columns and separator (0 or 1). With
  ! after_pause nonzero the first frame may follow a pause of one or two frame
  ! periods or none, and counts as the wider of the two. valid is 0 when the
  ! receiver would drop a frame.
  subroutine jtty_message_width_c(columns, separator, frames, nframes, after_pause, valid) &
       bind(C, name='jtty_message_width_c')
    integer(c_int),         intent(inout) :: columns, separator
    character(kind=c_char), intent(in)    :: frames(*)
    integer(c_int), value,  intent(in)    :: nframes, after_pause
    integer(c_int),         intent(out)   :: valid
    type(jtty_open_message) :: state, paused
    character(len=34) :: frame
    logical :: ok, ok_paused
    integer :: i, j

    state = jtty_open_message(int(columns), separator .ne. 0)
    valid = 1
    do i = 1, nframes
       do j = 1, 34
          frame(j:j) = frames((i - 1) * 34 + j)
       end do
       paused = state
       call jtty_message_append(state, frame, .false., ok)
       if (i .eq. 1 .and. after_pause .ne. 0) then
          call jtty_message_append(paused, frame, .true., ok_paused)
          if (ok_paused) state%columns = max(state%columns, paused%columns)
       end if
       if (.not. ok) then
          valid = 0
          return
       end if
    end do
    columns = state%columns
    separator = merge(1, 0, state%separator)
  end subroutine jtty_message_width_c

end module streaming_jtty_width
