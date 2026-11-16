! SPDX-License-Identifier: GPL-3.0-or-later
! The stream's Echo (configure's mode ECHO, 3 s periods), measured as the
! GUI's MainWindow::dataSink measures it in its Echo block, at m_ihsym ==
! m_hsymStop (widgets/mainwindow.cpp): avecho on the period's samples at its
! 9th 3,456-sample block (31,104 samples), with save_echo_params and avecho
! called as the GUI calls them, and the configure keys in force there.
!
! Keys (lib/streaming_control.f90 parses them; each is in force in every mode
! until another configure sets it, and its value at the start is the GUI's
! with no Astro data and its default settings):
!   doppler       Hz, the GUI's audio Doppler shift (m_fAudioShift, 0 with
!                 CFOM), avecho's ndop, a JSON number truncated toward zero as
!                 the GUI truncates it; 0.
!   rit           Hz, Astro's RIT, truncated the same way; 0.
!   echo_freq     Hz, f1: 1500 plus the dither drawn at transmit, from 1490
!                 to below 1510; 1500.
!   spread        Hz, the spread the client wants measured with, the GUI's
!                 self or DX spread (common/echocom2/, which the codec sets
!                 to it, both); 0, which avecho takes as 0.04.
!   kind          tone, message or cw (case aside), the GUI's Fixed Tone,
!                 Message and CW; tone.
!   echo_spacing  10, 15, 20, 25 or 30 Hz, the GUI's Spacing; 10.
!   echo_text     up to six letters and digits, the GUI's Echo message; empty.
!   echo_average  1, 2, 5, 10, 20, 50 or 100 periods, the GUI's averaging
!                 count; 10.
!   echo_auto     the GUI's Enable Tx (m_auto): avecho measures with the
!                 self spread; false.
!   clear_average in a configure that leaves the stream in ECHO: true clears
!                 the averages at the next measurement, as the GUI's Clear Avg
!                 button does (and its Enable Tx, which a client sends with
!                 it), and false cancels a clear not yet made. The session
!                 starts with one pending, as the GUI does.
! Live (configure's live_audio), as the GUI packs a live period, the codec
! packs the parameters into a copy of the period's first 15 samples
! (save_echo_params) unless the kind is tone: cw packs spacing 0, message the
! spacing, and avecho then writes the message's tones there and recovers the
! call. From a file it reads them back from those samples, whatever the keys
! say (but echo_auto, which measures with the spread key), and the event
! reports them, the spacing as the GUI's line shows it (0 past 30).
!
! The period: a period that reaches its 9th block is measured, whatever its
! samples, as the GUI measures every 3 s period its input fills that far; one
! that ends before (a discontinuity, a mode change, or a flush, halt or the
! end of input with fewer samples) is not. The GUI's input stops while it
! transmits unless Full Duplex is on, so its transmit halves never reach the
! block: a half-duplex client sends no audio while it transmits, and a
! discontinuity before the first sample of the next period.
!
! Thread: the stream's main thread, at the block, as the GUI measures on its
! own thread at the block; nothing else reads the Echo state, avecho's and
! decode_echo's saved arrays or common/echocom/ and /echocom2/. avecho's
! FFTs are planned at patience 0, as the GUI's process plans them (the
! calling thread's four2a override).
module streaming_echo
  use streaming_control, only: configure_fields, MODE_ECHO, ECHO_TONE, ECHO_MESSAGE, ECHO_CW
  use streaming_emit, only: streaming_emit_echo, time_form
  use four2a_patience, only: patience_override
  implicit none
  private
  public :: echo_apply, echo_measure, echo_range_detail

  ! The period, and the half-symbol at which the GUI measures it
  ! (MainWindow::fixStop).
  real(8), parameter, public :: ECHO_TRPERIOD = 3.d0
  integer, parameter, public :: ECHO_STOP = 9
  ! avecho's samples (lib/avecho.f90 id2_0).
  integer, parameter :: NSAMPLES = 6 * 4096 + 4096
  integer, parameter :: NZ = 4096

  type :: echo_settings
     integer          :: doppler = 0
     integer          :: rit = 0
     real             :: freq = 1500.0
     real             :: spread = 0.0
     integer          :: kind = ECHO_TONE
     integer          :: spacing = 10
     character(len=6) :: text = ' '
     integer          :: average = 10
     logical          :: auto = .false.
     ! The GUI's m_nclearave starts at 1.
     logical          :: clear = .true.
  end type echo_settings

  type(echo_settings), save :: echo_

  ! The GUI's routines, declared as they are defined.
  interface
     ! lib/avecho.f90
     subroutine avecho(id2_0, ndop, nfrit, nauto, navg, nqual, f1, xlevel,   &
          snrdb, db_err, dfreq, width, bDiskData, bEchoCall, txcall, rxcall)
       integer*2 :: id2_0(28672)
       integer   :: ndop, nfrit, nauto, navg, nqual
       real      :: f1, xlevel, snrdb, db_err, dfreq, width
       logical*1 :: bDiskData, bEchoCall
       character*6 :: txcall, rxcall
     end subroutine avecho
     ! lib/save_echo_params.f90
     subroutine save_echo_params(nDopTotal, nDopAudio, nfrit, f1, fspread,     &
          ntonespacing, itone, id2, idir)
       integer   :: nDopTotal, nDopAudio, nfrit, ntonespacing, idir
       real      :: f1, fspread
       integer*4 :: itone(6)
       integer*2 :: id2(15)
     end subroutine save_echo_params
     ! lib/gen_echocall.f90
     subroutine gen_echocall(callsign, itone)
       character*6 :: callsign
       integer     :: itone(6)
     end subroutine gen_echocall
  end interface

contains

  ! An accepted configure's Echo keys; mode, the mode it leaves the stream in.
  subroutine echo_apply(cfg, mode)
    type(configure_fields), intent(in) :: cfg
    integer,                intent(in) :: mode
    if (cfg%doppler_set)      echo_%doppler = cfg%doppler
    if (cfg%rit_set)          echo_%rit = cfg%rit
    if (cfg%echo_freq_set)    echo_%freq = real(cfg%echo_freq)
    if (cfg%spread_set)       echo_%spread = real(cfg%spread)
    if (cfg%kind_set)         echo_%kind = cfg%kind
    if (cfg%echo_spacing_set) echo_%spacing = cfg%echo_spacing
    if (cfg%echo_text_set)    echo_%text = cfg%echo_text
    if (cfg%echo_average_set) echo_%average = cfg%echo_average
    if (cfg%echo_auto_set)    echo_%auto = cfg%echo_auto
    if (cfg%clear_average_set .and. mode .eq. MODE_ECHO) echo_%clear = cfg%clear_average
  end subroutine echo_apply

  ! configure_range_error's detail for an Echo key echo_problem names.
  function echo_range_detail(key) result(detail)
    character(len=*), intent(in) :: key
    character(len=:), allocatable :: detail
    select case (trim(key))
    case ('echo_freq')
       detail = 'echo_freq must be a JSON number from 1490 to below 1510: the GUI ' //    &
            'measures Echo at 1500 Hz, dithered from -10 to below +10 Hz'
    case ('doppler', 'rit')
       detail = trim(key) // ' must be a JSON number of Hz less than 2147483648 either way'
    case ('spread')
       detail = 'spread must be a finite JSON number of Hz'
    case ('kind')
       detail = 'kind must be tone, message or cw, case aside'
    case ('echo_spacing')
       detail = 'echo_spacing must be 10, 15, 20, 25 or 30, a JSON integer of Hz'
    case ('echo_text')
       detail = 'echo_text must be up to six letters and digits'
    case ('echo_average')
       detail = 'echo_average must be 1, 2, 5, 10, 20, 50 or 100, a JSON integer'
    case default
       detail = trim(key) // ' is out of range'
    end select
  end function echo_range_detail

  ! The period's measurement on id2, its first k samples (k at least the
  ! stop's 31,104), live or from a file, labelled nutc in form: avecho as the
  ! GUI's Echo block calls it, then the echo line.
  subroutine echo_measure(id2, k, live, nutc, form)
    integer*2,       intent(in) :: id2(*)
    integer,         intent(in) :: k, nutc
    logical,         intent(in) :: live
    type(time_form), intent(in) :: form
    integer*2 :: d2(NSAMPLES)
    integer   :: itone(6), ndop_total, ndop, nfrit, ndf, nauto, navg, nqual, patience
    real      :: f1, width, xlevel, sigdb, db_err, dfreq, in_band_spread
    logical*1 :: disk, echo_call
    character(len=6) :: rxcall
    integer :: nclearave, nsum
    real    :: blue(NZ), red(NZ), fspread_self, fspread_dx
    common/echocom/nclearave,nsum,blue,red
    common/echocom2/fspread_self,fspread_dx

    d2 = 0
    d2(1:min(k, NSAMPLES)) = id2(1:min(k, NSAMPLES))
    ! The Clear Avg button: m_nclearave, and the Echo graph's nsum, blue and
    ! red (EchoGraph::clearAvg); otherwise m_nclearave is 0.
    nclearave = 0
    if (echo_%clear) then
       nclearave = 1
       nsum = 0
       blue = 0.
       red = 0.
    end if
    ! astro0's spreads, which the client chose.
    fspread_self = echo_%spread
    fspread_dx = echo_%spread
    nfrit = echo_%rit
    nauto = merge(1, 0, echo_%auto)
    nqual = 0
    f1 = echo_%freq
    xlevel = 0.
    sigdb = 0.
    db_err = 0.
    dfreq = 0.
    width = echo_%spread
    ndop = echo_%doppler
    ndop_total = echo_%doppler
    navg = echo_%average
    ndf = 0
    ! The GUI's tones of the transmitted message, as guiUpdate fills itone
    ! for a message or CW.
    call gen_echocall(echo_%text, itone)
    in_band_spread = 0.
    disk = .not. live
    if (live) then
       echo_call = echo_%kind .eq. ECHO_MESSAGE
       if (echo_%kind .ne. ECHO_TONE) then
          if (echo_%kind .eq. ECHO_MESSAGE) ndf = echo_%spacing
          call save_echo_params(ndop_total, ndop, nfrit, f1, width, ndf, itone, d2, 1)
       end if
    else
       call save_echo_params(ndop_total, ndop, nfrit, f1, width, ndf, itone, d2, -1)
       echo_call = ndf .ge. 10 .and. ndf .le. 30
       in_band_spread = width
    end if
    patience = patience_override
    patience_override = 0
    call avecho(d2, ndop, nfrit, nauto, navg, nqual, f1, xlevel, sigdb, db_err, dfreq, width, &
         disk, echo_call, echo_%text, rxcall)
    patience_override = patience
    echo_%clear = .false.
    ! The spacing as the GUI's line shows it.
    if (ndf .lt. 0 .or. ndf .gt. 30) ndf = 0
    if (live) then
       call streaming_emit_echo(nutc, form, xlevel, sigdb, db_err, dfreq, nqual, width, nsum, &
            rxcall, 12000.0 / 32768.0, blue, red)
    else
       call streaming_emit_echo(nutc, form, xlevel, sigdb, db_err, dfreq, nqual, width, nsum, &
            rxcall, 12000.0 / 32768.0, blue, red, [ndop_total, ndop, nfrit, ndf], f1, in_band_spread)
    end if
  end subroutine echo_measure

end module streaming_echo
