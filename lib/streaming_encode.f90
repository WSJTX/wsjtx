! SPDX-License-Identifier: GPL-3.0-or-later
! Encode requests in the stream session: lib/streaming_io.f90 hands each pack
! and render frame to stream_encode, in any mode. A request without "mode",
! or with "mode":"JTTY" (case aside), is JTTY's, stated and answered in
! lib/streaming_jtty.f90. With another mode it is a slot mode's, stated here:
! the message as the GUI generates it for a transmission
! (MainWindow::guiUpdate, widgets/mainwindow.cpp) and the audio its Modulator
! plays for it (Modulator/Modulator.cpp, lib/stream_slot_audio.cpp). ready
! lists the modes pack and render take in "encodes", JTTY among them, whose
! replies are lib/streaming_jtty.f90's and have no encoded line.
!
! An encode-only client opens the session as any other, with the 8-byte header
! of 12 kHz mono int16 audio (lib/streaming_io.f90); any other header ends the
! codec with an error line that carries "msg" rather than "code", and exit 1.
! A frame whose "t" the codec does not know ("PACK" too) gets no reply, even
! with an id.
!
! A slot-mode request:
!   {"t":"pack","id":I,"mode":M,"text":S,...}
!   {"t":"render","id":I,"mode":M,"text":S,...,"freq":F,"rate":R}
! I as in JTTY's requests. M, case aside: FT8, FT4, JT4, JT9, JT65, Q65, FST4,
! FST4W, WSPR or MSK144. The codec takes S less its outer spaces, which the
! GUI's Tx box keeps (and its FST4 and MSK144 encoders then drop characters
! after leading blanks; the codec does not follow it there). If that is
! longer than the 37 bytes the box gives the mode's encoder (22 for JT4,
! JT9, JT65 and WSPR) it is refused, else it is encoded as the GUI encodes
! that box, with a to z in upper case and nothing in it expanded. For
! WSPR, S is one of the messages the GUI builds from the station's call, grid
! and power: "CALL GRID4 DBM" (a call of three to six characters), "PFX/CALL
! DBM" or "CALL/SFX DBM" (a prefix of one to three characters, a suffix of one
! character or two digits), or "<CALL> GRID6 DBM"; DBM one of the GUI's
! powers, 0, 3, 7, 10, ... 60. FST4W's are the same but the last,
! "<CALL> GRID6", and must encode as its WSPR-type messages do.
! The other keys, each refused (bad_request) in a mode it does not name:
!   submode   JT4 A to G, JT9 A to H, JT65 A to C, Q65 A to C (trperiod 15),
!             A to D (30), A to E (60 and 300), A to F (120): a letter of
!             either case, or a JSON integer, 0 for A and so on. Required.
!   trperiod  FST4 15, 30, 60, 120, 300, 900 or 1800; FST4W 120, 300, 900 or
!             1800; Q65 15, 30, 60, 120 or 300; MSK144 5, 10, 15 or 30: a JSON
!             integer of seconds. Required.
!   spacing   FT8, JT9, FST4, FST4W and WSPR: 1 (default), 2 or 4, a JSON
!             integer, the GUI's x2 and x4 tone spacing.
!   copied    Q65: true sets the message's spare 78th bit, the GUI's Q65
!             pileup flag that it copied the station it answers. Default
!             false.
! A render adds F, the lowest tone, a JSON number from 200 to 5000 Hz (for
! MSK144 1000, whose tones are always 1000 and 2000 Hz), and R, the JSON
! integer 12000 or 48000. At 12000 Hz the highest tone (the table below) must
! be below 6000 Hz, or the render is refused with key freq. Any other key, a
! misspelt or mis-cased one included, is refused with that key as written
! (keys are matched without escapes; a byte not UTF-8 is reported as '?'),
! as are JTTY's keys and, on a pack, freq and rate; configure, unlike pack
! and render, ignores keys it does not know.
!
! A request's reply lines are consecutive, each carries its id, and the last
! is packed, rendered or rejected. pack answers encoded then packed; render
! answers encoded, the pcm lines of the audio, then rendered:
!   encoded   {"v":1,"t":"encoded","id":I,"mode":M,"msgsent":S,"tones":[T,...],
!              "type":N,"fallback":B,"submode":L,"trperiod":P,"spacing":N,
!              "copied":B,"freq":F,"rate":R,"samples":N}
!   pcm       {"v":1,"t":"pcm","id":I,"seg":0,"seq":Q,"data":S}
!   packed    {"v":1,"t":"packed","id":I}
!   rendered  {"v":1,"t":"rendered","id":I}
!   rejected  {"v":1,"t":"rejected","id":I,"reason":R,"key":K,"detail":S}
! M is the mode's name in upper case. msgsent is the message as the GUI shows
! and logs it, which is what a receiver decodes; compare it with S, for it can
! differ with no flag (JT4, JT9 and JT65 drop what they cannot send and cut
! free text to 13 characters). For WSPR it is the message as WSPR's decoder
! reads it back (lib/wqdecode.f90), with the hashed call of "<CALL> GRID6
! DBM" as given; a message that would read back otherwise is refused. One
! exception: a call beginning 3DA0, which WSPR sends as 3D0, reads back and
! shows in the GUI as 3DA0, while wsprd prints 3D0 (and <...> for its hash).
! tones
! are the channel symbols the GUI keys. type, for JT4, JT9, JT65 and MSK144
! only, is the GUI's message type: for JT4, JT9 and JT65 1 standard, 2 to 5
! with a prefix or suffix, 6 free text, and for JT65 7 a shorthand (RO, RRR
! or 73 alone); for MSK144 1, or 7 for a short message "<CALL1 CALL2> RPT".
! fallback, for every mode but JT4, JT9, JT65 and WSPR, says the message did
! not encode as written and its first 13 characters went as free text
! instead, as the GUI sends it. submode (its letter), trperiod, spacing and
! copied are those applied, each where the mode has it; a render adds freq,
! rate and samples. pcm lines are as JTTY's render writes them (int16
! little-endian mono, base64, 16384 samples a line but the last).
!
! Each mode's samples per symbol at 12 kHz, tone spacing, symbols, and its
! highest tone above the lowest:
!   FT8     1920; 12000/1920 = 6.25 Hz (x2 12.5, x4 25); 79; 7 spacings
!   FT4     576; 12000/576 Hz; 103, with a ramp symbol either side; 3
!   JT4     2520*12000/11025; 4.375 Hz times 1, 2, 4, 9, 18, 36, 72 for A to
!           G; 206; 3
!   JT9     6912; 12000/6912 Hz times 2**submode and spacing; 85; 8 (the slow
!           form at every submode: the GUI's fast JT9 is not encoded here)
!   JT65    4096*12000/11025; 11025/4096 Hz times 1, 2, 4 for A to C; 126; 65
!   Q65     1800, 3600, 7200, 16000, 41472 for 15, 30, 60, 120, 300 s;
!           12000/that times 2**submode; 85; 64
!   FST4,   720, 1680, 3888, 8200, 21504, 66560, 134400 for 15, 30, 60, 120,
!   FST4W   300, 900, 1800 s; 12000/that times spacing; 160; 3
!   WSPR    8192; 12000/8192 Hz times spacing; 162; 3
!   MSK144  6; tones at 1000 and 2000 Hz; 144, or 40 for a short message
!
! The audio is the GUI's, sample for sample: at 48000 Hz the samples its
! Modulator plays for the message from its first to the last it commits,
! full scale, before the GUI's output attenuation; at 12000 Hz every fourth
! of them from the first. At 48000 Hz that is the samples of the GUI's
! symbols, rounded down, and one more; at 12000 Hz a quarter of it, rounded
! up. MSK144 instead repeats its message for the period less 0.5 s, and one
! sample more. FT8, FT4, FST4 and FST4W are the GUI's shaped waveforms; JT4,
! JT9, JT65, WSPR, Q65, MSK144, and FT8 with spacing 2 or 4, are
! phase-continuous tones whose amplitude decays over the last 0.017 symbol.
! The GUI starts the audio 1 s into its period (0.5 s for FT8, FST4 at 15 s
! and Q65 at 15 and 30 s, 0.3 s for FT4, none for MSK144); timing is the
! client's.
!
! Each R and its K:
!   bad_request      the key at fault; none when the request is not a flat
!                    object. For text: the message does not encode in M (the
!                    GUI's *** bad message ***, which it does not transmit),
!                    or a hashed call in it collides (below); it begins with
!                    @ (the GUI's single tones, not encoded here); or it is
!                    not a WSPR message of the forms above.
!   empty            none: text is blank
!   too_long         text: it is longer than the mode takes; none: a render
!                    of more than 600 s of audio, which FST4 and FST4W give
!                    at 900 and 1800 s
!   encoding_failed  none
! A request without such an id gets error invalid_request_id.
!
! A hashed call (<CALL>) is resolved through the transmitter's hash tables,
! which the codec keeps as the GUI keeps its own: one set for FT8 and FT4 and
! another for FST4, FST4W, Q65 and MSK144, each holding the calls of every
! message encoded with it since the codec started, by pack or render, and
! never of a decode. A message whose hashed call has the hash of a different
! call already in its set does not encode, in the GUI and here; the refusal's
! detail says so, and a message that carries the call in full, encoded
! first, replaces the other. (The GUI's second set also takes the calls of
! every 77-bit decode it shows, Decoder/decodedtext.cpp, and of its own
! MSK144 decoder.)
!
! A slot-mode request is answered as JTTY's are, in order between frames
! (lib/streaming_jtty.f90), on the thread that decodes. On live audio its time
! delays the decoder: a pass it holds up runs late, and a pass whose trigger
! falls before that one ends is skipped as busy (pass_skipped), which the GUI,
! building its audio outside its decoder, never does. A render takes from a fraction of a second (FT8) to
! seconds (JT65, WSPR or FST4 at 48000 Hz), so send one between a period's
! decode_finished and its next trigger: for FT8 the first early pass, 11.81 s
! into the period (half-symbol 41).

module streaming_encode
  use, intrinsic :: iso_c_binding, only: c_int16_t, c_int32_t, c_double, c_char, c_int
  use, intrinsic :: iso_fortran_env, only: int64, output_unit
  use streaming_control, only: slot_request, parse_slot_request,              &
       encode_request_mode, MODE_JTTY, MODE_WSPR
  use streaming_emit, only: streaming_emit_error_code, streaming_emit_json_escape
  use streaming_jtty, only: jtty_encode, encode_emit_rejected, encode_emit_pcm
  use packjt77, only: pack77_state, pack77_for_state, pack77_options,          &
       PACK77_STATUS_ENCODED
  use packjt, only: packcall
  use ft8_codec_context, only: genft8_for_state
  use ft4_codec, only: genft4_for_state
  use fst4_tx, only: genfst4_for_state
  use q65_tx, only: genq65_for_state
  use msk144_tx, only: genmsk_128_90_for_state
  implicit none
  private

  public :: stream_encode

  integer, parameter :: MAX_RENDER_SECONDS = 600
  character(len=*), parameter :: BAD_MESSAGE = '*** bad message ***'
  ! The GUI's two sets of transmit hash tables: FT8's and FT4's
  ! (lib/77bit/decoder_codec_context.f90), and packjt77's own, which its
  ! FST4, Q65 and MSK144 encoders use.
  type(pack77_state), save, target :: ft_tables_, tables_

  interface
     ! lib/gen65.f90
     subroutine gen65(msg00, ichk, msgsent0, itone, itype) bind(C, name='gen65')
       import :: c_char, c_int
       character(kind=c_char) :: msg00(23), msgsent0(23)
       integer(c_int) :: ichk, itone(126), itype
     end subroutine gen65
     ! lib/stream_slot_audio.cpp
     function stream_slot_samples(mode, nsym, submode, spacing, trperiod, rate) &
          result(count) bind(C, name='stream_slot_samples')
       import :: c_int32_t
       integer(c_int32_t), value :: mode, nsym, submode, spacing, trperiod, rate
       integer(c_int32_t) :: count
     end function stream_slot_samples
     function stream_slot_top_tone(mode, submode, spacing, trperiod, freq)    &
          result(top) bind(C, name='stream_slot_top_tone')
       import :: c_int32_t, c_double
       integer(c_int32_t), value :: mode, submode, spacing, trperiod
       real(c_double), value :: freq
       real(c_double) :: top
     end function stream_slot_top_tone
     function stream_slot_render(mode, tones, nsym, submode, spacing, trperiod, &
          freq, rate, samples, capacity) result(count)                         &
          bind(C, name='stream_slot_render')
       import :: c_int32_t, c_int16_t, c_double
       integer(c_int32_t), value :: mode, nsym, submode, spacing, trperiod,    &
            rate, capacity
       integer(c_int32_t), intent(in) :: tones(*)
       real(c_double), value :: freq
       integer(c_int16_t), intent(out) :: samples(*)
       integer(c_int32_t) :: count
     end function stream_slot_render
  end interface

contains

  ! Answer one pack (render .false.) or render request.
  subroutine stream_encode(frame, render)
    character(len=*), intent(in) :: frame
    logical,          intent(in) :: render
    integer :: mode
    logical :: ok

    call encode_request_mode(frame, mode, ok)
    if (ok .and. mode .eq. MODE_JTTY) then
       call jtty_encode(frame, render)
    else
       call slot_encode_(frame, render)
    end if
  end subroutine stream_encode

  subroutine slot_encode_(frame, render)
    character(len=*), intent(in) :: frame
    logical,          intent(in) :: render
    type(slot_request) :: req
    type(pack77_state), target :: fresh_ft, fresh
    character(len=37) :: message, msgsent, unused
    character(len=22) :: decoded
    character(len=:), allocatable :: id, line, text
    integer(c_int32_t) :: tones(206), unused_tones(206)
    integer(c_int16_t), allocatable :: samples(:)
    integer(c_int32_t) :: count
    integer :: ntones, itype, i, alloc_status, first, last, limit, n_unused, t_unused
    real(c_double) :: top
    logical :: fallback, ok

    count = 0
    call parse_slot_request(frame, render, req)
    if (.not. req%id_ok) then
       call streaming_emit_error_code('invalid_request_id',                 &
            'pack and render need an integer id from -2147483648 to 2147483647')
       return
    end if
    id = itoa_(int(req%id, int64))
    if (len(req%problem) .gt. 0) then
       call encode_emit_rejected(id, 'bad_request', req%problem_key, req%problem)
       return
    end if

    ! The text less its outer blanks, so that every encoder takes the same one.
    first = verify(req%text, ' ')
    last = verify(req%text, ' ', back=.true.)
    if (first .eq. 0) then
       call encode_emit_rejected(id, 'empty', '', 'the message is blank')
       return
    end if
    text = req%text(first:last)
    limit = 37
    if (any(req%mode .eq. [4, 9, 65, MODE_WSPR])) limit = 22
    if (len(text) .gt. limit) then
       call encode_emit_rejected(id, 'too_long', 'text', 'the message is longer ' // &
            'than ' // itoa_(int(limit, int64)) // ' bytes, all ' //             &
            mode_name_(req%mode) // ' takes')
       return
    end if
    call tx_buffer_(text, message)
    select case (req%mode)
    case (4, 9, 65, 66, 144)
       if (message(1:1) .eq. '@') then
          call encode_emit_rejected(id, 'bad_request', 'text', 'a message ' //  &
               'beginning with @ is a single tone, which this does not encode')
          return
       end if
    case (MODE_WSPR)
       if (.not. wspr_message_ok_(message(1:22))) then
          call encode_emit_rejected(id, 'bad_request', 'text', 'not a WSPR ' // &
               'message: CALL GRID4 DBM, PFX/CALL DBM, CALL/SFX DBM or <CALL> GRID6 DBM')
          return
       end if
    end select

    if (render) then
       if (req%rate .eq. 12000) then
          top = stream_slot_top_tone(req%mode, req%submode, req%spacing,       &
               req%trperiod, real(req%freq, c_double))
          if (top .ge. 6000.d0) then
             call encode_emit_rejected(id, 'bad_request', 'freq', 'at 12000 Hz ' // &
                  'the top tone, ' // itoa_(nint(top, int64)) // ' Hz, would ' //  &
                  'fold back; it must be below 6000 Hz')
             return
          end if
       end if
       count = stream_slot_samples(req%mode, 144, req%submode, req%spacing,   &
            req%trperiod, req%rate)
       if (count .le. 0) then
          call encode_emit_rejected(id, 'encoding_failed', '', 'the audio has no length')
          return
       end if
       if (int(count, int64) .gt. int(MAX_RENDER_SECONDS, int64) * req%rate) then
          call encode_emit_rejected(id, 'too_long', '', 'the audio is longer than 600 s')
          return
       end if
    end if

    call generate_(req, message, ft_tables_, tables_, msgsent, tones, ntones,   &
         itype, fallback)
    if (trim(msgsent) .eq. BAD_MESSAGE) then
       ! With no calls in its tables, a message whose hashed call collides
       ! with one already there would encode.
       if (req%mode .ne. 4 .and. req%mode .ne. 9 .and. req%mode .ne. 65 .and. &
            req%mode .ne. MODE_WSPR) then
          call generate_(req, message, fresh_ft, fresh, unused, unused_tones,   &
               n_unused, t_unused, ok)
          if (trim(unused) .ne. BAD_MESSAGE) then
             line = 'FST4, FST4W, Q65 or MSK144'
             if (req%mode .eq. 8 .or. req%mode .eq. 5) line = 'FT8 or FT4'
             call encode_emit_rejected(id, 'bad_request', 'text', 'a hashed ' // &
                  'call in it has the hash of another call already sent in ' // &
                  line // '; send the call in full in a message first, which ' // &
                  'replaces the other')
             return
          end if
       end if
       call encode_emit_rejected(id, 'bad_request', 'text', 'the message does ' // &
            'not encode in ' // mode_name_(req%mode) // ' (the GUI''s ' //      &
            BAD_MESSAGE // ', which it does not transmit)')
       return
    end if
    if (req%mode .eq. MODE_WSPR) then
       call wspr_decoded_(message(1:22), decoded, ok)
       if (.not. ok) then
          call encode_emit_rejected(id, 'bad_request', 'text', 'it would ' //    &
               'decode as "' // escaped_(decoded) // '", not as written')
          return
       end if
       msgsent = decoded
    end if

    line = '{"v":1,"t":"encoded","id":' // id // ',"mode":"' //                &
         mode_name_(req%mode) // '","msgsent":"' // escaped_(msgsent) //        &
         '","tones":['
    do i = 1, ntones
       if (i .gt. 1) line = line // ','
       line = line // itoa_(int(tones(i), int64))
    end do
    line = line // ']'
    select case (req%mode)
    case (4, 9, 65, 144)
       line = line // ',"type":' // itoa_(int(itype, int64))
    end select
    select case (req%mode)
    case (8, 5, 240, 241, 66, 144)
       line = line // ',"fallback":' // trim(merge('true ', 'false', fallback))
    end select
    if (req%submode .ge. 0) line = line // ',"submode":"' //                   &
         achar(iachar('A') + req%submode) // '"'
    if (req%trperiod .gt. 0) line = line // ',"trperiod":' //                  &
         itoa_(int(req%trperiod, int64))
    if (any(req%mode .eq. [8, 9, 240, 241, MODE_WSPR])) line = line //          &
         ',"spacing":' // itoa_(int(req%spacing, int64))
    if (req%mode .eq. 66) line = line // ',"copied":' //                       &
         trim(merge('true ', 'false', req%copied))
    if (.not. render) then
       call emit_line_(line // '}')
       call emit_line_('{"v":1,"t":"packed","id":' // id // '}')
       return
    end if
    line = line // ',"freq":' // number_(req%freq) // ',"rate":' //            &
         itoa_(int(req%rate, int64))

    allocate(samples(count), stat=alloc_status)
    if (alloc_status .eq. 0) count = stream_slot_render(req%mode, tones, ntones, &
         req%submode, req%spacing, req%trperiod, real(req%freq, c_double),      &
         req%rate, samples, count)
    if (alloc_status .ne. 0 .or. count .ne. size(samples)) then
       call encode_emit_rejected(id, 'encoding_failed', '', 'the audio did not render')
       return
    end if
    call emit_line_(line // ',"samples":' // itoa_(int(count, int64)) // '}')
    call encode_emit_pcm(id, 0, samples)
    call emit_line_('{"v":1,"t":"rendered","id":' // id // '}')
  end subroutine slot_encode_

  ! The GUI's message buffer for text: MainWindow::ba2msg.
  subroutine tx_buffer_(text, message)
    character(len=*),  intent(in)  :: text
    character(len=37), intent(out) :: message
    integer :: i
    message = text
    do i = 1, 37
       if (message(i:i) .ge. 'a' .and. message(i:i) .le. 'z')                 &
            message(i:i) = achar(iachar(message(i:i)) - 32)
    end do
  end subroutine tx_buffer_

  ! As MainWindow::guiUpdate generates message in req's mode, with the
  ! transmit tables ft_tables (FT8 and FT4) and tables (FST4, FST4W, Q65 and
  ! MSK144): msgsent and the tones; itype the message type its generator
  ! gives, and fallback whether the 13-character free-text fallback applied.
  subroutine generate_(req, message, ft_tables, tables, msgsent, tones, ntones, &
       itype, fallback)
    type(slot_request), intent(in)  :: req
    character(len=37),  intent(in)  :: message
    type(pack77_state), target, intent(inout) :: ft_tables, tables
    character(len=37),  intent(out) :: msgsent
    integer(c_int32_t), intent(out) :: tones(206)
    integer,            intent(out) :: ntones, itype
    logical,            intent(out) :: fallback
    character(len=22) :: message22, msgsent22
    character(kind=c_char) :: msg00(23), msgsent0(23)
    character(len=77) :: c77
    integer(kind=1) :: msgbits77(77), msgbits101(101)
    integer :: ichk, i3, n3, iwspr, iflag, status, i
    integer(c_int) :: ichk_c, itype_c, itone65(126)
    type(pack77_state), pointer :: knowledge

    msgsent = ' '
    tones = 0
    ntones = 0
    itype = 0
    ichk = 0
    message22 = message(1:22)

    ! The free-text fallback applies when the message, as the encoder takes
    ! it, does not encode exactly with the tables as they stand before it.
    ! FST4W has none, and a short MSK144 message (below) is not a 77-bit one.
    status = PACK77_STATUS_ENCODED
    select case (req%mode)
    case (8)
       call pack77_for_state(ft_tables, message, i3, n3, c77,                &
            pack77_options(record_tx_hashes=.false.), status)
    case (5)
       call pack77_for_state(ft_tables, adjustl(cut_(message)), i3, n3, c77, &
            pack77_options(record_tx_hashes=.false.), status)
    case (240, 144)
       call pack77_for_state(tables, adjustl(cut_(message)), i3, n3, c77,    &
            pack77_options(record_tx_hashes=.false.), status)
    case (66)
       call pack77_for_state(tables, message, i3, n3, c77,                   &
            pack77_options(record_tx_hashes=.false.), status)
    end select

    select case (req%mode)
    case (4)
       call gen4(message22, ichk, msgsent22, tones, itype)
       msgsent = msgsent22
       ntones = 206
    case (9)
       call gen9(message22, ichk, msgsent22, tones, itype)
       msgsent = msgsent22
       ntones = 85
    case (65)
       do i = 1, 22
          msg00(i) = message22(i:i)
       end do
       msg00(23) = achar(0)
       ichk_c = 0
       call gen65(msg00, ichk_c, msgsent0, itone65, itype_c)
       do i = 1, 22
          msgsent(i:i) = msgsent0(i)
       end do
       tones(1:126) = itone65
       itype = itype_c
       ntones = 126
    case (MODE_WSPR)
       call genwspr(message22, msgsent22, tones)
       msgsent = msgsent22
       ntones = 162
    case (144)
       call genmsk_128_90_for_state(message, ichk, msgsent, tones, itype, tables)
       ntones = merge(40, 144, tones(41) .lt. 0)
    case (8)
       call genft8_for_state(ft_tables, message, i3, n3, msgsent, msgbits77, tones(1:79))
       ntones = 79
    case (5)
       call genft4_for_state(ft_tables, message, ichk, msgsent, msgbits77, tones(1:103))
       ntones = 103
    case (240, 241)
       iwspr = merge(1, 0, req%mode .eq. 241)
       call genfst4_for_state(message, ichk, msgsent, msgbits101, tones(1:160), iwspr, tables)
       ntones = 160
    case (66)
       i3 = -1
       n3 = -1
       iflag = merge(1, 0, req%copied)
       knowledge => tables
       call genq65_for_state(message, ichk, msgsent, tones(1:85), i3, n3, iflag, knowledge)
       ntones = 85
    end select

    fallback = status .ne. PACK77_STATUS_ENCODED .and.                       &
         trim(msgsent) .ne. BAD_MESSAGE .and. .not. (req%mode .eq. 144 .and. ntones .eq. 40)
  end subroutine generate_

  ! msg, a WSPR message the check above accepts, as wsprd would decode it, and
  ! ok when that is msg; a hashed call decodes from a receiver's own table,
  ! so it is taken as msg gives it.
  subroutine wspr_decoded_(msg, decoded, ok)
    character(len=22), intent(in)  :: msg
    character(len=22), intent(out) :: decoded
    logical,           intent(out) :: ok
    integer(kind=1) :: data0(11)
    integer :: ntype, i
    call wqencode(msg, ntype, data0)
    call wqdecode(data0, decoded, ntype)
    if (msg(1:1) .eq. '<' .and. decoded(1:1) .eq. '<') then
       i = index(decoded, '>')
       if (i .gt. 0) decoded = msg(1:index(msg, '>')) // decoded(i + 1:)
    end if
    ok = decoded .eq. msg
  end subroutine wspr_decoded_

  ! s up to its first NUL, as the FT4, FST4 and MSK144 encoders take it.
  function cut_(s) result(t)
    character(len=37), intent(in) :: s
    character(len=37) :: t
    integer :: i
    t = s
    i = index(t, achar(0))
    if (i .gt. 0) t(i:) = ' '
  end function cut_

  ! Whether msg is a WSPR message of a form the GUI builds (MainWindow::
  ! WSPR_message), which wqencode encodes as it reads it.
  logical function wspr_message_ok_(msg)
    character(len=22), intent(in) :: msg
    character(len=22) :: words(4)
    character(len=6) :: call6
    integer :: nwords, slash, i, ncall
    logical :: text

    wspr_message_ok_ = .false.
    if (msg(1:1) .eq. ' ' .or. index(trim(msg), '  ') .gt. 0) return
    nwords = 0
    words = ' '
    do i = 1, len_trim(msg)
       if (msg(i:i) .eq. ' ') then
          nwords = nwords + 1
          if (nwords .ge. size(words)) return
       else
          words(nwords + 1) = trim(words(nwords + 1)) // msg(i:i)
       end if
    end do
    nwords = nwords + 1
    if (.not. power_(words(nwords))) return
    slash = index(words(1), '/')
    if (nwords .eq. 3 .and. words(1)(1:1) .ne. '<') then
       ! A standard call and a four-character grid; wsprd re-encodes what it
       ! decodes, and takes a shorter call for no message.
       if (slash .gt. 0 .or. len_trim(words(1)) .lt. 3 .or.                  &
            len_trim(words(1)) .gt. 6) return
       if (.not. grid_(words(2), 4)) return
       call6 = words(1)(1:6)
       call packcall(call6, ncall, text)
       wspr_message_ok_ = .not. text
    else if (nwords .eq. 2) then
       ! A compound call: a prefix of one to three characters, or a suffix of
       ! one character or two digits.
       if (slash .lt. 2 .or. index(words(1)(slash + 1:), '/') .gt. 0) return
       if (.not. alnum_(words(1)(1:slash - 1)) .or.                         &
            .not. alnum_(words(1)(slash + 1:len_trim(words(1))))) return
       ! wqencode takes two characters after '/' as a numeric suffix.
       if (len_trim(words(1)) - slash .eq. 2 .and.                          &
            verify(words(1)(slash + 1:slash + 2), '0123456789') .ne. 0) return
       if (len_trim(words(1)) - slash .le. 2) then
          call6 = words(1)(1:slash - 1)
          call packcall(call6, ncall, text)
          wspr_message_ok_ = .not. text .and. slash .le. 7
       else if (slash .le. 4) then
          call6 = words(1)(slash + 1:)
          call packcall(call6, ncall, text)
          wspr_message_ok_ = .not. text .and. len_trim(words(1)) - slash .le. 6
       end if
    else if (nwords .eq. 3) then
       ! A hashed call and a six-character grid.
       i = len_trim(words(1))
       if (words(1)(i:i) .ne. '>' .or. i .lt. 5 .or. i .gt. 14) return
       if (verify(words(1)(2:i - 1), 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/') &
            .ne. 0) return
       wspr_message_ok_ = grid_(words(2), 6)
    end if

  contains

    ! A power as the GUI writes it, which wqencode reads; one the GUI does
    ! not offer reads back changed, and wspr_decoded_ refuses it.
    logical function power_(word)
      character(len=*), intent(in) :: word
      power_ = len_trim(word) .ge. 1 .and. len_trim(word) .le. 2 .and.        &
           verify(trim(word), '0123456789') .eq. 0 .and.                      &
           .not. (len_trim(word) .eq. 2 .and. word(1:1) .eq. '0')
    end function power_

    logical function grid_(word, length)
      character(len=*), intent(in) :: word
      integer,          intent(in) :: length
      grid_ = len_trim(word) .eq. length
      if (.not. grid_) return
      grid_ = verify(word(1:2), 'ABCDEFGHIJKLMNOPQR') .eq. 0 .and.           &
           verify(word(3:4), '0123456789') .eq. 0
      if (length .eq. 6) grid_ = grid_ .and.                                 &
           verify(word(5:6), 'ABCDEFGHIJKLMNOPQRSTUVWX') .eq. 0
    end function grid_

    logical function alnum_(word)
      character(len=*), intent(in) :: word
      alnum_ = len(word) .gt. 0 .and.                                        &
           verify(word, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789') .eq. 0
    end function alnum_

  end function wspr_message_ok_

  function mode_name_(mode) result(name)
    integer, intent(in) :: mode
    character(len=:), allocatable :: name
    select case (mode)
    case (8)
       name = 'FT8'
    case (5)
       name = 'FT4'
    case (4)
       name = 'JT4'
    case (9)
       name = 'JT9'
    case (65)
       name = 'JT65'
    case (66)
       name = 'Q65'
    case (240)
       name = 'FST4'
    case (241)
       name = 'FST4W'
    case (144)
       name = 'MSK144'
    case default
       name = 'WSPR'
    end select
  end function mode_name_

  function escaped_(s) result(text)
    character(len=*), intent(in) :: s
    character(len=:), allocatable :: text
    character(len=6 * len(s)) :: buffer
    integer :: n
    call streaming_emit_json_escape(s, buffer, n)
    text = buffer(1:n)
  end function escaped_

  ! value as a JSON number.
  function number_(value) result(text)
    real(8), intent(in) :: value
    character(len=:), allocatable :: text
    character(len=40) :: buffer
    integer :: n
    write(buffer, '(f0.9)') value
    n = len_trim(buffer)
    do while (buffer(n:n) .eq. '0')
       n = n - 1
    end do
    if (buffer(n:n) .eq. '.') n = n - 1
    text = buffer(1:n)
  end function number_

  function itoa_(value) result(text)
    integer(int64), intent(in) :: value
    character(len=:), allocatable :: text
    character(len=24) :: buffer
    write(buffer, '(i0)') value
    text = trim(buffer)
  end function itoa_

  subroutine emit_line_(line)
    character(len=*), intent(in) :: line
    write(output_unit, '(a)') line
    flush(output_unit)
  end subroutine emit_line_

end module streaming_encode
