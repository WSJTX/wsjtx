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
! FST4W, WSPR, MSK144, ECHO (the GUI's Echo mode), TUNE (its Tune button), or
! FOX and SUPERFOX (its FT8 Fox, below).
! The codec takes S less its outer spaces, which the GUI's Tx box keeps (and
! its FST4 and MSK144 encoders then drop characters after leading blanks; the
! codec does not follow it there). If that is
! longer than the 37 bytes the box gives the mode's encoder (22 for JT4,
! JT9, JT65 and WSPR) it is refused, else it is encoded as the GUI encodes
! that box, with a to z in upper case and nothing in it expanded. For
! WSPR, S is one of the messages the GUI builds from the station's call, grid
! and power: "CALL GRID4 DBM" (a call of three to six characters), "PFX/CALL
! DBM" or "CALL/SFX DBM" (a prefix of one to three characters, a suffix of one
! character or two digits), or "<CALL> GRID6 DBM"; DBM one of the GUI's
! powers, 0, 3, 7, 10, ... 60. FST4W's are the same but the last,
! "<CALL> GRID6", and must encode as its WSPR-type messages do.
! In JT4, JT9, JT65 and Q65 an S that begins with @ is the GUI's single tone:
! @ then a frequency, which the GUI reads from the next four characters as
! Fortran reads an integer (nothing there, or no integer, is 1000 Hz; only
! those four count, so @12345 is 1234 Hz). One below 100 Hz, which the GUI
! keys as a channel tone, is refused, as is a / or , before any number there
! or anything else that leaves the frequency unread (2*, a null value), which
! leaves the GUI keying its last single tone's frequency; and so is MSK144's,
! whose encoder inverts it (genmsk_128_90) so that the GUI keys no single
! tone. A single tone takes copied and cwid only false.
! ECHO's S is the GUI's Echo message, one to six letters and digits (longer
! is too_long); TUNE takes none.
! The other keys, each refused (bad_request) in a mode it does not name:
!   submode   JT4 A to G, JT9 A to H, JT65 A to C, Q65 A to C (trperiod 15),
!             A to D (30), A to E (60 and 300), A to F (120): a letter of
!             either case, or a JSON integer, 0 for A and so on. Required.
!   trperiod  FST4 15, 30, 60, 120, 300, 900 or 1800; FST4W 120, 300, 900 or
!             1800; Q65 15, 30, 60, 120 or 300; MSK144 and fast JT9 5, 10, 15
!             or 30: a JSON integer of seconds. Required.
!   spacing   FT8, slow JT9, FST4, FST4W and WSPR: 1 (default), 2 or 4, a JSON
!             integer, the GUI's x2 and x4 tone spacing. An Echo message: 10,
!             15, 20, 25 or 30, a JSON integer of Hz, the GUI's Spacing.
!             Required there.
!   copied    Q65: true sets the message's spare 78th bit, the GUI's Q65
!             pileup flag that it copied the station it answers. Default
!             false.
!   fast      JT9: true for the GUI's fast JT9, submode E to H, whose period
!             is trperiod and which takes no spacing. Default false.
!   kind      ECHO: tone, message or cw, case aside, the GUI's Fixed Tone,
!             Message and CW. Required. A tone takes no text, a message and cw
!             do.
! A render adds F, the lowest tone, a JSON number from 200 to 5000 Hz (for
! MSK144 1000, whose tones are always 1000 and 2000 Hz; for ECHO's tone and
! message from 1490 to below 1510, the GUI's 1500 Hz and its dither of -10 to
! below +10 Hz, and its cw 1500), and R, the JSON integer 12000 or 48000. A
! single tone takes no F. At 12000 Hz the highest tone (the table below) must
! be below 6000 Hz, or the render is refused with key freq (text for a single
! tone). A render also takes:
!   seconds   TUNE: the Tune's length, a JSON integer from 1 to 600. Required.
!   cwid      JT4, JT9, JT65, Q65, FST4, FST4W, WSPR and MSK144: true sends
!             the GUI's CW ID of the station's call after the message (below).
!             Default false. The call is the one the last accepted configure
!             that named one set (its "mycall", as JTTY takes it). The render
!             is refused not_configured with key mycall when no configure has
!             set one (a blank call counts as none), when it is longer than
!             the 64 characters a configure keeps, when its JSON string does
!             not decode (unreadable), when it has a character Morse lacks
!             (letters, digits and / are sent), or when it is longer than the
!             GUI's 250 Morse elements hold, the detail saying which. encoded's
!             cwid is the call in upper case, as sent. A
!             single tone takes it only false (the GUI keys the CW ID at its
!             Tx frequency, which a single tone's render does not take), and
!             the GUI sends none in FT8, FT4, Echo (whose 3 s period its
!             Modulator excludes) or Tune.
! Any other key, a
! misspelt or mis-cased one included, is refused with that key as written
! (keys are matched without escapes; a byte not UTF-8 is reported as '?'),
! as are JTTY's keys and, on a pack, freq, rate, seconds and cwid; configure,
! unlike pack and render, ignores keys it does not know. A request holds no
! array but a Fox's slots: any other key whose value is an array or object,
! a misspelt "slots" among them, makes it a request that is not flat
! (bad_request with no key).
!
! A request's reply lines are consecutive, each carries its id, and the last
! is packed, rendered or rejected. pack answers encoded then packed; render
! answers encoded, the pcm lines of the audio, then rendered:
!   encoded   {"v":1,"t":"encoded","id":I,"mode":M,"msgsent":S,"tones":[T,...],
!              "type":N,"fallback":B,"submode":L,"trperiod":P,"spacing":N,
!              "copied":B,"fast":B,"kind":K,"freq":F,"rate":R,"seconds":N,
!              "cwid":C,"samples":N}
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
! ECHO's and TUNE's are "ECHO" and "TUNE", the GUI's. A single tone's is
! JT4's S, Q65's the frequency right-justified in five characters then
! " Hz" (" 1500 Hz"), and JT9's and JT65's empty
! (JT9's encoder gives none, and JT65's gives the last message it encoded,
! which the GUI shows and the codec does not repeat).
! tones are the channel symbols the GUI keys: none for a single tone, [0] for
! TUNE and ECHO's tone, an Echo message's six, and none for its cw. type, for
! JT4, JT9, JT65 and MSK144 only, is the GUI's message type: for JT4, JT9 and
! JT65 1 standard, 2 to 5 with a prefix or suffix, 6 free text, 0 a single
! tone, and for JT65 7 a shorthand (RO, RRR or 73 alone); for MSK144 1, or 7
! for a short message "<CALL1 CALL2> RPT".
! fallback, for FT8, FT4, FST4, FST4W, Q65 and MSK144, says the message did
! not encode as written and its first 13 characters went as free text
! instead, as the GUI sends it. submode (its letter), trperiod, spacing,
! copied, fast (fast JT9 only) and kind are those applied, each where the mode
! has it; a render adds freq, rate, seconds (TUNE), cwid (the call its CW ID
! sends, when asked) and samples. A single tone carries freq, its frequency,
! on pack and render. pcm lines are as JTTY's render writes them (int16
! little-endian mono, base64, 16384 samples a line but the last).
!
! Each mode's samples per symbol at 12 kHz, tone spacing, symbols, and its
! highest tone above the lowest:
!   FT8     1920; 12000/1920 = 6.25 Hz (x2 12.5, x4 25); 79; 7 spacings
!   FT4     576; 12000/576 Hz; 103, with a ramp symbol either side; 3
!   JT4     2520*12000/11025; 4.375 Hz times 1, 2, 4, 9, 18, 36, 72 for A to
!           G; 206; 3
!   JT9     6912; 12000/6912 Hz times 2**submode and spacing; 85; 8. Fast
!           (E to H): 480, 240, 120, 60; 12000/that Hz; 85; 8
!   JT65    4096*12000/11025; 11025/4096 Hz times 1, 2, 4 for A to C; 126; 65
!   Q65     1800, 3600, 7200, 16000, 41472 for 15, 30, 60, 120, 300 s;
!           12000/that times 2**submode; 85; 64
!   FST4,   720, 1680, 3888, 8200, 21504, 66560, 134400 for 15, 30, 60, 120,
!   FST4W   300, 900, 1800 s; 12000/that times spacing; 160; 3
!   WSPR    8192; 12000/8192 Hz times spacing; 162; 3
!   MSK144  6; tones at 1000 and 2000 Hz; 144, or 40 for a short message
!   ECHO    4096; a message spacing Hz; 6; 36 spacings (a tone and cw: none)
!   TUNE    one tone at F
! A single tone's is its frequency.
!
! The audio is the GUI's, sample for sample: at 48000 Hz the samples its
! Modulator plays for the message from its first to the last it commits,
! full scale, before the GUI's output attenuation; at 12000 Hz every fourth
! of them from the first. At 48000 Hz that is the samples of the GUI's
! symbols, rounded down, and one more; at 12000 Hz a quarter of it, rounded
! up. MSK144 and fast JT9 instead repeat their message for the period less
! 0.5 s, and one sample more. FT8, FT4, FST4 and FST4W are the GUI's shaped
! waveforms; JT4, JT9, JT65, WSPR, Q65, MSK144, and FT8 with spacing 2 or 4,
! are phase-continuous tones whose amplitude decays over the last 0.017
! symbol; a single tone is one such tone, at its frequency for the mode's
! symbols. ECHO is 2.048 s and one sample: for a tone a steady tone at F, for
! a message its six symbols as the GUI's genwave makes them from its letters
! and digits (lib/gen_echocall.f90), and for cw the text in Morse filling the
! 2.048 s, as lib/gen_cw_wave.f90 keys it. TUNE is seconds of a steady tone
! at F; the GUI's own Tune stops after 9999 times its mode's samples per
! symbol at 12 kHz, counted at 48000 Hz (999999 times in MSK144 and fast
! JT9), which in FT4 is 120 s, in FT8 400 s, in MSK144 125 s and in FST4 at
! 15 s 150 s, and a render does not.
! A CW ID is the call in Morse keyed at 2560 samples a dit at 48000 Hz (22.5
! WPM), each key change ramped over 512 samples, at the message's frequency
! (FST4 and FST4W: the centre of their four tones), from the sample after the
! message's last; the GUI plays silence from there to the end of its audio
! buffer first, which a render does not. In MSK144 and fast JT9 the GUI
! instead stops the message for the CW ID at the first audio buffer it reads
! more than TR - 5 s into the period, at 1500 Hz, hard keyed, faster for a
! call of more than 4 s; a render, whose audio starts with the period, stops
! it 48 samples (1 ms at 48000 Hz) after TR - 5 s, the earliest the GUI's
! millisecond clock allows, so at TR 5 s the CW ID replaces the message after
! 48 samples (the GUI sends none of it or a whole buffer), while encoded
! still gives the message. Every transmission with a CW ID ends with it.
! The GUI starts the audio 1 s into its period (0.5 s for FT8, FST4 at 15 s
! and Q65 at 15 and 30 s, 0.3 s for FT4, none for MSK144, fast JT9 and Echo,
! which it sends every 6 s); timing is the client's.
!
! Each R and its K:
!   bad_request      the key at fault; none when the request is not a flat
!                    object. For text: the message does not encode in M (the
!                    GUI's *** bad message ***, which it does not transmit),
!                    or a hashed call in it collides (below); a single tone
!                    the GUI does not key as one (above); it is
!                    not a WSPR message of the forms above, or not an Echo
!                    message.
!   empty            none: text is blank
!   too_long         text: it is longer than the mode takes; none: a render
!                    of more than 600 s of audio, which FST4 and FST4W give
!                    at 900 and 1800 s
!   not_configured   mycall: a CW ID and no call it can send
!   encoding_failed  none
! and for FOX and SUPERFOX those below. A request without such an id gets
! error invalid_request_id.
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
! The GUI's Fox (FT8 with its Fox special operation) sends up to five FT8
! messages at once, one a slot, and with its SuperFox option packs them into
! one transmission:
!   {"t":"pack","id":I,"mode":"FOX","slots":[S,...]}
!   {"t":"pack","id":I,"mode":"SUPERFOX","slots":[S,...],"text":T,
!    "more_cqs":B,"otp":N}
! and their renders with "freq" and "rate". slots, required, are one to five
! messages as MainWindow::foxTxSequencer writes them (four for a SuperFox
! with text), each less its outer spaces and at most 37 bytes, taken as
! written: the GUI does not upper-case them. A SuperFox slot must be one of
! the forms the GUI writes, word for word, since foxgen2 and sfox_assemble
! alter or drop most other text without saying so (sfox_pack refuses some
! itself): "CQ CALL GRID4" (the GUI writes "CQ CALL" for a compound call,
! which sfox_pack refuses), "HOUND CALL RR73",
! "HOUND CALL RPT" and "HOUND1 RR73; HOUND2 <CALL> RPT", RPT a sign and two
! digits (sfox_pack takes -18 to +12; zero is +00, as the GUI writes it), each
! HOUND at most 6 characters, as the GUI queues them. The slots name the Fox
! call as the GUI writes it: whole, the same, in every combined slot, and as
! its base call (Radio::base_callsign) in every other Hound slot. SuperFox
! sends the last slot's for every Hound, and sfox_pack takes a CQ only as
! three words and the Fox call of Hound messages only as a standard call, so
! for a compound or suffixed Fox call, whose base call differs from it, the
! GUI's CQ, which drops the grid, is refused ("CQ CALL GRID4" encodes), and
! the last slot must be one that names the base call; the GUI's own SuperFox
! fails the same way. A CQ slot goes alone, as the GUI writes one only when
! it has no Hound to call: with Hound slots SuperFox would send only the CQ.
! SuperFox carries at most 5 Hounds with RR73 and 4 with reports, 9 in all,
! or 4 in all with text; a combined slot holds one of each. T, its free text,
! is upper-cased and keeps its leading blanks, as the GUI's is, and is at
! most 26 bytes less its trailing blanks; it is sent when given, even blank.
! more_cqs (default false) is the GUI's More CQs; N (default 0, the GUI's
! code with verification off) is the six-digit verification code the GUI
! makes from the operator's seed and the time, which the client makes.
! FOX's F is the lowest tone of its first slot, a JSON integer from 200 to
! 5000 Hz, each later slot 60 Hz above the last (the GUI keeps it at 900 Hz
! or below, moving a higher one to 500, and unless Hold Tx Freq picks it
! from 300 to 598 Hz for each transmission); SUPERFOX's is 750, a JSON
! integer.
! FOX's encoded line has "slots", for each its "msgsent", "tones" and
! "fallback" as FT8's, instead of msgsent and tones; SUPERFOX's has "tones",
! its 151 channel symbols (0 to 128), "more_cqs" and "otp", and no msgsent:
! the GUI shows and logs the slots and the text as written, and a Hound
! decodes what sfox_pack packs from them. Both pack with FT8's hash tables:
! a FOX request's slots enter them in turn, and a SUPERFOX request's Fox and
! Hound calls do (a CQ's call does not).
! FOX's audio is foxgen's: each slot's tones as phase-continuous tones
! without FT8's shaping, summed, scaled to full scale, filtered (foxfilt)
! and scaled again, 79 symbols of 1920 samples at 12 kHz and one sample.
! foxfilt's FFTs are planned as the GUI plans them (FFTW_ESTIMATE).
! SUPERFOX's is sfox_wave_gfsk's GFSK of its tones at 750 Hz, 11.72 Hz a
! tone, with a ramp symbol either side: 153 symbols of 1024 samples and one.
! The GUI starts a Fox 0.5 s and a SuperFox 0.4 s into its period. Each
! refusal's R and K:
!   empty            slots: a slot is blank
!   too_long         slots: a slot is longer than 37 bytes; text: the free
!                    text is longer than 26
!   bad_request      slots: they are not one to five JSON strings (four
!                    with text), a FOX slot does not encode in FT8 (which the
!                    GUI's Fox would send as a steady tone) or a hashed call
!                    in it collides, a SUPERFOX slot is none of its forms or
!                    has a longer Hound call, the slots name different Fox
!                    calls, a CQ slot is not alone, they hold more Hounds than
!                    SuperFox carries, or sfox_pack refuses them (invalid
!                    message tokens, callsign, CQ call or grid, or a report
!                    outside -18 to +12); text: its free text has a character
!                    SuperFox does not send; otp: not a JSON integer from 0
!                    to 999999.
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
  use, intrinsic :: iso_c_binding, only: c_int16_t, c_int32_t, c_double, c_char, &
       c_int, c_float, c_float_complex
  use, intrinsic :: iso_fortran_env, only: int64, output_unit
  use streaming_control, only: slot_request, parse_slot_request,              &
       encode_request_mode, MODE_JTTY, MODE_WSPR, MODE_ECHO, MODE_TUNE,        &
       MODE_FOX, MODE_SUPERFOX
  use streaming_emit, only: streaming_emit_error_code, streaming_emit_json_escape
  use streaming_jtty, only: jtty_encode, encode_emit_rejected, encode_emit_pcm, &
       encode_station_call
  use packjt77, only: pack77_state, pack77_for_state, pack77_options,          &
       PACK77_STATUS_ENCODED
  use packjt, only: packcall
  use morse_encoder, only: encode_morse
  use ft8_codec_context, only: genft8_for_state
  use ft4_codec, only: genft4_for_state
  use fst4_tx, only: genfst4_for_state
  use q65_tx, only: genq65_for_state
  use msk144_tx, only: genmsk_128_90_for_state
  use fox_tx, only: foxgen_for_state
  use sfox_tx, only: sftx_for_state, sfox_wave_for_tones
  implicit none
  private

  public :: stream_encode

  integer, parameter :: MAX_RENDER_SECONDS = 600
  character(len=*), parameter :: BAD_MESSAGE = '*** bad message ***'
  ! An Echo transmission's samples at 48000 Hz (MainWindow::transmit).
  integer, parameter :: ECHO_WAVE = 6 * 4 * 4096
  ! The GUI's two sets of transmit hash tables: FT8's and FT4's
  ! (lib/77bit/decoder_codec_context.f90), and packjt77's own, which its
  ! FST4, Q65 and MSK144 encoders use.
  type(pack77_state), save, target :: ft_tables_, tables_

  ! lib/stream_slot_audio.cpp's Tx.
  type, bind(C) :: stream_tx
     integer(c_int32_t) :: mode = 0
     integer(c_int32_t) :: submode = -1
     integer(c_int32_t) :: spacing = 1
     integer(c_int32_t) :: trperiod = 0
     integer(c_int32_t) :: nsym = 0
     integer(c_int32_t) :: fast = 0
     integer(c_int32_t) :: seconds = 0
     integer(c_int32_t) :: kind = 0
     integer(c_int32_t) :: rate = 0
     real(c_double)     :: freq = 0
  end type stream_tx

  interface
     ! lib/gen65.f90
     subroutine gen65(msg00, ichk, msgsent0, itone, itype) bind(C, name='gen65')
       import :: c_char, c_int
       character(kind=c_char) :: msg00(23), msgsent0(23)
       integer(c_int) :: ichk, itone(126), itype
     end subroutine gen65
     ! lib/gen_echocall.f90
     subroutine gen_echocall(callsign, itone)
       character(len=6) :: callsign
       integer :: itone(6)
     end subroutine gen_echocall
     ! lib/genwave.f90
     subroutine genwave(itone, nsym, nsps, nwave, fsample, tonespacing, f0,   &
          icmplx, cwave, wave)
       import :: c_float, c_float_complex
       integer :: itone(*), nsym, nsps, nwave, icmplx
       real(c_float) :: fsample, f0, wave(*)
       real(8) :: tonespacing
       complex(c_float_complex) :: cwave(*)
     end subroutine genwave
     ! lib/gen_cw_wave.f90
     subroutine gen_cw_wave(message, ifreq, wave)
       import :: c_float
       character(len=*) :: message
       integer :: ifreq
       real(c_float) :: wave(*)
     end subroutine gen_cw_wave
     ! lib/morse.f90
     subroutine morse(msg, idat, n)
       character(len=*), intent(in) :: msg
       integer, intent(out) :: idat(250), n
     end subroutine morse
     ! lib/stream_slot_audio.cpp
     function stream_slot_samples(tx, ncw) result(count)                      &
          bind(C, name='stream_slot_samples')
       import :: c_int32_t, stream_tx
       type(stream_tx), intent(in) :: tx
       integer(c_int32_t), value :: ncw
       integer(c_int32_t) :: count
     end function stream_slot_samples
     function stream_slot_top_tone(tx, tones, ntones) result(top)             &
          bind(C, name='stream_slot_top_tone')
       import :: c_int32_t, c_double, stream_tx
       type(stream_tx), intent(in) :: tx
       integer(c_int32_t), intent(in) :: tones(*)
       integer(c_int32_t), value :: ntones
       real(c_double) :: top
     end function stream_slot_top_tone
     function stream_slot_render(tx, tones, ntones, wave, nwave, cw, ncw,     &
          samples, capacity) result(count) bind(C, name='stream_slot_render')
       import :: c_int32_t, c_int16_t, c_float, stream_tx
       type(stream_tx), intent(in) :: tx
       integer(c_int32_t), intent(in) :: tones(*), cw(*)
       integer(c_int32_t), value :: ntones, nwave, ncw, capacity
       real(c_float), intent(in) :: wave(*)
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
    type(stream_tx) :: tx
    character(len=37) :: message, msgsent, unused
    character(len=22) :: decoded
    character(len=64) :: call
    character(len=:), allocatable :: id, line, text
    integer(c_int32_t) :: tones(206), unused_tones(206), top_tones(6), cw(250)
    integer(c_int16_t), allocatable :: samples(:)
    real(c_float), allocatable :: wave(:)
    integer(c_int32_t) :: count
    integer :: ntones, itype, i, alloc_status, first, last, limit, n_unused,   &
         t_unused, nfreq, ncw, ntop, cw_status
    real(c_double) :: top
    logical :: fallback, ok, call_long, call_unreadable

    count = 0
    ncw = 0
    nfreq = 0
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
    if (req%mode .eq. MODE_FOX .or. req%mode .eq. MODE_SUPERFOX) then
       call fox_encode_(req, render, id)
       return
    end if

    message = ' '
    if (req%mode .ne. MODE_TUNE .and. req%kind .ne. 1) then
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
       if (req%mode .eq. MODE_ECHO) limit = 6
       if (len(text) .gt. limit) then
          call encode_emit_rejected(id, 'too_long', 'text', 'the message is longer ' // &
               'than ' // itoa_(int(limit, int64)) // ' bytes, all ' //             &
               mode_name_(req%mode) // ' takes')
          return
       end if
       call tx_buffer_(text, message)
    end if
    select case (req%mode)
    case (4, 9, 65, 66)
       if (req%single) then
          if (.not. single_tone_(message, nfreq)) then
             call encode_emit_rejected(id, 'bad_request', 'text', 'the four ' //  &
                  'characters after @ give a frequency below 100 Hz, or a / or ' // &
                  ', or anything else before any number that leaves the ' //     &
                  'frequency unread, which the GUI keys as a channel tone or ' //  &
                  'at a stale frequency')
             return
          end if
          if (req%copied) then
             call encode_emit_rejected(id, 'bad_request', 'copied', 'a single ' // &
                  'tone carries no message bits')
             return
          end if
          ! The GUI sends the CW ID at its Tx frequency, which a single tone's
          ! render does not take.
          if (req%cwid) then
             call encode_emit_rejected(id, 'bad_request', 'cwid', 'a single ' // &
                  'tone takes no CW ID')
             return
          end if
       end if
    case (144)
       if (message(1:1) .eq. '@') then
          call encode_emit_rejected(id, 'bad_request', 'text', 'MSK144''s ' //  &
               'single tone is not encoded: the GUI''s MSK144 encoder ' //       &
               '(genmsk_128_90) inverts it, so that it keys no single tone')
          return
       end if
    case (MODE_WSPR)
       if (.not. wspr_message_ok_(message(1:22))) then
          call encode_emit_rejected(id, 'bad_request', 'text', 'not a WSPR ' // &
               'message: CALL GRID4 DBM, PFX/CALL DBM, CALL/SFX DBM or <CALL> GRID6 DBM')
          return
       end if
    case (MODE_ECHO)
       if (req%kind .ne. 1 .and.                                              &
            verify(trim(message), 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789') .ne. 0) then
          call encode_emit_rejected(id, 'bad_request', 'text', 'an Echo ' //    &
               'message is one to six letters and digits')
          return
       end if
    end select

    tx%mode = req%mode
    tx%submode = req%submode
    tx%spacing = req%spacing
    tx%trperiod = req%trperiod
    tx%nsym = 144
    tx%fast = merge(1, 0, req%fast)
    tx%seconds = req%seconds
    tx%kind = req%kind
    tx%rate = req%rate
    tx%freq = req%freq
    if (req%single) tx%freq = nfreq

    if (render) then
       if (req%rate .eq. 12000) then
          ntop = 1
          top_tones(1) = 0
          if (req%single) top_tones(1) = nfreq
          if (req%kind .eq. 2) then
             call gen_echocall(message(1:6), top_tones)
             ntop = 6
          end if
          top = stream_slot_top_tone(tx, top_tones, ntop)
          if (top .ge. 6000.d0) then
             ! A single tone's frequency comes from its text.
             call encode_emit_rejected(id, 'bad_request', merge('text', 'freq', req%single), &
                  'at 12000 Hz the top tone, ' // itoa_(nint(top, int64)) // ' Hz, ' // &
                  'would fold back; it must be below 6000 Hz')
             return
          end if
       end if
       if (req%cwid) then
          call encode_station_call(call, call_long, call_unreadable)
          if (call_unreadable) then
             cw_status = 5
          else if (call_long) then
             cw_status = 4
          else
             call cw_id_(trim(call), cw, ncw, cw_status)
          end if
          if (cw_status .ne. 0) then
             select case (cw_status)
             case (2)
                line = 'the configured call has a character Morse does not ' // &
                     'send (it sends letters, digits and /)'
             case (3)
                line = 'the configured call is longer than the GUI''s 250 ' //  &
                     'Morse elements hold'
             case (4)
                line = 'the configured call is longer than the 64 characters ' // &
                     'a configure keeps'
             case (5)
                line = 'the configured call is unreadable (its JSON string ' //  &
                     'does not decode)'
             case default
                line = 'no configure has set it'
             end select
             call encode_emit_rejected(id, 'not_configured', 'mycall', 'a CW ID ' // &
                  'sends the station''s call, and ' // line)
             return
          end if
       end if
       count = stream_slot_samples(tx, ncw)
       if (count .le. 0) then
          call encode_emit_rejected(id, 'encoding_failed', '', 'the audio has no length')
          return
       end if
       if (int(count, int64) .gt. int(MAX_RENDER_SECONDS, int64) * req%rate) then
          call encode_emit_rejected(id, 'too_long', '', 'the audio is longer than 600 s')
          return
       end if
    end if

    call generate_(req, message, nfreq, ft_tables_, tables_, msgsent, tones,   &
         ntones, itype, fallback)
    if (trim(msgsent) .eq. BAD_MESSAGE) then
       ! With no calls in its tables, a message whose hashed call collides
       ! with one already there would encode.
       if (req%mode .ne. 4 .and. req%mode .ne. 9 .and. req%mode .ne. 65 .and. &
            req%mode .ne. MODE_WSPR) then
          call generate_(req, message, nfreq, fresh_ft, fresh, unused,          &
               unused_tones, n_unused, t_unused, ok)
          if (trim(unused) .ne. BAD_MESSAGE) then
             line = 'FST4, FST4W, Q65 or MSK144'
             if (req%mode .eq. 8 .or. req%mode .eq. 5) line = 'FT8, FT4, FOX or SUPERFOX'
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
    tx%nsym = ntones

    line = '{"v":1,"t":"encoded","id":' // id // ',"mode":"' //                &
         mode_name_(req%mode) // '","msgsent":"' // escaped_(msgsent) //        &
         '","tones":['
    if (.not. req%single .and. req%kind .ne. 3) then
       do i = 1, ntones
          if (i .gt. 1) line = line // ','
          line = line // itoa_(int(tones(i), int64))
       end do
    end if
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
    if (any(req%mode .eq. [8, 240, 241, MODE_WSPR]) .or. req%kind .eq. 2 .or.   &
         (req%mode .eq. 9 .and. .not. req%fast)) line = line // ',"spacing":' // &
         itoa_(int(req%spacing, int64))
    if (req%mode .eq. 66) line = line // ',"copied":' //                       &
         trim(merge('true ', 'false', req%copied))
    if (req%fast) line = line // ',"fast":true'
    select case (req%kind)
    case (1)
       line = line // ',"kind":"tone"'
    case (2)
       line = line // ',"kind":"message"'
    case (3)
       line = line // ',"kind":"cw"'
    end select
    if (req%single) line = line // ',"freq":' // itoa_(int(nfreq, int64))
    if (.not. render) then
       call emit_line_(line // '}')
       call emit_line_('{"v":1,"t":"packed","id":' // id // '}')
       return
    end if
    if (.not. req%single) line = line // ',"freq":' // number_(req%freq)
    line = line // ',"rate":' // itoa_(int(req%rate, int64))
    if (req%mode .eq. MODE_TUNE) line = line // ',"seconds":' //               &
         itoa_(int(req%seconds, int64))
    if (req%cwid) line = line // ',"cwid":"' // escaped_(upper_(trim(call))) // '"'

    call echo_wave_(req, message, tones, wave)
    allocate(samples(count), stat=alloc_status)
    if (alloc_status .eq. 0) count = stream_slot_render(tx, tones, ntones,     &
         wave, size(wave, kind=c_int32_t), cw, ncw, samples, count)
    if (alloc_status .ne. 0 .or. count .ne. size(samples)) then
       call encode_emit_rejected(id, 'encoding_failed', '', 'the audio did not render')
       return
    end if
    call emit_line_(line // ',"samples":' // itoa_(int(count, int64)) // '}')
    call encode_emit_pcm(id, 0, samples)
    call emit_line_('{"v":1,"t":"rendered","id":' // id // '}')
  end subroutine slot_encode_

  ! A FOX or SUPERFOX request, whose id is id, generated as the GUI's Fox
  ! generates its transmission (MainWindow::foxTxSequencer, foxgen, sfox_tx).
  subroutine fox_encode_(req, render, id)
    type(slot_request), intent(in) :: req
    logical,            intent(in) :: render
    character(len=*),   intent(in) :: id
    type(pack77_state), target :: trial, wave_tables, fresh
    type(stream_tx) :: tx
    character(len=40) :: cmsg(5)
    character(len=37) :: msgsent(5), unused
    character(len=77) :: c77
    character(len=10) :: ckey
    character(len=:), allocatable :: line, text, slot, message
    integer(c_int32_t) :: tones(151), itone(79, 5), unused_tone(79), cw(1)
    integer(kind=1) :: msgbits(77, 5), unused_bits(77)
    integer(c_int16_t), allocatable :: samples(:)
    real(c_float), allocatable :: wave(:)
    integer(c_int32_t) :: count, ntones
    integer :: n, i, first, last, nslots, i3, n3, status, pack_error, alloc_status, patience
    logical :: fallback(5)
    integer :: npatience, nthreads
    common/patience/npatience, nthreads

    cmsg = ' '
    text = ''
    slot = ''
    nslots = req%nslots
    do n = 1, nslots
       slot = itoa_(int(n, int64))
       first = verify(req%slots(n), ' ')
       last = verify(req%slots(n), ' ', back=.true.)
       if (first .eq. 0) then
          call encode_emit_rejected(id, 'empty', 'slots', 'slot ' // slot // ' is blank')
          return
       end if
       if (req%slot_long(n) .or. last - first + 1 .gt. 37) then
          call encode_emit_rejected(id, 'too_long', 'slots', 'slot ' // slot //        &
               ' is longer than the 37 bytes the GUI''s Fox encodes')
          return
       end if
       message = req%slots(n)(first:last)
       cmsg(n) = message
    end do
    if (req%mode .eq. MODE_SUPERFOX) then
       call superfox_slots_(cmsg, nslots, req%send_text, line)
       if (len(line) .gt. 0) then
          call encode_emit_rejected(id, 'bad_request', 'slots', line)
          return
       end if
    end if
    if (req%mode .eq. MODE_SUPERFOX .and. req%send_text) then
       ! The GUI's free text keeps its leading blanks and is upper-cased.
       last = verify(req%text, ' ', back=.true.)
       if (last .gt. 0) text = upper_(req%text(1:last))
       if (len(text) .gt. 26) then
          call encode_emit_rejected(id, 'too_long', 'text', 'the free text is longer ' // &
               'than the 26 bytes SuperFox sends')
          return
       end if
    end if

    tx%mode = req%mode
    tx%rate = req%rate
    tx%freq = req%freq
    count = 0
    tones = 0
    allocate(wave(merge(merge(79 * 4 * 1920, 153 * 4096, req%mode .eq. MODE_FOX), 0, render)))
    ! At 12000 Hz neither folds back: FOX's top tone is at most 5283.75 Hz,
    ! and SUPERFOX's 2250.
    if (render) then
       count = stream_slot_samples(tx, 0)
       if (count .le. 0) then
          call encode_emit_rejected(id, 'encoding_failed', '', 'the audio has no length')
          return
       end if
    end if

    trial = ft_tables_
    line = '{"v":1,"t":"encoded","id":' // id // ',"mode":"' // mode_name_(req%mode) // '"'
    if (req%mode .eq. MODE_FOX) then
       ! foxgen encodes each slot with genft8 and the GUI's FT8 tables, in turn.
       do n = 1, nslots
          call pack77_for_state(trial, cmsg(n)(1:37), i3, n3, c77,                &
               pack77_options(record_tx_hashes=.false.), status)
          call genft8_for_state(trial, cmsg(n)(1:37), i3, n3, msgsent(n),        &
               msgbits(:, n), itone(:, n))
          if (trim(msgsent(n)) .eq. BAD_MESSAGE) then
             slot = itoa_(int(n, int64))
             call genft8_for_state(fresh, cmsg(n)(1:37), i3, n3, unused, unused_bits, &
                  unused_tone)
             if (trim(unused) .ne. BAD_MESSAGE) then
                call encode_emit_rejected(id, 'bad_request', 'slots', 'a hashed ' //  &
                     'call in slot ' // slot // ' has the hash of another call ' //    &
                     'already sent in FT8, FT4, FOX or SUPERFOX; send the call in ' //  &
                     'full in a message first, which replaces the other')
             else
                call encode_emit_rejected(id, 'bad_request', 'slots', 'slot ' //      &
                     slot // ' does not encode in FT8 (the GUI''s ' // BAD_MESSAGE //    &
                     ', which its Fox sends as a steady tone)')
             end if
             return
          end if
          fallback(n) = status .ne. PACK77_STATUS_ENCODED
       end do
       line = line // ',"slots":['
       do n = 1, nslots
          if (n .gt. 1) line = line // ','
          line = line // '{"msgsent":"' // escaped_(msgsent(n)) // '","tones":['
          do i = 1, 79
             if (i .gt. 1) line = line // ','
             line = line // itoa_(int(itone(i, n), int64))
          end do
          line = line // '],"fallback":' // trim(merge('true ', 'false', fallback(n))) // '}'
       end do
       line = line // ']'
       tones(1) = itone(1, 1)
       ntones = 1
       if (render) then
          ! The GUI plans foxfilt's FFTs with its patience, 0.
          wave_tables = ft_tables_
          patience = npatience
          npatience = 0
          call foxgen_for_state(wave_tables, nslots, cmsg, nint(req%freq), wave,  &
               msgsent, itone, msgbits)
          npatience = patience
       end if
    else
       ! foxgen's SuperFox flags, then sfox_tx's packing with the GUI's FT8
       ! tables and the verification code req%otp.
       if (req%more_cqs) cmsg(1)(40:40) = '1'
       if (req%send_text) then
          nslots = nslots + 1
          cmsg(nslots) = text
          cmsg(nslots)(39:39) = '1'
       end if
       write(ckey, '(a,i0)') 'OTP:', req%otp
       call sftx_for_state(trial, ckey, nslots, cmsg, tones, pack_error)
       if (pack_error .ne. 0) then
          call superfox_refusal_(id, pack_error)
          return
       end if
       ntones = 151
       line = line // ',"tones":['
       do i = 1, 151
          if (i .gt. 1) line = line // ','
          line = line // itoa_(int(tones(i), int64))
       end do
       line = line // '],"more_cqs":' // trim(merge('true ', 'false', req%more_cqs)) // &
            ',"otp":' // itoa_(int(req%otp, int64))
       if (render) call sfox_wave_for_tones(tones, wave)
    end if
    ft_tables_ = trial

    if (.not. render) then
       call emit_line_(line // '}')
       call emit_line_('{"v":1,"t":"packed","id":' // id // '}')
       return
    end if
    line = line // ',"freq":' // number_(req%freq) // ',"rate":' // itoa_(int(req%rate, int64))
    allocate(samples(count), stat=alloc_status)
    if (alloc_status .eq. 0) count = stream_slot_render(tx, tones, ntones,     &
         wave, size(wave, kind=c_int32_t), cw, 0, samples, count)
    if (alloc_status .ne. 0 .or. count .ne. size(samples)) then
       call encode_emit_rejected(id, 'encoding_failed', '', 'the audio did not render')
       return
    end if
    call emit_line_(line // ',"samples":' // itoa_(int(count, int64)) // '}')
    call encode_emit_pcm(id, 0, samples)
    call emit_line_('{"v":1,"t":"rendered","id":' // id // '}')
  end subroutine fox_encode_

  ! Whether msg is one of the messages the GUI's SuperFox builds, as foxgen2
  ! sorts them, and names the Fox's call (with none, sfox_assemble would send
  ! the call of an earlier transmission).
  subroutine superfox_slots_(cmsg, nslots, send_text, problem)
    character(len=40),             intent(in)  :: cmsg(5)
    integer,                       intent(in)  :: nslots
    logical,                       intent(in)  :: send_text
    character(len=:), allocatable, intent(out) :: problem
    character(len=37) :: words(6)
    character(len=:), allocatable :: slot, fox, whole, single
    integer :: n, nwords, i, rr73, reports, cqs
    logical :: ok, paired

    problem = ''
    rr73 = 0
    reports = 0
    cqs = 0
    whole = ''
    single = ''
    do n = 1, nslots
       slot = 'slot ' // itoa_(int(n, int64))
       call split_words_(upper_(cmsg(n)(1:37)), words, nwords)
       fox = ''
       ok = .false.
       if (words(1) .eq. 'CQ') then
          ! CQ CALL GRID4, or CQ CALL for a compound call, which sfox_pack refuses.
          ok = nwords .eq. 3 .or. (nwords .eq. 2 .and. index(words(2), '/') .gt. 0)
          cqs = cqs + 1
       else if (nwords .eq. 3) then
          ok = trim(words(3)) .eq. 'RR73' .or. report_(words(3))
          fox = trim(words(2))
          if (trim(words(3)) .eq. 'RR73') rr73 = rr73 + 1
          if (report_(words(3))) reports = reports + 1
       else if (nwords .eq. 5) then
          i = len_trim(words(4))
          ok = trim(words(2)) .eq. 'RR73;' .and. words(4)(1:1) .eq. '<' .and.      &
               words(4)(i:i) .eq. '>' .and. i .ge. 3 .and. report_(words(5))
          if (ok) fox = words(4)(2:i - 1)
          rr73 = rr73 + 1
          reports = reports + 1
       end if
       if (.not. ok) then
          problem = slot // ' is none of the GUI''s SuperFox messages: CQ CALL ' //  &
               'GRID4, HOUND CALL RR73, HOUND CALL RPT or HOUND1 RR73; HOUND2 ' //    &
               '<CALL> RPT, RPT a sign and two digits (+00, not -00)'
          return
       end if
       ! The GUI queues a Hound only when its base call is at most 6 characters
       ! (MainWindow's superFoxQueueableHound); foxgen2 cuts longer ones.
       if (words(1) .ne. 'CQ' .and. (len_trim(words(1)) .gt. 6 .or.               &
            (nwords .eq. 5 .and. len_trim(words(3)) .gt. 6))) then
          problem = slot // ' has a Hound call longer than the 6 characters ' //    &
               'SuperFox sends'
          return
       end if
       ! The GUI writes its call whole in each combined slot and as its base
       ! call in the others.
       if (len(fox) .gt. 0) then
          if (nwords .eq. 5) then
             if (len(whole) .eq. 0) whole = fox
             paired = fox .eq. whole
          else
             if (len(single) .eq. 0) single = fox
             paired = fox .eq. single
          end if
          if (paired .and. len(whole) .gt. 0 .and. len(single) .gt. 0)         &
               paired = single .eq. base_call_(whole)
          if (.not. paired) then
             problem = 'the slots name different Fox calls: the GUI writes its ' // &
                  'call whole in each combined slot and as its base call in ' //    &
                  'the others, and SuperFox sends the last slot''s for every Hound'
             return
          end if
       end if
    end do
    if (cqs .gt. 0 .and. nslots .gt. 1) then
       problem = 'a CQ slot goes alone: with one, SuperFox sends only the CQ (the ' // &
            'GUI writes a CQ only when it has no Hound slot)'
    else if (reports .gt. 4 .or. (send_text .and. rr73 + reports .gt. 4)) then
       ! Five slots hold at most five RR73s, so nine Hounds in all.
       problem = 'the slots hold ' // itoa_(int(rr73 + reports, int64)) //           &
            ' Hounds; SuperFox sends at most 5 RR73s and 4 reports, 9 Hounds in ' //  &
            'all, or 4 with free text (a combined slot holds two)'
    end if

  contains

    ! A report as the GUI writes one: a sign and two digits, zero as +00.
    logical function report_(word)
      character(len=*), intent(in) :: word
      report_ = len_trim(word) .eq. 3 .and. (word(1:1) .eq. '+' .or.             &
           word(1:1) .eq. '-') .and. verify(word(2:3), '0123456789') .eq. 0 .and. &
           word(1:3) .ne. '-00'
    end function report_

  end subroutine superfox_slots_

  ! s's blank-separated words, the first size(words) of them, and their count.
  subroutine split_words_(s, words, nwords)
    character(len=*), intent(in)  :: s
    character(len=*), intent(out) :: words(:)
    integer,          intent(out) :: nwords
    integer :: i
    logical :: inword
    words = ' '
    nwords = 0
    inword = .false.
    do i = 1, len(s)
       if (s(i:i) .eq. ' ') then
          inword = .false.
       else
          if (.not. inword) nwords = nwords + 1
          inword = .true.
          if (nwords .le. size(words)) words(nwords) = trim(words(nwords)) // s(i:i)
       end if
    end do
  end subroutine split_words_

  ! Radio::base_callsign: call less its operating suffixes (/P, /QRP, ...),
  ! then of a compound call the longer part, the part after / on a tie.
  function base_call_(call) result(base)
    character(len=*), intent(in) :: call
    character(len=:), allocatable :: base
    integer :: slash
    base = upper_(trim(call))
    do
       slash = index(base, '/', back=.true.)
       if (slash .eq. 0) exit
       select case (base(slash + 1:))
       case ('0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'M', 'P', 'Q', &
            'R', 'QRP', 'FD', 'FF', 'AM', 'MM', 'LH', 'LT', 'LGT')
          base = base(1:slash - 1)
       case default
          exit
       end select
    end do
    slash = index(base, '/')
    if (slash .gt. 0) then
       if (len(base) - slash .ge. slash - 1) then
          base = base(slash + 1:)
       else
          base = base(1:slash - 1)
       end if
    end if
  end function base_call_

  ! sfox_pack's error code as a refusal, with MainWindow::sfox_tx's words.
  subroutine superfox_refusal_(id, pack_error)
    character(len=*), intent(in) :: id
    integer,          intent(in) :: pack_error
    select case (pack_error)
    case (2)
       call encode_emit_rejected(id, 'bad_request', 'otp', 'invalid verification code')
    case (3)
       call encode_emit_rejected(id, 'bad_request', 'slots', 'invalid CQ call or grid')
    case (4)
       call encode_emit_rejected(id, 'bad_request', 'slots', 'invalid callsign')
    case (5)
       call encode_emit_rejected(id, 'bad_request', 'slots', 'report is outside ' //     &
            'the SuperFox range')
    case (6)
       call encode_emit_rejected(id, 'bad_request', 'text', 'free text contains ' //     &
            'unsupported characters')
    case default
       call encode_emit_rejected(id, 'bad_request', 'slots', 'invalid message tokens')
    end select
  end subroutine superfox_refusal_

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

  ! The frequency of the single tone message gives, as the JT4, JT9, JT65 and
  ! Q65 encoders read it, and whether the GUI keys it as one: a frequency
  ! below 100 is keyed as a channel tone, and none read ('/', ',' or a null
  ! value such as 2*) leaves the encoders' saved one.
  logical function single_tone_(message, nfreq)
    character(len=37), intent(in)  :: message
    integer,           intent(out) :: nfreq
    nfreq = -huge(nfreq)
    read(message(2:5), *, end=1, err=1) nfreq
    go to 2
1   nfreq = 1000
2   single_tone_ = nfreq .ge. 100
  end function single_tone_

  ! The GUI's CW ID of call: lib/morse.f90's elements as MainWindow::transmit
  ! puts them in its request, from the second (it overwrites the first, a
  ! space, with their count) and at most 249. status: 0, or 1 call is blank,
  ! 2 it has a character Morse lacks, 3 it has more elements than the GUI's
  ! 250 hold.
  subroutine cw_id_(call, cw, ncw, status)
    character(len=*),   intent(in)  :: call
    integer(c_int32_t), intent(out) :: cw(250)
    integer,            intent(out) :: ncw, status
    integer :: idat(250), whole(1000), n, nwhole
    cw = 0
    ncw = 0
    status = 1
    if (len(call) .eq. 0) return
    status = 2
    if (verify(call, 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/') &
         .ne. 0) return
    call morse(call, idat, n)
    call encode_morse(call, whole, nwhole)
    status = 3
    if (n .ne. nwhole) return
    status = 0
    ncw = min(n, 249)
    cw(1:ncw) = idat(2:ncw + 1)
  end subroutine cw_id_

  ! The waveform MainWindow::transmit precomputes for an Echo message or CW,
  ! or none.
  subroutine echo_wave_(req, message, tones, wave)
    type(slot_request),         intent(in)  :: req
    character(len=37),          intent(in)  :: message
    integer(c_int32_t),         intent(in)  :: tones(206)
    real(c_float), allocatable, intent(out) :: wave(:)
    complex(c_float_complex) :: cwave(1)
    integer :: itone(6), nsym, nsps4, nwave, icmplx, ifreq
    real(c_float) :: fsample, f0
    real(8) :: spacing

    allocate(wave(ECHO_WAVE))
    wave = 0
    if (req%kind .eq. 2) then
       itone = tones(1:6)
       nsym = 6
       nsps4 = 4 * 4096
       nwave = nsym * nsps4
       fsample = 48000.0
       spacing = req%spacing
       f0 = real(req%freq, c_float)
       icmplx = 0
       call genwave(itone, nsym, nsps4, nwave, fsample, spacing, f0, icmplx,   &
            cwave, wave)
    else if (req%kind .eq. 3) then
       ifreq = 1500
       call gen_cw_wave(trim(message), ifreq, wave)
    else
       deallocate(wave)
       allocate(wave(0))
    end if
  end subroutine echo_wave_

  ! As MainWindow::guiUpdate generates message in req's mode, with the
  ! transmit tables ft_tables (FT8 and FT4) and tables (FST4, FST4W, Q65 and
  ! MSK144): msgsent and the tones; itype the message type its generator
  ! gives, and fallback whether the 13-character free-text fallback applied.
  ! A single tone, nfreq, is tones(1).
  subroutine generate_(req, message, nfreq, ft_tables, tables, msgsent, tones, &
       ntones, itype, fallback)
    type(slot_request), intent(in)  :: req
    character(len=37),  intent(in)  :: message
    integer,            intent(in)  :: nfreq
    type(pack77_state), target, intent(inout) :: ft_tables, tables
    character(len=37),  intent(out) :: msgsent
    integer(c_int32_t), intent(out) :: tones(206)
    integer,            intent(out) :: ntones, itype
    logical,            intent(out) :: fallback
    character(len=22) :: message22, msgsent22
    character(kind=c_char) :: msg00(23), msgsent0(23)
    character(len=77) :: c77
    integer(kind=1) :: msgbits77(77), msgbits101(101)
    integer :: ichk, i3, n3, iwspr, iflag, status, i, itone6(6)
    integer(c_int) :: ichk_c, itype_c, itone65(126)
    type(pack77_state), pointer :: knowledge

    msgsent = ' '
    tones = 0
    ntones = 0
    itype = 0
    ichk = 0
    fallback = .false.
    message22 = message(1:22)

    if (req%single) then
       ! The JT4, JT9, JT65 and Q65 encoders' single tone; JT9's leaves the
       ! GUI's blank message, and JT65's its own last one.
       tones(1) = nfreq
       ntones = 1
       if (req%mode .eq. 4) msgsent = message22
       if (req%mode .eq. 66) write(msgsent, '(i5,'' Hz'')') nfreq
       select case (req%mode)
       case (4)
          ntones = 206
       case (9, 66)
          ntones = 85
       case default
          ntones = 126
       end select
       return
    end if
    select case (req%mode)
    case (MODE_TUNE)
       msgsent = 'TUNE'
       ntones = 1
       return
    case (MODE_ECHO)
       msgsent = 'ECHO'
       ntones = 1
       if (req%kind .ne. 1) then
          call gen_echocall(message(1:6), itone6)
          tones(1:6) = itone6
          ntones = 6
       end if
       return
    end select

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

  ! s with a to z in upper case.
  function upper_(s) result(t)
    character(len=*), intent(in) :: s
    character(len=len(s)) :: t
    integer :: i
    t = s
    do i = 1, len(t)
       if (t(i:i) .ge. 'a' .and. t(i:i) .le. 'z') t(i:i) = achar(iachar(t(i:i)) - 32)
    end do
  end function upper_

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
    case (MODE_ECHO)
       name = 'ECHO'
    case (MODE_TUNE)
       name = 'TUNE'
    case (MODE_FOX)
       name = 'FOX'
    case (MODE_SUPERFOX)
       name = 'SUPERFOX'
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
