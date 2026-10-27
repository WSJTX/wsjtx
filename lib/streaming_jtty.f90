! SPDX-License-Identifier: GPL-3.0-or-later
! JTTY receive in the stream session. lib/streaming_io.f90 hands each JTTY
! audio chunk to jtty_audio, which runs the GUI's receive path (jtty_rx_*,
! lib/jtty/jtty_receive_context.f90) on it.
!
! Output lines, besides the stream's own (lib/streaming_emit.f90):
!   session      {"v":1,"t":"session","session":N,"origin":S}
!   jtty_update  {"v":1,"t":"jtty_update","session":N,"id":I,
!                 "state":"growing"|"complete"|"expired"|"ended",
!                 "freq":Hz,"snr":dB,"start":T,"latest":T,"text":"...",
!                 "gaps":[O,...]}
!   error        {"v":1,"t":"error","code":"configure_range_error",
!                 "key":K,"detail":"K must be from 0 to 6000 Hz"}
!
! Sessions are numbered from 1 in each process. A session begins with its
! first audio sample; S is that sample's index in the stream, counted from 0
! at the first audio sample read in any mode (odd-length frames are skipped
! uncounted). Where a session begins in a transmission changes what decodes,
! as it does in the GUI. After each decoder step the session writes one
! jtty_update line per pending update, as the GUI takes them. Between its
! session line and its last update only error lines and the replies to
! encode requests appear; an odd_audio_frame error comes before the "ended"
! lines it causes. I is unique in the process, across sessions and mode
! changes, and the same on every update of one message. A message's last
! line is its "complete", "expired" or "ended" one. A message whose final
! frame has not arrived is "expired" once about 8 s of audio follow its
! latest frame, and ending a session gives each message still in progress an
! "ended" line; after a fatal error (exit 1) nothing follows, so messages in
! progress get no terminal line.
!
! start and latest (T) are the sync times of the message's first and latest
! decoded frames, in stream samples / 12000. freq is the latest frame's lowest
! tone, to 3 decimals; snr is the first frame's, in whole dB, and does not
! change. text is what the GUI shows for an update (trimmed, filler removed):
! at most 80 characters, as the decoder keeps only the first 80 of a message
! (its later frames move latest but add no text), and possibly empty. Each O
! is the 0-based offset in text of a "..." that stands for missed frames,
! which typed dots never are. A freq, start or latest that is not finite is
! null.
!
! Unlike the GUI, ending a session also searches the window that extends past
! its last sample (jtty_rx_process_final), so a last frame that ends shortly
! before the session does still decodes.
!
! The decoder searches rxfreq +/- ntol, and two 300 Hz channels centred on
! 1350 and 1650 Hz, each centre first moved into nfa..nfb and the channel
! clipped to it (neither when nfa > nfb); with the default nfa and nfb, 1200
! to 1800 Hz. Its 6 kHz complex signal keeps only 0 to 3000 Hz of the audio
! (ana64a), so nothing whose lowest tone is above about 2950 Hz decodes, as
! in the GUI. A configure that would leave rxfreq, ntol, nfa or nfb outside
! 0 to 6000 Hz is declined (jtty_range_key): that is the range the keys
! accept, not the band searched.
!
! A decoder step that needs FFT sizes with no saved FFTW wisdom in -a measures
! them: up to tens of seconds on ARM Linux, during which the stream reads no
! input. The sizes follow from rxfreq, ntol, nfa and nfb; a step over zero
! samples needs none, so the first to measure is the first step over other
! audio (noise is enough), and zero samples warm nothing. Wisdom is saved only
! when the stream ends at halt or end of input (a killed process saves none),
! so keep -a across runs.
!
! JTTY encode, in any mode: the control frame
!   {"t":"pack","id":I,"text":S,...}
! with no "mode", or with "mode":"JTTY", case aside (a request naming another
! mode is a slot mode's, lib/streaming_encode.f90).
! I is a JSON integer from -2147483648 to 2147483647, as are serial and
! report; a number in another form ("+5", "05", "1.0") is not one. The
! request's I is echoed on each of its reply lines; a jtty_update's id is the
! codec's message id, so lines are routed by t. Exactly one of text, sent
! as typed text (placeholders expanded, a newline ends a message,
! "final":false leaves the last one open), and template, sent as a function
! key (natively if it is one of the forms below, else expanded as text that
! ends its message); each at most 32767 UTF-16 units, and 1048576 expanded.
! The stream's 262,144-byte control frame limit applies to the whole request,
! so long fields together can exceed it (control_frame_too_large). Any key
! not named here, a misspelt or mis-cased one included, is refused
! bad_request with that key as written (keys are matched without escapes),
! as are freq and rate on a pack.
! "profile": "none" (default), "field_day" or "rtty". "his_call" and
! "exchange" (default ""): at most 64 units, none below U+0020, none from
! U+007F to U+009F and no '%'. "serial" and "report": never defaulted. The
! station's call and grid are the whole mycall and mygrid of the last
! accepted configure that set them, less any ASCII white space at either end
! (as the GUI trims its call), and usable when at most 64 characters of
! printable ASCII, not blank and without '%'. A configure whose mycall or
! mygrid string does not decode leaves that value unusable until another
! sets it. Placeholders, upper case only, are replaced in this order, and a
! value inserted earlier can complete a later one, as in the GUI:
!   %M  the station's call
!   %H  his_call
!   %Q  his_call
!   %N  serial, at least three digits ("007", "-01")
!   %E  profile none: serial as %N; field_day: exchange, upper case, with a
!       space put between its count and class and its section ("1DEMA" is
!       "1D EMA"); rtty: exchange, upper case, or serial as %N when the
!       exchange is DX or #
!   %G  the station's grid: its first four characters, upper case
!   %R  report, signed, at least two digits ("-07", "+05")
! An absent his_call or exchange expands to nothing, as an empty field does
! in the GUI. A native form sends %E as an atom instead: 599 and the serial
! (0 to 131071) under none, and under rtty with DX or #; the count, class
! and section under field_day; otherwise 599 and the rtty exchange, a serial
! or a state. The native forms, matched after collapsing spaces and
! upper-casing (the 599 %N forms exactly):
!   CQ %M CQ, %H %E, %H 599 %N, %H %G, %H TU CQ %M CQ, %M, %H, TU NOW %Q %E,
!   TU NOW %Q 599 %N, TU NOW %Q %G, %H AGN?, %E, 599 %N, %G, 599 %G,
! and the control phrases
!   AGN?, CALL?, AGN CALL, NR?, AGN NR, EXCH?, STATE?, SECTION?, ZONE?, GRID?,
!   RPRT?, QSL TU, TU, QRZ?, QSO B4, WAIT, NIL?, OK?.
! Requests are answered in order, between frames, so a reply also shows that
! every earlier frame was read. It does not wait for decodes: a period's later
! FT8 passes and its decode_finished, and a decode again held for the last
! pass, can come after it. A request holds up the audio behind it while it is
! answered; on live audio (live_audio) FT8 ends a pass on the wall clock, so a
! request inside a period can change that period's decodes. A request's reply
! lines are consecutive, each carries its id, and the last is packed or
! rejected:
!   segment   {"v":1,"t":"segment","id":I,"seg":K,"text":S,"canonical":S,
!              "final":B,"substituted":B,"frames":[{"text":S},...],
!              "seconds":X}
!   packed    {"v":1,"t":"packed","id":I,"segments":M,"substituted":B}
!   rejected  {"v":1,"t":"rejected","id":I,"reason":R,"key":K,"detail":S}
! One segment line per segment, K from 0: text is the characters it carries
! (trimmed, case kept, replacements shown), canonical what its frames carry,
! final whether its last frame ends the message, frames each frame as the
! receiver renders it (a five-character text frame keeps its trailing spaces
! unless it ends the message), X its seconds on air. A line's later segments
! continue its message, of which a receiver keeps the first 80 characters
! (text, above). substituted, a boolean for each segment and for the whole
! text, says whether a character was replaced: one JTTY lacks becomes '#' for
! each UTF-16 unit ("##" beyond the BMP), each byte that is not UTF-8 '#',
! and NUL and '~' a space. A rejected request has no segment line. Each R,
! and the key its line carries:
!   bad_request      the request key at fault; none when the request is
!                    not a flat object
!   too_long         text or template: it, or its expansion, is too long;
!                    none for a render's audio (below)
!   not_configured   mycall or mygrid: it uses the station's call or grid,
!                    and no configure has set a usable one
!   missing          serial or report: it uses one and does not give it
!   empty            none
!   invalid_runtime  none: a native form its context cannot fill
!   encoding_failed  none
! A request without such an id among its own keys (not a nested object's)
! gets error invalid_request_id. An encode that cannot proceed (no memory)
! ends the program with an error line, and with it period decoding and any
! JTTY session.
!
! JTTY render, in any mode: {"t":"render","id":I,"template":S,...,
! "freq":F,"rate":R}, pack's keys with F the lowest tone, a JSON number from
! 200 to 5000 Hz, and R the JSON integer 12000 or 48000. It is answered as
! pack is, except that each segment line also carries "samples":N and is
! followed by its audio, N samples of int16 little-endian mono at R, base64
! (RFC 4648), in lines of 16384 samples but the last,
!   pcm       {"v":1,"t":"pcm","id":I,"seg":K,"seq":Q,"data":S}
! Q counting each segment's pcm lines from 0, and that the last line is
!   rendered  {"v":1,"t":"rendered","id":I,"segments":M,"substituted":B}
! instead of packed. A render of more than 600 s of audio is rejected
! too_long, with no key. A JTTY receiver decodes nothing whose lowest tone is
! above about 2950 Hz (above), though F may be up to 5000 Hz. A render holds
! up audio for as long as it computes and its lines take to be read.

module streaming_jtty
  use, intrinsic :: iso_c_binding, only: c_int, c_int64_t, c_float, c_double,  &
       c_char, c_int16_t, c_int32_t
  use, intrinsic :: iso_fortran_env, only: int16, int64, real64, output_unit,  &
       error_unit
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use jtty_receive_context, only: jtty_rx_create, jtty_rx_destroy,             &
       jtty_rx_begin, jtty_rx_process, jtty_rx_process_final,                  &
       jtty_rx_next_required_sample, jtty_rx_next_search_sample,               &
       jtty_rx_take_updates_with_gaps, jtty_rx_end
  use jtty_mdec, only: UPDATE_GROWING, UPDATE_COMPLETE, UPDATE_EXPIRED,        &
       UPDATE_RECEPTION_ENDED, MAX_DISPLAY_GAPS, jtty_release_fft_resources
  use jtty_filler, only: strip_filler_text
  use streaming_emit, only: streaming_emit_error, streaming_emit_error_code, &
       streaming_emit_json_escape
  use streaming_apply, only: params_block
  use streaming_control, only: configure_fields, encode_request,               &
       parse_encode_request, parse_render_request
  use jtty_source_codec, only: jtty_source_atom, unpack_jtty_atom,             &
       render_jtty_atom, JTTY_ATOM_TEXT5
  use jtty_mod, only: MAX_FRAMES
  implicit none
  private

  public :: jtty_audio, jtty_end, jtty_range_key, jtty_emit_range_error,      &
       jtty_release, jtty_note_station, jtty_encode
  ! The reply lines and the station's call the slot modes' encode shares
  ! (lib/streaming_encode.f90).
  public :: encode_emit_rejected, encode_emit_pcm, encode_station_call

  integer, parameter :: NSPS = 384
  integer, parameter :: NFRAME = 59 * NSPS
  integer, parameter :: WINDOW = NFRAME + NFRAME / 4   ! samples one search step reads
  integer, parameter :: UPDATE_CAPACITY = 30
  integer, parameter :: MAX_HZ = 6000                  ! half the 12 kHz sample rate

  integer(c_int), save :: handle_ = 0
  integer,        save :: session_ = 0
  logical,        save :: session_open_ = .false.
  ! The session's audio from stream sample pcm_base_ on.
  integer(int16), allocatable, save :: pcm_(:)
  integer(int64), save :: pcm_base_ = 0
  integer,        save :: pcm_count_ = 0

  ! Encode. Jtty::Encoder::TransmitStatus, lib/jtty/JttyTransmitHost.hpp.
  integer(c_int32_t), parameter :: TX_ENCODED = 0, TX_EMPTY = 1,                &
       TX_ENCODING_FAILED = 2, TX_INVALID_RUNTIME = 3, TX_TOO_LONG = 4,         &
       TX_BAD_REQUEST = 5, TX_NOT_CONFIGURED = 6, TX_MISSING = 7
  ! jtty_tx_encode's station value lengths for no value and one too long.
  integer(c_int32_t), parameter :: TX_UNSET = -1, TX_OVERLONG = -2
  integer, parameter :: TX_FRAME_BITS = 34
  integer, parameter :: TX_FRAME_SYMBOLS = 59
  integer, parameter :: TX_TEXT = 80                   ! Jtty::maxTransmitLength
  integer, parameter :: PCM_LINE = 16384               ! samples a pcm line holds
  integer, parameter :: MAX_RENDER_SECONDS = 600
  ! The station's call and grid as accepted configures set them; params'
  ! start values are test identities that must never go on the air.
  character(len=64), save :: station_call_ = ' '
  character(len=64), save :: station_grid_ = ' '
  logical,           save :: station_call_set_ = .false.
  logical,           save :: station_grid_set_ = .false.
  logical,           save :: station_call_long_ = .false.
  logical,           save :: station_call_unreadable_ = .false.
  logical,           save :: station_grid_long_ = .false.
  integer(c_int16_t), allocatable, save :: tx_samples_(:)

  interface
     ! lib/jtty/JttyTransmitHost.hpp
     function jtty_tx_encode(is_template, text, text_length, his_call,        &
          his_call_length, exchange, exchange_length, my_call, my_call_length, &
          grid, grid_length, serial, serial_given, report, report_given,      &
          profile, is_final) result(handle) bind(C, name='jtty_tx_encode')
       import :: c_int32_t, c_char
       integer(c_int32_t), value :: is_template, text_length, his_call_length, &
            exchange_length, my_call_length, grid_length, serial, serial_given,  &
            report, report_given, profile, is_final
       character(kind=c_char), intent(in) :: text(*), his_call(*), exchange(*), &
            my_call(*), grid(*)
       integer(c_int32_t) :: handle
     end function jtty_tx_encode
     function jtty_tx_status(handle, segments, substituted) result(status)    &
          bind(C, name='jtty_tx_status')
       import :: c_int32_t
       integer(c_int32_t), value :: handle
       integer(c_int32_t), intent(out) :: segments, substituted
       integer(c_int32_t) :: status
     end function jtty_tx_status
     subroutine jtty_tx_error(handle, key, key_capacity, key_length, detail,  &
          detail_capacity, detail_length) bind(C, name='jtty_tx_error')
       import :: c_int32_t, c_char
       integer(c_int32_t), value :: handle, key_capacity, detail_capacity
       character(kind=c_char), intent(out) :: key(*), detail(*)
       integer(c_int32_t), intent(out) :: key_length, detail_length
     end subroutine jtty_tx_error
     function jtty_tx_segment(handle, index, tones, frames, nframes, text,    &
          text_length, canonical, canonical_length, is_final, substituted)    &
          result(nsym) bind(C, name='jtty_tx_segment')
       import :: c_int32_t, c_char
       integer(c_int32_t), value :: handle, index
       integer(c_int32_t), intent(out) :: tones(*), nframes, text_length,     &
            canonical_length, is_final, substituted
       character(kind=c_char), intent(out) :: frames(*), text(*), canonical(*)
       integer(c_int32_t) :: nsym
     end function jtty_tx_segment
     subroutine jtty_tx_destroy(handle) bind(C, name='jtty_tx_destroy')
       import :: c_int32_t
       integer(c_int32_t), value :: handle
     end subroutine jtty_tx_destroy
     function jtty_tx_render(tones, nsym, sample_rate, f0, samples, capacity) &
          result(count) bind(C, name='jtty_tx_render')
       import :: c_int32_t, c_int16_t, c_float
       integer(c_int32_t), intent(in) :: tones(*)
       integer(c_int32_t), value :: nsym, sample_rate, capacity
       real(c_float), value :: f0
       integer(c_int16_t), intent(out) :: samples(*)
       integer(c_int32_t) :: count
     end function jtty_tx_render
  end interface

contains

  ! Decode samples, the stream's samples from first_sample on. A session
  ! begins with them if none is open.
  subroutine jtty_audio(samples, first_sample, params)
    integer(int16),     intent(in) :: samples(:)
    integer(int64),     intent(in) :: first_sample
    type(params_block), intent(in) :: params
    if (size(samples) .eq. 0) return
    if (.not. session_open_) call begin_session_(first_sample)
    call append_samples_(samples)
    call decode_available_(params, .false.)
  end subroutine jtty_audio

  ! params are the settings the session decoded with.
  subroutine jtty_end(params)
    type(params_block), intent(in) :: params
    if (.not. session_open_) return
    call decode_available_(params, .true.)
    call jtty_rx_end(handle_, UPDATE_RECEPTION_ENDED)
    call emit_updates_()
    session_open_ = .false.
  end subroutine jtty_end

  ! The first of rxfreq, ntol, nfa and nfb outside 0 to MAX_HZ, or ''.
  function jtty_range_key(params) result(key)
    type(params_block), intent(in) :: params
    character(len=:), allocatable :: key
    key = ''
    if (params%nfqso .lt. 0 .or. params%nfqso .gt. MAX_HZ) then
       key = 'rxfreq'
    else if (params%ntol .lt. 0 .or. params%ntol .gt. MAX_HZ) then
       key = 'ntol'
    else if (params%nfa .lt. 0 .or. params%nfa .gt. MAX_HZ) then
       key = 'nfa'
    else if (params%nfb .lt. 0 .or. params%nfb .gt. MAX_HZ) then
       key = 'nfb'
    end if
  end function jtty_range_key

  subroutine jtty_emit_range_error(key)
    character(len=*), intent(in) :: key
    call emit_line_('{"v":1,"t":"error","code":"configure_range_error","key":"' //  &
         key // '","detail":"' // key // ' must be from 0 to ' //              &
         itoa_(int(MAX_HZ, int64)) // ' Hz"}')
  end subroutine jtty_emit_range_error

  ! Before the program's fftwf_cleanup, after which the decoder's plans can
  ! no longer be destroyed.
  subroutine jtty_release()
    if (allocated(tx_samples_)) deallocate(tx_samples_)
    if (handle_ .eq. 0) return
    call jtty_rx_destroy(handle_)
    handle_ = 0
    if (allocated(pcm_)) deallocate(pcm_)
    call jtty_release_fft_resources()
  end subroutine jtty_release

  ! Each session's search grid starts at its first sample, as the GUI's
  ! starts at the first sample of every reception.
  subroutine begin_session_(origin)
    integer(int64), intent(in) :: origin
    if (handle_ .eq. 0) then
       handle_ = jtty_rx_create()
       if (handle_ .eq. 0) call fatal_('no JTTY receive context is available')
       allocate(pcm_(65536))
    end if
    session_ = session_ + 1
    call jtty_rx_begin(handle_, int(session_, c_int64_t), origin, origin, NSPS)
    pcm_base_ = origin
    pcm_count_ = 0
    session_open_ = .true.
    call emit_line_('{"v":1,"t":"session","session":' //                    &
         itoa_(int(session_, int64)) // ',"origin":' // itoa_(origin) // '}')
  end subroutine begin_session_

  subroutine append_samples_(samples)
    integer(int16), intent(in) :: samples(:)
    integer(int16), allocatable :: grown(:)
    integer :: count, drop
    count = size(samples)
    if (pcm_count_ + count .gt. size(pcm_)) then
       ! Release what the decoder can no longer revisit.
       drop = int(min(max(jtty_rx_next_required_sample(handle_) - pcm_base_,  &
            0_int64), int(pcm_count_, int64)))
       if (drop .gt. 0) then
          pcm_(1:pcm_count_ - drop) = pcm_(drop + 1:pcm_count_)
          pcm_count_ = pcm_count_ - drop
          pcm_base_ = pcm_base_ + drop
       end if
       if (pcm_count_ + count .gt. size(pcm_)) then
          allocate(grown(max(2 * size(pcm_), pcm_count_ + count)))
          grown(1:pcm_count_) = pcm_(1:pcm_count_)
          call move_alloc(grown, pcm_)
       end if
    end if
    pcm_(pcm_count_ + 1:pcm_count_ + count) = samples
    pcm_count_ = pcm_count_ + count
  end subroutine append_samples_

  ! As the GUI does: one decoder step at a time while a search window is
  ! available, taking the updates after each step. When the session ends
  ! (final), also the window that extends past its last sample.
  subroutine decode_available_(params, final)
    type(params_block), intent(in) :: params
    logical,            intent(in) :: final
    integer(int64) :: required, available_end
    integer(c_int) :: processed
    integer :: first
    do
       available_end = pcm_base_ + pcm_count_
       if (.not. final .and.                                                 &
            jtty_rx_next_search_sample(handle_) + WINDOW .gt. available_end) return
       required = jtty_rx_next_required_sample(handle_)
       if (required .lt. pcm_base_)                                         &
            call fatal_('required receive audio is no longer retained')
       first = int(required - pcm_base_) + 1
       if (final) then
          processed = jtty_rx_process_final(handle_, pcm_(first:pcm_count_), &
               int(pcm_count_ - first + 1, c_int), required, available_end, 1, &
               params%nfa, params%nfb, real(params%nfqso, c_float),          &
               real(params%ntol, c_float))
       else
          processed = jtty_rx_process(handle_, pcm_(first:pcm_count_),      &
               int(pcm_count_ - first + 1, c_int), required, available_end, 1, &
               params%nfa, params%nfb, real(params%nfqso, c_float),          &
               real(params%ntol, c_float))
       end if
       call emit_updates_()
       if (processed .lt. 0) call fatal_('required receive audio is unavailable')
       if (processed .eq. 0) return
    end do
  end subroutine decode_available_

  subroutine emit_updates_()
    character(kind=c_char) :: text(80 * UPDATE_CAPACITY)
    integer(c_int64_t) :: ids(UPDATE_CAPACITY)
    real(c_float) :: frequencies(UPDATE_CAPACITY)
    real(c_double) :: starts(UPDATE_CAPACITY), latest(UPDATE_CAPACITY)
    integer(c_int) :: terminals(UPDATE_CAPACITY), snrs(UPDATE_CAPACITY), taken
    integer :: gaps(MAX_DISPLAY_GAPS, UPDATE_CAPACITY), ngaps(UPDATE_CAPACITY)
    character(len=80) :: message, shown
    character(len=480) :: escaped
    character(len=:), allocatable :: state, gap_list
    integer :: i, j, length, escaped_length, offset
    do
       taken = jtty_rx_take_updates_with_gaps(handle_, UPDATE_CAPACITY, text, &
            ids, frequencies, starts, latest, terminals, snrs, gaps, ngaps)
       do i = 1, taken
          do j = 1, 80
             message(j:j) = text((i - 1) * 80 + j)
          end do
          call strip_filler_text(message(1:len_trim(message)), shown, length)
          select case (terminals(i))
          case (UPDATE_GROWING)
             state = 'growing'
          case (UPDATE_COMPLETE)
             state = 'complete'
          case (UPDATE_EXPIRED)
             state = 'expired'
          case (UPDATE_RECEPTION_ENDED)
             state = 'ended'
          case default
             call fatal_('the decoder reported an unknown update state')
          end select
          gap_list = ''
          do j = 1, ngaps(i)
             offset = shown_offset_(message, gaps(j, i), shown(1:length))
             if (offset .lt. 0) then
                write(error_unit, '(a,i0,a)') 'wsjt-codec: the gap markers of message ', &
                     ids(i), ' are not in its displayed text; it is reported without gaps'
                gap_list = ''
                exit
             end if
             if (j .gt. 1) gap_list = gap_list // ','
             gap_list = gap_list // itoa_(int(offset, int64))
          end do
          call streaming_emit_json_escape(shown(1:length), escaped, escaped_length)
          call emit_line_('{"v":1,"t":"jtty_update","session":' //          &
               itoa_(int(session_, int64)) // ',"id":' // itoa_(int(ids(i), int64)) // &
               ',"state":"' // state // '","freq":' //                       &
               json_real_(real(frequencies(i), real64), 3) //                &
               ',"snr":' // itoa_(int(snrs(i), int64)) //                    &
               ',"start":' // json_real_(real(starts(i), real64), 6) //       &
               ',"latest":' // json_real_(real(latest(i), real64), 6) //      &
               ',"text":"' // escaped(1:escaped_length) // '","gaps":[' //    &
               gap_list // ']}')
       end do
       if (taken .lt. UPDATE_CAPACITY) return
    end do
  end subroutine emit_updates_

  ! The offset in shown of the '.' at offset in message, or -1. Trimming and
  ! filler removal drop only whitespace and '<', so the n-th '.' of message
  ! is the n-th '.' of shown.
  integer function shown_offset_(message, offset, shown)
    character(len=*), intent(in) :: message, shown
    integer,          intent(in) :: offset
    integer :: dots, i
    shown_offset_ = -1
    if (offset .lt. 0 .or. offset .ge. len(message)) return
    if (message(offset + 1:offset + 1) .ne. '.') return
    dots = count([(message(i:i) .eq. '.', i = 1, offset + 1)])
    do i = 1, len(shown)
       if (shown(i:i) .eq. '.') dots = dots - 1
       if (dots .eq. 0) then
          shown_offset_ = i - 1
          return
       end if
    end do
  end function shown_offset_

  subroutine fatal_(message)
    character(len=*), intent(in) :: message
    call streaming_emit_error(message)
    write(error_unit, '(2a)') 'wsjt-codec: ', message
    stop 1
  end subroutine fatal_

  subroutine emit_line_(line)
    character(len=*), intent(in) :: line
    write(output_unit, '(a)') line
    flush(output_unit)
  end subroutine emit_line_

  function itoa_(value) result(text)
    integer(int64), intent(in) :: value
    character(len=:), allocatable :: text
    character(len=24) :: buffer
    write(buffer, '(i0)') value
    text = trim(buffer)
  end function itoa_

  ! A JSON number with the given decimals; gfortran's f0.d drops the leading
  ! zero ('.25'), which JSON forbids.
  function json_real_(value, decimals) result(text)
    real(real64), intent(in) :: value
    integer,      intent(in) :: decimals
    character(len=:), allocatable :: text
    character(len=64) :: buffer
    character(len=16) :: form
    if (.not. ieee_is_finite(value)) then
       text = 'null'
       return
    end if
    write(form, '(a,i0,a)') '(f0.', decimals, ')'
    write(buffer, form) value
    buffer = adjustl(buffer)
    if (buffer(1:1) .eq. '.') then
       text = '0' // trim(buffer)
    else if (buffer(1:2) .eq. '-.') then
       text = '-0' // trim(buffer(2:))
    else
       text = trim(buffer)
    end if
  end function json_real_

  ! Kept from each configure the stream accepts.
  subroutine jtty_note_station(cfg)
    type(configure_fields), intent(in) :: cfg
    if (cfg%mycall_set .or. cfg%mycall_unreadable) then
       station_call_ = cfg%mycall_tx
       station_call_long_ = cfg%mycall_tx_long
       station_call_unreadable_ = cfg%mycall_unreadable
       station_call_set_ = .true.
    end if
    if (cfg%mygrid_set .or. cfg%mygrid_unreadable) then
       station_grid_ = cfg%mygrid_tx
       station_grid_long_ = cfg%mygrid_tx_long
       station_grid_set_ = .true.
    end if
  end subroutine jtty_note_station

  ! The station's call as the last accepted configure that named one gave it,
  ! blank before one has: long when it has more than the 64 characters call
  ! holds, unreadable when its JSON string does not decode (call is blank).
  subroutine encode_station_call(call, long, unreadable)
    character(len=64), intent(out) :: call
    logical,           intent(out) :: long, unreadable
    call = station_call_
    long = station_call_long_
    unreadable = station_call_unreadable_
  end subroutine encode_station_call

  ! Answer one pack (render .false.) or render request.
  subroutine jtty_encode(frame, render)
    character(len=*), intent(in) :: frame
    logical,          intent(in) :: render
    type(encode_request) :: req
    integer(c_int32_t) :: handle, status, segments, substituted, nsym,        &
         nframes, text_length, canonical_length, is_final, seg_substituted,   &
         key_length, detail_length, rendered
    integer(c_int32_t) :: tones(MAX_FRAMES * TX_FRAME_SYMBOLS)
    character(kind=c_char) :: frames(MAX_FRAMES * TX_FRAME_BITS), text(TX_TEXT), &
         canonical(TX_TEXT), key(32), detail(256)
    character(len=:), allocatable :: id, list, line
    integer(int64) :: total, samples
    integer :: seg, alloc_status
    logical :: ok

    if (render) then
       call parse_render_request(frame, req)
    else
       call parse_encode_request(frame, req)
    end if
    if (.not. req%id_ok) then
       call streaming_emit_error_code('invalid_request_id',                 &
            'pack and render need an integer id from -2147483648 to 2147483647')
       return
    end if
    id = itoa_(int(req%id, int64))
    if (len(req%problem) .gt. 0) then
       call emit_rejected_(id, 'bad_request', req%problem_key, req%problem)
       return
    end if

    handle = jtty_tx_encode(merge(1_c_int32_t, 0_c_int32_t, req%is_template), &
         req%text, len(req%text, c_int32_t), req%his_call,                    &
         len(req%his_call, c_int32_t), req%exchange, len(req%exchange, c_int32_t), &
         station_call_, station_length_(station_call_, station_call_set_,      &
         station_call_long_), station_grid_, station_length_(station_grid_,   &
         station_grid_set_, station_grid_long_), int(req%serial, c_int32_t),  &
         merge(1_c_int32_t, 0_c_int32_t, req%serial_given), int(req%report, c_int32_t), &
         merge(1_c_int32_t, 0_c_int32_t, req%report_given),                   &
         int(req%profile, c_int32_t), merge(1_c_int32_t, 0_c_int32_t, req%final))
    if (handle .eq. 0) call fatal_('no JTTY transmit context is available')
    status = jtty_tx_status(handle, segments, substituted)
    if (status .ne. TX_ENCODED) then
       call jtty_tx_error(handle, key, size(key, kind=c_int32_t), key_length,  &
            detail, size(detail, kind=c_int32_t), detail_length)
       call jtty_tx_destroy(handle)
       call emit_rejected_(id, tx_reason_(status), chars_(key, key_length),    &
            chars_(detail, detail_length))
       return
    end if

    ! Every check precedes the first line, so a rejected render sends no audio.
    total = 0
    do seg = 0, segments - 1
       nframes = 0
       is_final = 0
       nsym = jtty_tx_segment(handle, seg, tones, frames, nframes, text,     &
            text_length, canonical, canonical_length, is_final, seg_substituted)
       call frame_list_(frames, nframes, nsym, is_final .ne. 0, list, ok)
       if (.not. ok) then
          call jtty_tx_destroy(handle)
          call emit_rejected_(id, 'encoding_failed', '', 'the frames of segment ' // &
               itoa_(int(seg, int64)) // ' do not account for its tones')
          return
       end if
       total = total + int(nsym, int64) * NSPS * req%rate / 12000
    end do
    if (render .and. total .gt. int(MAX_RENDER_SECONDS, int64) * req%rate) then
       call jtty_tx_destroy(handle)
       call emit_rejected_(id, 'too_long', '', 'the audio is longer than 600 s')
       return
    end if
    if (render .and. .not. allocated(tx_samples_)) then
       allocate(tx_samples_(MAX_FRAMES * TX_FRAME_SYMBOLS * NSPS * 4), stat=alloc_status)
       if (alloc_status .ne. 0) call fatal_('no memory for transmit audio')
    end if

    do seg = 0, segments - 1
       nframes = 0
       is_final = 0
       nsym = jtty_tx_segment(handle, seg, tones, frames, nframes, text,     &
            text_length, canonical, canonical_length, is_final, seg_substituted)
       call frame_list_(frames, nframes, nsym, is_final .ne. 0, list, ok)
       line = '{"v":1,"t":"segment","id":' // id // ',"seg":' //              &
            itoa_(int(seg, int64)) // ',"text":"' //                         &
            escaped_(chars_(text, text_length)) // '","canonical":"' //       &
            escaped_(chars_(canonical, canonical_length)) // '","final":' //  &
            json_bool_(is_final .ne. 0) // ',"substituted":' //               &
            json_bool_(seg_substituted .ne. 0) // ',"frames":' // list //     &
            ',"seconds":' // json_real_(real(nsym, real64) * NSPS / 12000, 3)
       if (.not. render) then
          call emit_line_(line // '}')
          cycle
       end if
       samples = int(nsym, int64) * NSPS * req%rate / 12000
       call emit_line_(line // ',"samples":' // itoa_(samples) // '}')
       rendered = jtty_tx_render(tones, nsym, int(req%rate, c_int32_t),      &
            real(req%freq, c_float), tx_samples_, size(tx_samples_, kind=c_int32_t))
       if (rendered .ne. samples) call fatal_('a transmit segment did not render')
       call encode_emit_pcm(id, seg, tx_samples_(1:rendered))
    end do
    call jtty_tx_destroy(handle)
    call emit_line_('{"v":1,"t":"' // trim(merge('rendered', 'packed  ', render)) // &
         '","id":' // id // ',"segments":' // itoa_(int(segments, int64)) //  &
         ',"substituted":' // json_bool_(substituted .ne. 0) // '}')
  end subroutine jtty_encode

  ! A station value's length for jtty_tx_encode.
  function station_length_(value, set, long) result(length)
    character(len=*), intent(in) :: value
    logical,          intent(in) :: set, long
    integer(c_int32_t) :: length
    if (.not. set) then
       length = TX_UNSET
    else if (long) then
       length = TX_OVERLONG
    else
       length = len_trim(value, c_int32_t)
    end if
  end function station_length_

  ! samples as the pcm lines of request id's segment seg.
  subroutine encode_emit_pcm(id, seg, samples)
    character(len=*),   intent(in) :: id
    integer,            intent(in) :: seg
    integer(c_int16_t), intent(in) :: samples(:)
    integer :: first, seq
    seq = 0
    do first = 1, size(samples), PCM_LINE
       call emit_line_('{"v":1,"t":"pcm","id":' // id // ',"seg":' //       &
            itoa_(int(seg, int64)) // ',"seq":' // itoa_(int(seq, int64)) //    &
            ',"data":"' // base64_(samples(first:min(first + PCM_LINE - 1,    &
            size(samples)))) // '"}')
       seq = seq + 1
    end do
  end subroutine encode_emit_pcm

  subroutine encode_emit_rejected(id, reason, key, detail)
    character(len=*), intent(in) :: id, reason, key, detail
    call emit_rejected_(id, reason, key, detail)
  end subroutine encode_emit_rejected

  subroutine emit_rejected_(id, reason, key, detail)
    character(len=*), intent(in) :: id, reason, key, detail
    character(len=:), allocatable :: line
    line = '{"v":1,"t":"rejected","id":' // id // ',"reason":"' // reason // '"'
    if (len(key) .gt. 0) line = line // ',"key":"' // escaped_(key) // '"'
    call emit_line_(line // ',"detail":"' // escaped_(detail) // '"}')
  end subroutine emit_rejected_

  function tx_reason_(status) result(reason)
    integer(c_int32_t), intent(in) :: status
    character(len=:), allocatable :: reason
    select case (status)
    case (TX_EMPTY)
       reason = 'empty'
    case (TX_INVALID_RUNTIME)
       reason = 'invalid_runtime'
    case (TX_TOO_LONG)
       reason = 'too_long'
    case (TX_BAD_REQUEST)
       reason = 'bad_request'
    case (TX_NOT_CONFIGURED)
       reason = 'not_configured'
    case (TX_MISSING)
       reason = 'missing'
    case default                        ! TX_ENCODING_FAILED
       reason = 'encoding_failed'
    end select
  end function tx_reason_

  ! The frames as a JSON array of {"text":S}; ok is .false. when they do not
  ! account for nsym tones or one is no source atom. ends: the segment ends
  ! its message.
  subroutine frame_list_(frames, nframes, nsym, ends, list, ok)
    character(kind=c_char),        intent(in)  :: frames(:)
    integer(c_int32_t),            intent(in)  :: nframes, nsym
    logical,                       intent(in)  :: ends
    character(len=:), allocatable, intent(out) :: list
    logical,                       intent(out) :: ok
    character(len=TX_FRAME_BITS) :: bits
    character(len=:), allocatable :: shown
    integer :: i, j

    list = '['
    ok = nframes .gt. 0 .and. nframes .le. MAX_FRAMES .and.                  &
         nframes * TX_FRAME_SYMBOLS .eq. nsym
    if (.not. ok) return
    do i = 1, nframes
       do j = 1, TX_FRAME_BITS
          bits(j:j) = frames((i - 1) * TX_FRAME_BITS + j)
       end do
       call frame_text_(bits, ends .and. i .eq. nframes, shown, ok)
       if (.not. ok) return
       if (i .gt. 1) list = list // ','
       list = list // '{"text":"' // escaped_(shown) // '"}'
    end do
    list = list // ']'
  end subroutine frame_list_

  ! A frame as the receiver renders it: a structured atom's text, or a text
  ! atom's five characters, whose trailing spaces are part of the message
  ! unless the frame ends it.
  subroutine frame_text_(bits, last, text, ok)
    character(len=*),              intent(in)  :: bits
    logical,                       intent(in)  :: last
    character(len=:), allocatable, intent(out) :: text
    logical,                       intent(out) :: ok
    type(jtty_source_atom) :: atom
    character(len=80) :: rendered
    logical :: eom

    text = ''
    call unpack_jtty_atom(bits, atom, ok, eom)
    if (ok) call render_jtty_atom(atom, rendered, ok)
    if (.not. ok) return
    if (atom%kind .eq. JTTY_ATOM_TEXT5 .and. .not. last) then
       text = rendered(1:5)
    else
       text = trim(rendered)
    end if
  end subroutine frame_text_

  ! s as JSON string contents; its trailing spaces, which the stream's
  ! escaper drops, are kept.
  function escaped_(s) result(text)
    character(len=*), intent(in) :: s
    character(len=:), allocatable :: text
    character(len=6 * len(s)) :: buffer
    integer :: n
    call streaming_emit_json_escape(s, buffer, n)
    text = buffer(1:n) // repeat(' ', len(s) - len_trim(s))
  end function escaped_

  function chars_(c, n) result(text)
    character(kind=c_char), intent(in) :: c(:)
    integer(c_int32_t),     intent(in) :: n
    character(len=:), allocatable :: text
    integer :: i
    allocate(character(len=max(0, min(int(n), size(c)))) :: text)
    do i = 1, len(text)
       text(i:i) = c(i)
    end do
  end function chars_

  function json_bool_(flag) result(text)
    logical, intent(in) :: flag
    character(len=:), allocatable :: text
    if (flag) then
       text = 'true'
    else
       text = 'false'
    end if
  end function json_bool_

  ! samples as int16 little-endian bytes in base64 (RFC 4648 section 4).
  function base64_(samples) result(text)
    integer(c_int16_t), intent(in) :: samples(:)
    character(len=:), allocatable :: text
    character(len=*), parameter :: DIGITS =                                  &
         'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
    integer :: nbytes, i, j, n, group, k

    nbytes = 2 * size(samples)
    allocate(character(len=4 * ((nbytes + 2) / 3)) :: text)
    k = 0
    do i = 0, nbytes - 1, 3
       n = min(3, nbytes - i)
       group = 0
       do j = 0, 2
          group = ishft(group, 8)
          if (j .lt. n) group = ior(group, sample_byte_(i + j))
       end do
       do j = 0, 3
          if (j .le. n) then
             text(k + j + 1:k + j + 1) = DIGITS(iand(ishft(group, -6 * (3 - j)), 63) + 1: &
                  iand(ishft(group, -6 * (3 - j)), 63) + 1)
          else
             text(k + j + 1:k + j + 1) = '='
          end if
       end do
       k = k + 4
    end do

  contains

    ! Byte b, from 0, of samples as int16 little-endian.
    integer function sample_byte_(b)
      integer, intent(in) :: b
      integer :: value
      value = int(samples(b / 2 + 1))
      if (mod(b, 2) .eq. 0) then
         sample_byte_ = iand(value, 255)
      else
         sample_byte_ = iand(ishft(value, -8), 255)
      end if
    end function sample_byte_

  end function base64_

end module streaming_jtty
