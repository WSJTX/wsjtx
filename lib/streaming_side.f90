! SPDX-License-Identifier: GPL-3.0-or-later
! The decoders' side outputs as events, and the edits of the Q65 and Fox
! caller lists. The decoders still write the side files WSJT-X's GUI reads,
! as the worker does; each event carries the list the attempt leaves, which
! is what its file then holds but in one case below, so that a client that
! shares no directory with the codec gets the same lists:
!   fox_callers  after each FT8 attempt in Fox mode: the records the attempt
!                writes to houndcallers.txt (lib/ft8_engine_kernel.f90). An
!                attempt that ages out every caller writes none and leaves
!                the file's last lines, which the GUI then shows; its line
!                has no callers (doc/codec/interface.md states it).
!   averages     after each JT4 and JT65 attempt: avemsg.txt's entries
!                (lib/jt4_host_support.f90, lib/jt65_host_support.f90)
!   q65_sync     where a Q65 attempt writes red.dat: its values
!                (lib/q65_host_support.f90)
!   q65_callers  where a Q65 contest attempt saves tsil.3q (after a load
!                that succeeded): the records it leaves, which it saves,
!                whether or not the save succeeds (lib/q65_host_support.f90)
! Each is written on the thread that runs the attempt (the decode thread, or
! the frame loop's without one), after the attempt's decode lines and before
! its decode_finished, with the attempt's period label as "time" and its
! "pass", as the attempt's decode lines carry them. lib/streaming_emit.f90
! states the lines' fields. Each routine writes its own line, with its "t"
! and every field name in literals, as .github/scripts/check-codec-reference.py
! reads them.
!
! The GUI edits tsil.3q between decodes: it removes a caller with rm_q3list
! (lib/get_q3list.f90) and erases the list by deleting the file. Configure's
! one-shot q65_callers_clear and q65_callers_remove do the same, on the
! decode thread at the configure's place in the stream (lib/streaming_decode.f90),
! so an edit lands between two attempts and the next attempt loads it. The
! GUI's Fox reset deletes houndcallers.txt, and the FT8 decoder starts a new
! caller list when it finds none (lib/ft8_engine_kernel.f90);
! fox_callers_clear deletes it likewise.
module streaming_side
  use, intrinsic :: iso_c_binding, only: c_char, c_int, c_null_char
  use, intrinsic :: iso_fortran_env, only: real32
  use streaming_emit, only: streaming_emit_enabled, streaming_emit_line,    &
       streaming_emit_pass, streaming_emit_real, streaming_emit_time,        &
       streaming_emit_json_escape, streaming_emit_base64
  use prog_args, only: data_dir, temp_dir
  implicit none
  private
  public :: side_fox_callers, side_averages, side_q65_sync, side_q65_callers
  public :: side_q65_edit, side_fox_clear

  interface
     subroutine rm_q3list(fname, dxcall)
       character(len=*), intent(in) :: fname, dxcall
     end subroutine rm_q3list
     ! remove() of a missing file fails harmlessly. No INQUIRE(FILE=) first:
     ! on Windows it ends the codec's stdin (lib/stream_remove_file.c).
     function c_remove(path) result(status) bind(C, name='remove')
       import :: c_char, c_int
       character(kind=c_char), intent(in) :: path(*)
       integer(c_int) :: status
     end function c_remove
  end interface

contains

  ! The callers houndcallers.txt gets, in its order: call, grid, SNR,
  ! frequency, distance (km; 9999 without a grid) and age (30-s units).
  subroutine side_fox_callers(nutc, count, calls, grids, snrs, freqs, distances, ages)
    integer,          intent(in) :: nutc, count
    character(len=*), intent(in) :: calls(:), grids(:)
    integer,          intent(in) :: snrs(:), freqs(:), distances(:), ages(:)
    character(len=:), allocatable :: list
    integer :: i
    if (.not. streaming_emit_enabled()) return
    list = ''
    do i = 1, count
       if (i .gt. 1) list = list // ','
       list = list // '{"call":' // text_(calls(i)) // ',"grid":' // text_(grids(i)) //  &
            ',"snr":' // int_(snrs(i)) // ',"freq":' // int_(freqs(i)) //                  &
            ',"distance":' // int_(distances(i)) // ',"age":' // int_(ages(i)) // '}'
    end do
    call streaming_emit_line('{"v":1,"t":"fox_callers","time":"' // streaming_emit_time(nutc) //   &
         '","callers":[' // list // ']' // pass_() // '}')
  end subroutine side_fox_callers

  ! avemsg.txt's entries in its order: used, the period's label (in the
  ! label's form: utc_is_hhmmss as the mode's decodes), sync, dt, frequency
  ! and polarity (-1, 0 or 1).
  subroutine side_averages(mode, nutc, utc_is_hhmmss, count, used, utc, sync, dt, freq, polarity)
    character(len=*), intent(in) :: mode
    integer,          intent(in) :: nutc, count
    logical,          intent(in) :: utc_is_hhmmss
    logical,          intent(in) :: used(:)
    integer,          intent(in) :: utc(:), freq(:), polarity(:)
    real,             intent(in) :: sync(:), dt(:)
    character(len=:), allocatable :: list
    integer :: i
    if (.not. streaming_emit_enabled()) return
    list = ''
    do i = 1, count
       if (i .gt. 1) list = list // ','
       list = list // '{"used":' // trim(merge('true ', 'false', used(i))) //                &
            ',"time":"' // streaming_emit_time(utc(i), utc_is_hhmmss) // '"' //              &
            ',"sync":' // streaming_emit_real(sync(i)) // ',"dt":' // streaming_emit_real(dt(i)) // &
            ',"freq":' // int_(freq(i)) // ',"polarity":' // int_(polarity(i)) // '}'
    end do
    call streaming_emit_line('{"v":1,"t":"averages","time":"' //                                  &
         streaming_emit_time(nutc, utc_is_hhmmss) // '","mode":"' // mode // '","entries":[' //    &
         list // ']' // pass_() // '}')
  end subroutine side_averages

  ! red.dat's values: dt, the averaged curve's least and greatest, the bin
  ! width and the first bin written, and each bin's averaged and current
  ! values as written.
  subroutine side_q65_sync(nutc, dt, low, high, df, first_bin, averaged, current)
    integer, intent(in) :: nutc, first_bin
    real,    intent(in) :: dt, low, high, df
    real,    intent(in) :: averaged(:), current(:)
    if (.not. streaming_emit_enabled()) return
    call streaming_emit_line('{"v":1,"t":"q65_sync","time":"' // streaming_emit_time(nutc, .true.) // &
         '","dt":' // streaming_emit_real(dt) //                                                      &
         ',"averaged_min":' // streaming_emit_real(low) // ',"averaged_max":' //                       &
         streaming_emit_real(high) // ',"df":' // streaming_emit_real(df) // ',"first_bin":' //        &
         int_(first_bin) // ',"averaged":"' // streaming_emit_base64(real(averaged, real32)) //        &
         '","current":"' // streaming_emit_base64(real(current, real32)) // '"' // pass_() // '}')
  end subroutine side_q65_sync

  ! tsil.3q's records in its order: call, grid, last seen (Unix seconds) and
  ! frequency; its Moon elevation, always 0 as the decoder writes it, is left out.
  subroutine side_q65_callers(nutc, count, calls, grids, seconds, freqs)
    integer,          intent(in) :: nutc, count
    character(len=*), intent(in) :: calls(:), grids(:)
    integer,          intent(in) :: seconds(:), freqs(:)
    character(len=:), allocatable :: list
    integer :: i
    if (.not. streaming_emit_enabled()) return
    list = ''
    do i = 1, count
       if (i .gt. 1) list = list // ','
       list = list // '{"call":' // text_(calls(i)) // ',"grid":' // text_(grids(i)) //  &
            ',"last_seen":' // int_(seconds(i)) // ',"freq":' // int_(freqs(i)) // '}'
    end do
    call streaming_emit_line('{"v":1,"t":"q65_callers","time":"' // streaming_emit_time(nutc, .true.) // &
         '","callers":[' // list // ']' // pass_() // '}')
  end subroutine side_q65_callers

  ! The GUI's edits of <-a>/tsil.3q: clear deletes it, as "Erase list of Q65
  ! callers" does; then each call goes through rm_q3list, as Ctrl-click and
  ! logging do. A file that cannot be deleted stays, as in the GUI.
  subroutine side_q65_edit(clear, calls)
    logical,          intent(in) :: clear
    character(len=*), intent(in) :: calls(:)
    character(len=:), allocatable :: path
    integer :: i
    integer(c_int) :: status
    path = trim(data_dir) // '/tsil.3q'
    if (clear) status = c_remove(path // c_null_char)
    do i = 1, size(calls)
       call rm_q3list(path, trim(calls(i)))
    end do
  end subroutine side_q65_edit

  ! The GUI's Fox reset (FoxReset, widgets/mainwindow.cpp): <-t>/houndcallers.txt
  ! deleted, so the next FT8 attempt in Fox mode starts a new caller list. A
  ! file that cannot be deleted stays, as in the GUI.
  subroutine side_fox_clear()
    character(len=:), allocatable :: path
    integer(c_int) :: status
    path = trim(temp_dir) // '/houndcallers.txt'
    status = c_remove(path // c_null_char)
  end subroutine side_fox_clear

  ! The pass mark a decode line ends with ('': none yet).
  function pass_() result(text)
    character(len=:), allocatable :: text
    text = ''
    if (streaming_emit_pass() .gt. 0) text = ',"pass":' // int_(streaming_emit_pass())
  end function pass_

  ! A blank-trimmed text as a JSON string.
  function text_(value) result(text)
    character(len=*), intent(in) :: value
    character(len=:), allocatable :: text
    character(len=6 * len(value) + 1) :: escaped
    integer :: n
    call streaming_emit_json_escape(value, escaped, n)
    text = '"' // escaped(1:n) // '"'
  end function text_

  function int_(n) result(text)
    integer, intent(in) :: n
    character(len=:), allocatable :: text
    character(len=12) :: s
    write(s, '(i0)') n
    text = trim(s)
  end function int_
end module streaming_side
