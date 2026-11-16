! SPDX-License-Identifier: GPL-3.0-or-later
! WSPR in the stream session ({"mode":"WSPR"}, case aside; no integer code
! selects it): WSPR-2, the GUI's WSPR, at a trperiod of 120 s, the only one
! it takes. Each period decodes once, at the GUI's stop, 396 half-symbols
! (1,368,576 samples; live, 114.05 s in) or, from a file, at the period's
! end, on the decode thread as the other period modes decode, through the
! decoder wsprd uses (lib/wsprd/wsprd_decode.h, lib/stream_wspr.c) on the
! period's first 1,368,000 samples, the audio wsprd reads from the GUI's WAV
! file; a period cut short (flush, halt, end of input) decodes what it has,
! zeros after it.
! depth is the GUI's (m_ndepth&7 in MainWindow::decode): 1 wsprd -qB, 2
! -C 500 -o 4, 3 -C 500 -o 4 -d, any other value wsprd's defaults.
! The hash table, which resolves type 3 messages, lives for the session: it
! is loaded from <-a>/hashtable.txt before the session's first WSPR decode
! and written back, in wsprd's format, after every WSPR period decoded;
! wspr_hash_clear true empties it and deletes the file, as the GUI's Erase
! WSPR hashtable, at the configure's place in the stream. The session never
! reads or writes wspr_wisdom.dat (its FFT plans see the codec's imported
! jt9_wisdom.dat), and writes no ALL_WSPR.TXT or wspr_spots.txt: those are
! the client's.
! Each decode is a decode line, in wsprd's order (ascending frequency):
!   decode  {"v":1,"t":"decode","mode":"WSPR","time":"091800","snr":-9,
!            "dt":1.13333321,"freq":1446,"message":"ND6P DM04 30",
!            "audio_freq":1446.283203125,"snr_real":-8.51409531,"drift":0,
!            "wspr_sync":0.683369577,"wspr_sync10":6,"wspr_pass":1,
!            "wspr_blocksize":1,"wspr_jitter":0,"wspr_osd":0,
!            "wspr_hard_errors":0,"wspr_cycles":1,"wspr_metric":612,
!            "pass":396}
!          (samples/WSPR/150426_0918.wav, depth 3, nutc 918)
!          time: the period's label (lib/streaming_io.f90), never a date.
!          snr: what wsprd's %3.0f prints of its snr, an integer (round half
!          to even); freq: 1500 + f1 rounded half to even, Hz; dt as every
!          decode's (9 significant digits, which read back as wsprd's
!          single-precision value); message as wsprd prints it (a type 3
!          call it cannot resolve is <...>).
!          audio_freq: wsprd's single-precision 1500 + f1, Hz, in full, so
!          that a double parse is exact; snr_real and wspr_sync: wsprd's snr
!          and sync to 9 significant digits; drift: (int) of wsprd's drift,
!          as it prints it; wspr_sync10: wspr_spots.txt's (int)(10*sync);
!          wspr_pass: its pass, 1 to 3; wspr_blocksize, wspr_jitter,
!          wspr_osd (1 when OSD decoded it), wspr_hard_errors, wspr_cycles
!          (cycles/81) and wspr_metric: ALL_WSPR.TXT's columns as it prints
!          them.
! A client rebuilds wsprd's lines byte for byte with C printf (half to even
! on the binary value) from these, its dial frequency D in MHz (the double
! wsprd reads from -f, strtod of the GUI's %.6f), and the UTC date of the
! period's start as YYMMDD (date6) and the label's first four digits (time4):
! with F = D + audio_freq/1e6 in double,
!   ALL_WSPR.TXT  "%6s %4s %3.0f %5.2f %11.7f  %-22s %2d %5.2f %2d %2d %4d %2d %3d %5u %5d\n"
!                 date6, time4, snr_real, dt, F, message, drift, wspr_sync,
!                 wspr_pass, wspr_blocksize, wspr_jitter, wspr_osd,
!                 wspr_hard_errors, wspr_cycles, wspr_metric
!   wspr_spots.txt "%6s %4s %3d %3.0f %4.1f %10.6f  %-22s %2d %5u %4d\n"
!                 date6, time4, wspr_sync10, snr_real, dt, F, message,
!                 drift, wspr_cycles, wspr_jitter
! The period's decode_finished counts [0, decodes, 0]. WSPR keeps no period
! to decode again: the GUI's Decode button does nothing in WSPR.
module streaming_wspr
  use, intrinsic :: iso_c_binding, only: c_int, c_float, c_char, c_short, c_null_char
  use, intrinsic :: iso_fortran_env, only: int16
  use prog_args, only: data_dir
  use streaming_emit, only: streaming_emit_decode, wspr_marks
  implicit none
  private
  public :: wspr_decode, wspr_hash_clear

  integer, parameter :: MAX_DECODES = 50

  ! lib/stream_wspr.c's struct stream_wspr_decode.
  type, bind(C) :: wspr_result
     integer(c_int) :: snr, freq, drift, sync10, pass, blocksize, jitter, osd
     integer(c_int) :: hard_errors, cycles, metric
     real(c_float) :: dt, snr_real, sync
     character(kind=c_char) :: audio_freq(32), message(24)
  end type wspr_result

  interface
     function stream_wspr_decode(samples, k, ndepth, dir, dir_len, out) result(n) &
          bind(C, name='stream_wspr_decode')
       import :: c_int, c_short, c_char, wspr_result
       integer(c_short), intent(in) :: samples(*)
       integer(c_int), value :: k, ndepth, dir_len
       character(kind=c_char), intent(in) :: dir(*)
       type(wspr_result), intent(out) :: out(*)
       integer(c_int) :: n
     end function stream_wspr_decode
     subroutine stream_wspr_hash_clear(dir, dir_len) bind(C, name='stream_wspr_hash_clear')
       import :: c_int, c_char
       character(kind=c_char), intent(in) :: dir(*)
       integer(c_int), value :: dir_len
     end subroutine stream_wspr_hash_clear
  end interface

contains

  ! The period's first k samples at depth ndepth, each decode a line labelled
  ! nutc in the form the decode pass set; count is the number sent, or -1
  ! when the decoder could not run (no memory for its buffers or plans).
  subroutine wspr_decode(samples, k, ndepth, nutc, count)
    integer(int16), intent(in) :: samples(*)
    integer, intent(in) :: k, ndepth, nutc
    integer, intent(out) :: count
    type(wspr_result) :: results(MAX_DECODES)
    type(wspr_marks) :: marks
    integer :: i, n
    n = stream_wspr_decode(samples, k, ndepth, data_dir, len_trim(data_dir), results)
    count = 0
    if (n .lt. 0) then
       count = -1
       return
    end if
    do i = 1, n
       associate (r => results(i))
         marks%audio_freq = text_(r%audio_freq)
         marks%snr_real = r%snr_real
         marks%drift = r%drift
         marks%sync = r%sync
         marks%sync10 = r%sync10
         marks%pass = r%pass
         marks%blocksize = r%blocksize
         marks%jitter = r%jitter
         marks%osd = r%osd
         marks%hard_errors = r%hard_errors
         marks%cycles = r%cycles
         marks%metric = r%metric
         call streaming_emit_decode('WSPR', nutc, r%snr, r%dt, r%freq, text_(r%message), &
              wspr=marks)
       end associate
       count = count + 1
    end do
  end subroutine wspr_decode

  ! wspr_hash_clear: the table empties and <-a>/hashtable.txt goes.
  subroutine wspr_hash_clear()
    call stream_wspr_hash_clear(data_dir, len_trim(data_dir))
  end subroutine wspr_hash_clear

  ! A NUL-terminated C string.
  function text_(chars) result(text)
    character(kind=c_char), intent(in) :: chars(:)
    character(len=:), allocatable :: text
    integer :: i
    text = ''
    do i = 1, size(chars)
       if (chars(i) .eq. c_null_char) exit
       text = text // chars(i)
    end do
  end function text_

end module streaming_wspr
