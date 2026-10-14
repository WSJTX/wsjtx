! Streaming I/O — framed audio + control on stdin.
!
! Reads framed messages from stdin. After a
! one-time 8-byte WSJT session header (4-byte magic 'WSJT' + 1-byte fmt
! + 1-byte channels + 2-byte rate_kHz LE), stdin carries a sequence of
! frames:
!
!   [type:1][len:4 LE][body:len bytes]
!
! Frame types:
!   0x01  audio chunk     body = N samples in declared format
!   0x02  control JSON    body = UTF-8 JSON line (no trailing newline)
!
! Other types are skipped (forward-compat: consumers ignore unknown
! frame types just as NDJSON consumers ignore unknown "t" values).
!
! Control frames recognized today: {"t":"configure",...}, {"t":"halt"},
! {"t":"discontinuity"}. See lib/streaming_control.f90 for the parser + schema.
!
! Apply policy:
!   configure : applied at receive. A change of mode or period starts a
!               new period; what was in flight is discarded. Producers
!               send `configure` first; audio is interpreted under the
!               most recent one. A declined configure applies none of
!               its keys.
!   halt      : drain the current period (decode if it has samples), exit.
!
! A period is TRperiod * 12 kHz samples (lib/streaming_period.f90). Keep a
! frame remainder for the next period; retain only the first npts samples
! for decoding. The reader's phase is that of the first audio after
! `configure`; send every sample.
!
! JTTY ({"mode":"JTTY"}; no integer code or command-line flag selects it) has
! no periods: its audio goes to lib/streaming_jtty.f90, which states its
! output. A JTTY session ends at a discontinuity, an odd-length audio frame, a
! configure that leaves JTTY, halt and end of input; the period modes ignore
! discontinuity. Entering JTTY, rxfreq carries over, ntol becomes 20 and nfa
! and nfb their values at the start of the stream (-L and -H, 200 and 4000 Hz
! by default; with -9 and an -S other than 2700, nfa is -S) unless the frame
! sets them. Any configure that carries mode or trperiod and stays in JTTY
! keeps the session but resets in the same way whichever of nfa, nfb and ntol
! it does not set. trperiod is validated and unused. A configure that would
! leave rxfreq, ntol, nfa or nfb outside 0 to 6000 Hz in JTTY is declined
! (configure_range_error).
!
! Format support: fmt=0x00 (int16 PCM) only. Other formats may be
! added later.
!
! Platform: reads stdin through a portable C read-fully helper on fd 0
! (stream_stdin_read_fully, lib/stream_stdin.c) — the same code path on
! every platform. Fortran unit I/O cannot read a redirected stdin
! portably: native Windows has no /dev/stdin, and re-OPENing the
! preconnected unit 5 with ACCESS='STREAM' is non-conforming (F2018
! 12.5.6.1 — ACCESS is not a changeable mode), rejected by libgfortran
! everywhere. Binary mode is forced on Windows first via
! stream_set_stdin_binary (lib/stream_setmode.c; no-op on POSIX). The
! helper loops read() until the request fills or the stream truly ends,
! so a pipe short-read is never mistaken for EOF.

subroutine jt9_stream(shared_data, mode, TRperiod)
  use, intrinsic :: iso_fortran_env, only: int8, int16, int64, error_unit
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use, intrinsic :: iso_c_binding, only: c_int64_t
  use prog_args, only: data_dir
  use timer_module, only: timer
  use streaming_emit, only: streaming_emit_ready,                          &
       streaming_emit_decode_finished, streaming_emit_error,               &
       streaming_emit_error_code, streaming_emit_error_version,            &
       streaming_emit_error_type, streaming_emit_set_time_form
  use streaming_control, only: parse_control_frame, configure_fields,      &
       control_type_error, CTRL_CONFIGURE, CTRL_HALT, CTRL_PARSE_ERR,      &
       CTRL_DISCONTINUITY, MODE_JTTY
  use streaming_apply, only: apply_configure_fields
  use streaming_jtty, only: jtty_audio, jtty_end, jtty_range_key,          &
       jtty_emit_range_error, jtty_release
  use streaming_period, only: period_state, period_begin, period_room,   &
       period_take, period_full, period_ready

  include 'jt9com.f90'

  type(dec_data) :: shared_data
  integer        :: mode
  real(8)        :: TRperiod

  integer, parameter :: HDR_LEN = 8
  integer, parameter :: NFSAMPLE = 12000
  integer, parameter :: FRAME_AUDIO   = 1
  integer, parameter :: FRAME_CONTROL = 2
  integer, parameter :: CTL_BUF_LEN = 1024
  ! 4-byte session magic "WSJT" (W=0x57 S=0x53 J=0x4A T=0x54)
  integer(int8), parameter :: MAGIC(4) = [                                 &
       int(z'57', int8), int(z'53', int8),                                 &
       int(z'4A', int8), int(z'54', int8) ]

  integer :: ios, fmt, ch, rate_khz
  integer(int8)  :: hdr(HDR_LEN), type_byte(1), len_bytes(4)
  integer(int64) :: frame_len_wide, max_audio_frame_bytes
  integer        :: frame_len

  integer :: nsps, kstep, nhsym, nhsym0
  ! The period's sample accounting; `body_left` carries a straddling
  ! frame's remainder into the next period.
  type(period_state) :: period
  ! This period's decode has run.
  logical :: decoded
  integer :: ingain, nminw, ihsym, npts8
  real    :: pxdb, df3, pxdbmax, s(NSMAX)
  integer :: npct_unused
  integer :: body_left, take_bytes, take_samples
  integer(int16) :: chunk(4096)
  integer(int8)  :: byte_sink(4096)
  ! FT8 progressive-decode working buffer (mirrors jt9.f90:19's id2a) — the
  ! FT8 decoder's `dd` array is populated only on nzhsym <= 47 calls, so we
  ! mirror the WAV path's 41/47/50 call cadence with a working copy zeroed
  ! past the per-call sample boundary.
  integer(int16), save :: id2a(180000)  !Keep this big array off the stack
  character(len=CTL_BUF_LEN) :: ctl_buf
  integer(int8)  :: ctl_bytes(CTL_BUF_LEN)
  integer :: i_ctl, prev_mode, next_mode
  real(8) :: prev_TRperiod, next_TRperiod
  type(params_block) :: next_params
  character(len=:), allocatable :: range_key
  ! Audio samples consumed in any mode; JTTY's origin and times count them.
  integer(int64) :: stream_samples
  ! Session-baseline nfa/nfb stashed after jt9.f90's init_streaming_extra_fields
  ! returned. apply_configure_fields restores these on mode change with no
  ! explicit consumer override; passed in as dummy args.
  integer :: baseline_nfa, baseline_nfb
  logical :: bLowSidelobes, eof_period, halt_req
  type(configure_fields)   :: cfg
  type(control_type_error) :: terr
  integer :: action

  ! C stdin helpers. stream_set_stdin_binary (lib/stream_setmode.c) is a
  ! no-op on POSIX; stream_stdin_read_fully (lib/stream_stdin.c) is the
  ! read-fully byte reader used on all platforms.
  interface
     subroutine stream_set_stdin_binary() bind(C, name="stream_set_stdin_binary")
     end subroutine stream_set_stdin_binary
     function stream_stdin_read_fully(buf, n) result(nread)               &
          bind(C, name="stream_stdin_read_fully")
       import :: c_int64_t
       type(*), dimension(*), intent(inout) :: buf
       integer(c_int64_t), value :: n
       integer(c_int64_t) :: nread
     end function stream_stdin_read_fully
  end interface

  bLowSidelobes = .false.
  ingain        = 0
  nminw         = 1

  ! Read the framed byte stream from stdin via the C helper (stdin_read
  ! below) — no Fortran unit is opened. Binary mode must be forced before
  ! the first read on Windows or the CRT stops at 0x1A and rewrites CRLF.
  call stream_set_stdin_binary()

  call stdin_read(hdr, HDR_LEN, ios)
  if (ios /= 0) then
     call streaming_emit_error('short header (need 8 bytes)')
     write(error_unit, '(a)') 'wsjt-codec --stream: short header (need 8 bytes)'
     stop 1
  end if

  if (any(hdr(1:4) /= MAGIC)) then
     call streaming_emit_error('bad magic (expected ''WSJT'')')
     write(error_unit, '(a)') 'wsjt-codec --stream: bad magic (expected ''WSJT'')'
     stop 1
  end if

  fmt      = iand(int(hdr(5)), 255)
  ch       = iand(int(hdr(6)), 255)
  rate_khz = iand(int(hdr(7)), 255) + ishft(iand(int(hdr(8)), 255), 8)

  if (fmt /= 0) then
     call streaming_emit_error('unsupported format (only fmt=0 int16 PCM supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'wsjt-codec --stream: unsupported fmt=', fmt, ' (only int16 PCM supported)'
     stop 1
  end if

  if (ch /= 1) then
     call streaming_emit_error('unsupported channel count (only mono supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'wsjt-codec --stream: unsupported channels=', ch, ' (only mono supported)'
     stop 1
  end if

  if (rate_khz /= 12) then
     call streaming_emit_error('unsupported sample rate (only 12 kHz supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'wsjt-codec --stream: unsupported rate=', rate_khz, ' kHz (only 12 kHz supported)'
     stop 1
  end if

  write(error_unit, '(a,i0,a,i0,a,i0,a)')                                  &
       'wsjt-codec --stream: header ok (fmt=', fmt, ' ch=', ch,            &
       ' rate=', rate_khz, ' kHz)'

  nsps  = 6912
  kstep = nsps / 2

  ! Streaming-specific params override. The full params init was already
  ! performed in jt9.f90's stream-mode dispatch arm via
  ! init_default_params + init_streaming_extra_fields;
  ! all that remains here is the streaming-vs-WAV diff.
  !
  ! CRITICAL: ndiskdat = .true. Without it, ft8_decode.f90:141 bails out at
  ! tseq >= 14.3s ("real-time mode, leave time for next period") — but
  ! streaming calls multimode_decoder once per period at end-of-period when
  ! tseq >= 15.0, so the bail ALWAYS fires and zero decodes are produced.
  ! ndiskdat=.true. tells the decoder "complete-period one-shot, don't bail".
  shared_data%params%ndiskdat = .true.

  ! Stash session-baseline nfa/nfb. apply_configure_fields
  ! restores these on mode change with no explicit consumer override —
  ! mirrors WAV semantics where each invocation starts from CLI defaults.
  baseline_nfa = shared_data%params%nfa
  baseline_nfb = shared_data%params%nfb

  call streaming_emit_ready()
  max_audio_frame_bytes = 2_int64 * int(size(shared_data%id2), int64)
  body_left = 0
  stream_samples = 0

  ! ===== Outer period loop ===========================================
  do
     call period_begin(period, TRperiod, mode, size(shared_data%id2))
     shared_data%id2 = 0
     nhsym      = 0
     nhsym0     = 0       ! seed at 0 so the symspec catch-up loop is O(nhsym)
     eof_period = .false.
     halt_req   = .false.
     decoded    = .false.

     ! The previous period ended inside an audio frame: its remainder is
     ! this period's first audio.
     if (body_left .gt. 0) then
        call consume_audio_body()
        if (.not. decoded .and. period_ready(period)) call run_period_decode()
     end if

     ! Inner frame loop: read frames until the period is full, halt, or EOF.
     do while (.not. period_full(period) .and. .not. halt_req .and. .not. eof_period)
        call stdin_read(type_byte, 1, ios)
        if (ios /= 0) then
           eof_period = .true.; exit
        end if
        call stdin_read(len_bytes, 4, ios)
        if (ios /= 0) then
           eof_period = .true.; exit
        end if
        frame_len_wide = iand(int(len_bytes(1), int64), int(z'FF', int64))       &
             + 256_int64 * iand(int(len_bytes(2), int64), int(z'FF', int64))     &
             + 65536_int64 * iand(int(len_bytes(3), int64), int(z'FF', int64))   &
             + 16777216_int64 * iand(int(len_bytes(4), int64), int(z'FF', int64))
        if (frame_len_wide .gt. max_audio_frame_bytes) then
           call streaming_emit_error_code('frame_too_large',                    &
                'frame length exceeds the streaming sample-buffer capacity')
           write(error_unit, '(a,i0,a,i0)') 'wsjt-codec --stream: frame length ', &
                frame_len_wide, ' exceeds capacity ', max_audio_frame_bytes
           stop 1
        end if
        frame_len = int(frame_len_wide)

        if (iand(int(type_byte(1)), 255) .eq. FRAME_AUDIO) then
           if (mod(frame_len, 2) /= 0) then
              call streaming_emit_error_code('odd_audio_frame',                &
                   'audio frame length must contain whole int16 samples')
              ! The skipped samples are a hole in the JTTY session.
              if (mode .eq. MODE_JTTY) call jtty_end()
              body_left = frame_len
              do while (body_left .gt. 0)
                 take_bytes = min(body_left, size(byte_sink))
                 call stdin_read(byte_sink, take_bytes, ios)
                 if (ios /= 0) then
                    eof_period = .true.; exit
                 end if
                 body_left = body_left - take_bytes
              end do
              cycle
           end if
           body_left = frame_len
           if (mode .eq. MODE_JTTY) then
              call consume_jtty_body()
           else
              call consume_audio_body()
              if (.not. decoded .and. period_ready(period)) call run_period_decode()
           end if

        else if (iand(int(type_byte(1)), 255) .eq. FRAME_CONTROL) then
           ! Read JSON body into char buffer (cap at CTL_BUF_LEN)
           if (frame_len .gt. CTL_BUF_LEN) then
              call streaming_emit_error_code('control_frame_too_large',        &
                   'control frame exceeds the configured buffer capacity')
              ! drain + skip
              body_left = frame_len
              do while (body_left .gt. 0)
                 take_bytes = min(body_left, size(byte_sink))
                 call stdin_read(byte_sink, take_bytes, ios)
                 if (ios /= 0) exit
                 body_left = body_left - take_bytes
              end do
              cycle
           end if
           call stdin_read(ctl_bytes, frame_len, ios)
           if (ios /= 0) then
              eof_period = .true.; exit
           end if
           ctl_buf = ' '
           do i_ctl = 1, frame_len
              ctl_buf(i_ctl:i_ctl) = achar(iand(int(ctl_bytes(i_ctl)), 255))
           end do
           call parse_control_frame(ctl_buf(1:frame_len), action, cfg, terr)
           select case (action)
           case (CTRL_HALT)
              halt_req = .true.
           case (CTRL_CONFIGURE)
              if (cfg%version_set .and. cfg%version .ne. 1) then
                 ! RFC v1 §3/§4.1: unknown schema version -> decline this frame.
                 ! Checked BEFORE per-key types: an unrecognized version means
                 ! we cannot trust the body's schema. (A wrong-TYPED version
                 ! leaves version_set=.false., so it falls through to the
                 ! type-error gate below — reported as configure_type_error
                 ! key:"version" only if version is the sole mistyped key;
                 ! otherwise the FIRST mistyped key wins, since parse records
                 ! one error and checks version last. Either way: declined.)
                 call streaming_emit_error_version(cfg%version)
                 cycle
              end if
              if (terr%present) then
                 ! RFC v1 §4.1: a key's JSON value type mismatched the schema.
                 ! Decline the ENTIRE frame — no field is applied (the apply
                 ! below is gated on reaching it; cycle skips it AND the npts/
                 ! buffer-discard logic, so a co-present valid key never takes
                 ! effect).
                 call streaming_emit_error_type(trim(terr%key),             &
                      trim(terr%expected), trim(terr%got))
                 cycle
              end if
              if (cfg%mode_invalid) then
                 call streaming_emit_error_code('unknown_mode',                 &
                      'configure frame contains an unsupported mode')
                 cycle
              end if
              if (cfg%trperiod_set .and.                                      &
                  (.not. ieee_is_finite(cfg%trperiod) .or.                     &
                   cfg%trperiod .le. 0.d0 .or.                                &
                   cfg%trperiod .gt. dble(NTMAX))) then
                 call streaming_emit_error_code('invalid_trperiod',             &
                      'trperiod must be finite and between 0 and 1800 seconds')
                 cycle
              end if
              ! Applied to a copy, so that a declined frame changes nothing.
              next_mode     = mode
              next_TRperiod = TRperiod
              next_params   = shared_data%params
              call apply_configure_fields(cfg, next_mode, next_TRperiod,   &
                   next_params, baseline_nfa, baseline_nfb)
              if (next_mode .eq. MODE_JTTY) then
                 range_key = jtty_range_key(next_params)
                 if (len(range_key) .gt. 0) then
                    call jtty_emit_range_error(range_key)
                    cycle
                 end if
              end if
              if (mode .eq. MODE_JTTY .and. next_mode .ne. MODE_JTTY) call jtty_end()
              prev_mode          = mode
              prev_TRperiod      = TRperiod
              mode               = next_mode
              TRperiod           = next_TRperiod
              shared_data%params = next_params
              call streaming_emit_set_time_form(cfg%utc_set, cfg%nutc_set, TRperiod)
              ! A configure that changes the applied mode or period starts a
              ! new period; what was in flight is discarded. Repeating the
              ! current configuration leaves the period alone.
              if (mode .ne. prev_mode .or. TRperiod .ne. prev_TRperiod) then
                 call period_begin(period, TRperiod, mode, size(shared_data%id2))
                 shared_data%id2 = 0
                 nhsym   = 0
                 nhsym0  = 0
                 decoded = .false.
              end if
           case (CTRL_DISCONTINUITY)
              if (mode .eq. MODE_JTTY) call jtty_end()
           case (CTRL_PARSE_ERR)
              call streaming_emit_error_code('configure_parse_error',          &
                   'malformed control frame (missing or invalid t field)')
           end select

        else
           ! Unknown frame type: drain body
           body_left = frame_len
           do while (body_left .gt. 0)
              take_bytes = min(body_left, size(byte_sink))
              call stdin_read(byte_sink, take_bytes, ios)
              if (ios /= 0) then
                 eof_period = .true.; exit
              end if
              body_left = body_left - take_bytes
           end do
        end if
     end do

     ! A period cut short (halt, or the producer closed the pipe) still
     ! decodes what it has — once.
     if (.not. decoded .and. period%k .gt. 0) call run_period_decode()

     if (halt_req .or. eof_period) then
        if (mode .eq. MODE_JTTY) call jtty_end()
        call jtty_release()
        return
     end if
  end do

contains

  ! This period's decode, on the `npts` samples in `id2`. Runs once per
  ! period: when the decoder's share is in, or from the drain when the
  ! period ends short of it.
  subroutine run_period_decode()
    decoded = .true.
    shared_data%params%newdat = .true.
    shared_data%params%ntr = int(TRperiod)
    shared_data%params%nzhsym = nhsym
    ! FT8 override: decoder is tuned for nzhsym=50 (cf. jt9.f90:552).
    if (mode .eq. 8) shared_data%params%nzhsym = 50
    ! Non-FT8/FST4/Q65: cap at 181 to match WAV-path exit (ss array bound).
    if (mode .ne. 8   .and. mode .ne. 240 .and. mode .ne. 241 .and.    &
        mode .ne. 242 .and. mode .ne. 66  .and. shared_data%params%nzhsym .gt. 181) then
       shared_data%params%nzhsym = 181
    end if
    shared_data%params%kin    = 64800
    if (mode .eq. 4 .or. mode .eq. 5 .or. mode .eq. 9 .or. mode .eq. 65 .or. mode .eq. 66 .or. &
         mode .eq. 240 .or. mode .eq. 241 .or. mode .eq. 242) &
         shared_data%params%kin = period%k

    if (mode .eq. 144) then
       call decode_msk144(shared_data%id2, shared_data%params, data_dir)
    else if (mode .eq. 8 .and. .not. shared_data%params%lmultift8) then
       ! FT8 progressive 41/47/50 sequence (mirrors jt9.f90:562-581).
       ! Without the early calls, ft8_decode.f90's saved `dd` array stays
       ! unset and the final nzhsym=50 call decodes silence. With the
       ! sequence, dd is populated on the nzhsym=41/47 passes and the
       ! nzhsym=50 final pass produces real decodes.
       shared_data%params%nzhsym = 41
       id2a(1:41*3456) = shared_data%id2(1:41*3456)
       id2a(41*3456+1:) = 0
       call multimode_decoder(shared_data%ss, id2a, shared_data%params, NFSAMPLE)

       shared_data%params%nzhsym = 47
       id2a(1:47*3456) = shared_data%id2(1:47*3456)
       id2a(47*3456+1:) = 0
       call multimode_decoder(shared_data%ss, id2a, shared_data%params, NFSAMPLE)

       shared_data%params%nzhsym = 50
       id2a(1:50*3456) = shared_data%id2(1:50*3456)
       id2a(50*3456+1:) = 0
       call multimode_decoder(shared_data%ss, id2a, shared_data%params, NFSAMPLE)
    else
       call multimode_decoder(shared_data%ss, shared_data%id2,         &
            shared_data%params, NFSAMPLE)
    end if
    if(mode==66) shared_data%params%nclearave=.false.
  end subroutine run_period_decode

  ! Stop at the period boundary; body_left carries the remaining frame
  ! forward.
  subroutine consume_audio_body()
    integer :: k0, nacc
    do while (body_left .gt. 0 .and. .not. period_full(period))
       take_samples = period_room(period, min(body_left / 2, size(chunk)))
       take_bytes   = 2 * take_samples
       call stdin_read(chunk, take_bytes, ios)
       if (ios /= 0) then
          eof_period = .true.; return
       end if
       body_left = body_left - take_bytes
       stream_samples = stream_samples + take_samples
       k0 = period%k
       call period_take(period, take_samples, nacc)
       if (nacc .gt. 0) then
          shared_data%id2(k0+1 : k0+nacc) = chunk(1:nacc)
          ! Incremental symspec for JT9-family at kstep boundaries.
          ! Cap at nhsym=181 to match the WAV-path exit condition
          ! (jt9.f90:442-443) — exceeding it overruns ss(184,*).
          do while ((period%k - 2048) / kstep .gt. nhsym0 .and. nhsym0 .lt. 181)
             nhsym0 = nhsym0 + 1
             if (nhsym0 .lt. 1) cycle
             nhsym = nhsym0
             if (mode .eq. 74) then
                call timer('symspec ', 0)
                call symspec(shared_data, nhsym*kstep + 2048, nsps,        &
                     ingain, bLowSidelobes, nminw, pxdb, s, df3,           &
                     ihsym, npts8, pxdbmax, npct_unused)
                call timer('symspec ', 1)
             end if
          end do
       end if
    end do
  end subroutine consume_audio_body

  ! JTTY has no periods: the whole frame goes to the session.
  subroutine consume_jtty_body()
    do while (body_left .gt. 0)
       take_samples = min(body_left / 2, size(chunk))
       take_bytes   = 2 * take_samples
       call stdin_read(chunk, take_bytes, ios)
       if (ios /= 0) then
          eof_period = .true.; return
       end if
       body_left = body_left - take_bytes
       call jtty_audio(chunk(1:take_samples), stream_samples, shared_data%params)
       stream_samples = stream_samples + take_samples
    end do
  end subroutine consume_jtty_body

  ! Read exactly nbytes from stdin via the C helper; ios=0 on success,
  ! ios=1 on end-of-stream (short delivery — the producer closed the
  ! pipe). Every caller treats nonzero ios as end-of-stream, matching
  ! the old unformatted-stream-read iostat contract.
  subroutine stdin_read(buf, nbytes, ios)
    type(*), dimension(*), intent(inout) :: buf
    integer, intent(in)  :: nbytes
    integer, intent(out) :: ios
    ios = 0
    if (stream_stdin_read_fully(buf, int(nbytes, c_int64_t)) /=          &
         int(nbytes, c_int64_t)) ios = 1
  end subroutine stdin_read

end subroutine jt9_stream

logical function jt9_stream_available()
  jt9_stream_available = .true.
end function jt9_stream_available
