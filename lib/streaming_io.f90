! Streaming I/O — framed audio + control on stdin.
!
! Reads framed messages from stdin. After a
! one-time 8-byte WSJL session header (4-byte magic 'WSJL' + 1-byte fmt
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
! Control frames recognized today: {"t":"configure",...}, {"t":"halt"}.
! See lib/streaming_control.f90 for the parser + schema.
!
! Apply policy:
!   configure : applied immediately at receive. If mode changes, the
!               in-flight sample accumulator is discarded (k=0) — the
!               buffered audio is wrong-format for the new mode anyway.
!               Producers should send `configure` FIRST, before any
!               audio frames; subsequent audio is interpreted under the
!               most recent configure.
!   halt      : drain current period (call decoder if k>0), exit clean.
!
! Period boundary: when k accumulates to `npts` samples (mode-dependent),
! we run the decoder. Audio frames that overflow `npts` have the excess
! discarded — producers should chunk audio so that any single frame fits
! within one period, OR send exactly `npts` samples per period. An
! external producer should honor this.
!
! Format support: fmt=0x00 (int16 PCM) only. Other formats may be
! added later.
!
! Platform: reads stdin via stream-access unformatted I/O. POSIX (Linux,
! macOS) opens /dev/stdin; Windows / no /dev/stdin falls back to the
! preconnected stdin unit 5, with binary mode forced first via the C helper
! stream_set_stdin_binary (lib/stream_setmode.c; no-op on POSIX). The Windows
! fallback path is not yet exercised by CI — it needs a Windows runner.

subroutine jt9_stream(shared_data, mode, TRperiod)
  use, intrinsic :: iso_fortran_env, only: int8, int16, int32, error_unit
  use prog_args, only: data_dir
  use timer_module, only: timer
  use streaming_emit, only: streaming_emit_ready,                          &
       streaming_emit_decode_finished, streaming_emit_error,               &
       streaming_emit_error_code, streaming_emit_error_version,            &
       streaming_emit_error_type
  use streaming_control, only: parse_control_frame, configure_fields,      &
       control_type_error, CTRL_CONFIGURE, CTRL_HALT, CTRL_PARSE_ERR
  use streaming_apply, only: apply_configure_fields

  include 'jt9com.f90'

  type(dec_data) :: shared_data
  integer        :: mode
  real(8)        :: TRperiod

  integer, parameter :: HDR_LEN = 8
  integer, parameter :: NFSAMPLE = 12000
  integer, parameter :: FRAME_AUDIO   = 1
  integer, parameter :: FRAME_CONTROL = 2
  integer, parameter :: CTL_BUF_LEN = 1024
  integer(int8), parameter :: MAGIC(4) = [                                 &
       int(z'57', int8), int(z'53', int8),                                 &
       int(z'4A', int8), int(z'4C', int8) ]

  integer :: lu, ios, fmt, ch, rate_khz
  integer(int8)  :: hdr(HDR_LEN), type_byte, len_bytes(4)
  integer(int32) :: frame_len

  integer :: nsps, kstep, npts, k, nhsym, nhsym0
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
  integer(int16) :: id2a(180000)
  character(len=CTL_BUF_LEN) :: ctl_buf
  integer(int8)  :: ctl_bytes(CTL_BUF_LEN)
  integer :: i_ctl, prev_mode
  ! Session-baseline nfa/nfb stashed after jt9.f90's init_streaming_extra_fields
  ! returned. apply_configure_fields restores these on mode change with no
  ! explicit consumer override; passed in as dummy args.
  integer :: baseline_nfa, baseline_nfb
  logical :: bLowSidelobes, eof_period, halt_req
  type(configure_fields)   :: cfg
  type(control_type_error) :: terr
  integer :: action

  ! Windows binary-mode helper (lib/stream_setmode.c). No-op on POSIX.
  interface
     subroutine stream_set_stdin_binary() bind(C, name="stream_set_stdin_binary")
     end subroutine stream_set_stdin_binary
  end interface

  bLowSidelobes = .false.
  ingain        = 0
  nminw         = 1

  ! Read the framed byte stream from stdin. POSIX (Linux/macOS): open
  ! /dev/stdin as an unformatted stream. Windows / no /dev/stdin: fall back to
  ! the preconnected stdin unit 5 (binary mode already forced above). The 6
  ! reads below operate on `lu` and are unchanged by which path opened it.
  call stream_set_stdin_binary()
  open(newunit=lu, file='/dev/stdin', access='stream',                     &
       form='unformatted', status='old', action='read', iostat=ios)
  if (ios /= 0) then
     lu = 5
     open(unit=lu, access='stream', form='unformatted',                    &
          action='read', iostat=ios)
  end if
  if (ios /= 0) then
     call streaming_emit_error('cannot open stdin')
     write(error_unit, '(a)') 'jt9 --stream: cannot open stdin'
     stop 1
  end if

  read(lu, iostat=ios) hdr
  if (ios /= 0) then
     call streaming_emit_error('short header (need 8 bytes)')
     write(error_unit, '(a)') 'jt9 --stream: short header (need 8 bytes)'
     close(lu); stop 1
  end if

  if (any(hdr(1:4) /= MAGIC)) then
     call streaming_emit_error('bad magic (expected ''WSJL'')')
     write(error_unit, '(a)') 'jt9 --stream: bad magic (expected ''WSJL'')'
     close(lu); stop 1
  end if

  fmt      = iand(int(hdr(5)), 255)
  ch       = iand(int(hdr(6)), 255)
  rate_khz = iand(int(hdr(7)), 255) + ishft(iand(int(hdr(8)), 255), 8)

  if (fmt /= 0) then
     call streaming_emit_error('unsupported format (only fmt=0 int16 PCM supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'jt9 --stream: unsupported fmt=', fmt, ' (only int16 PCM supported)'
     close(lu); stop 1
  end if

  if (ch /= 1) then
     call streaming_emit_error('unsupported channel count (only mono supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'jt9 --stream: unsupported channels=', ch, ' (only mono supported)'
     close(lu); stop 1
  end if

  if (rate_khz /= 12) then
     call streaming_emit_error('unsupported sample rate (only 12 kHz supported)')
     write(error_unit, '(a,i0,a)')                                         &
          'jt9 --stream: unsupported rate=', rate_khz, ' kHz (only 12 kHz supported)'
     close(lu); stop 1
  end if

  write(error_unit, '(a,i0,a,i0,a,i0,a)')                                  &
       'jt9 --stream: header ok (fmt=', fmt, ' ch=', ch,                   &
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

  ! ===== Outer period loop ===========================================
  do
     ! Compute per-period sample target. (Same multi-mode logic.)
     npts = int(TRperiod * NFSAMPLE)
     if (mode .eq. 5)  npts = 21 * 3456
     if (npts > size(shared_data%id2)) npts = size(shared_data%id2)
     if (npts < 1) npts = NFSAMPLE
     if (mode .eq. 8 .and. npts .gt. 50 * 3456) npts = 50 * 3456

     shared_data%id2 = 0
     k          = 0
     nhsym      = 0
     nhsym0     = 0       ! seed at 0 so the symspec catch-up loop is O(nhsym)
     eof_period = .false.
     halt_req   = .false.

     ! Inner frame loop: read frames until period full, halt, or EOF.
     do while (k .lt. npts .and. .not. halt_req .and. .not. eof_period)
        read(lu, iostat=ios) type_byte
        if (ios /= 0) then
           eof_period = .true.; exit
        end if
        read(lu, iostat=ios) len_bytes
        if (ios /= 0) then
           eof_period = .true.; exit
        end if
        frame_len = iand(int(len_bytes(1), int32), int(z'FF', int32))                  &
             + ishft(iand(int(len_bytes(2), int32), int(z'FF', int32)),  8)            &
             + ishft(iand(int(len_bytes(3), int32), int(z'FF', int32)), 16)            &
             + ishft(iand(int(len_bytes(4), int32), int(z'FF', int32)), 24)
        if (frame_len .lt. 0) then
           call streaming_emit_error('negative frame length')
           eof_period = .true.; exit
        end if

        if (iand(int(type_byte), 255) .eq. FRAME_AUDIO) then
           if (mod(frame_len, 2_int32) /= 0) then
              call streaming_emit_error('odd audio frame length')
              body_left = frame_len
              do while (body_left .gt. 0)
                 take_bytes = min(body_left, size(byte_sink))
                 read(lu, iostat=ios) byte_sink(1 : take_bytes)
                 if (ios /= 0) then
                    eof_period = .true.; exit
                 end if
                 body_left = body_left - take_bytes
              end do
              cycle
           end if
           body_left = frame_len
           do while (body_left .gt. 0)
              if (k .lt. npts) then
                 take_bytes   = min(body_left, 2 * size(chunk))
                 take_samples = take_bytes / 2
                 read(lu, iostat=ios) chunk(1:take_samples)
                 if (ios /= 0) then
                    eof_period = .true.; exit
                 end if
                 if (k + take_samples .gt. npts) take_samples = npts - k
                 shared_data%id2(k+1 : k+take_samples) =                   &
                      chunk(1:take_samples)
                 k = k + take_samples
                 body_left = body_left - take_bytes
                 ! Incremental symspec for JT9-family at kstep boundaries.
                 ! Cap at nhsym=181 to match the WAV-path exit condition
                 ! (jt9.f90:442-443) — exceeding it overruns ss(184,*).
                 do while ((k - 2048) / kstep .gt. nhsym0 .and. nhsym0 .lt. 181)
                    nhsym0 = nhsym0 + 1
                    if (nhsym0 .lt. 1) cycle
                    nhsym = nhsym0
                    if (mode .eq. 9 .or. mode .eq. 74) then
                       call timer('symspec ', 0)
                       call symspec(shared_data, nhsym*kstep + 2048, nsps, &
                            ingain, bLowSidelobes, nminw, pxdb, s, df3,   &
                            ihsym, npts8, pxdbmax, npct_unused)
                       call timer('symspec ', 1)
                    end if
                 end do
              else
                 ! Period full: drain the rest of this audio frame.
                 take_bytes = min(body_left, size(byte_sink))
                 read(lu, iostat=ios) byte_sink(1 : take_bytes)
                 if (ios /= 0) then
                    eof_period = .true.; exit
                 end if
                 body_left = body_left - take_bytes
              end if
           end do

        else if (iand(int(type_byte), 255) .eq. FRAME_CONTROL) then
           ! Read JSON body into char buffer (cap at CTL_BUF_LEN)
           if (frame_len .gt. CTL_BUF_LEN) then
              call streaming_emit_error('control frame too large')
              ! drain + skip
              body_left = frame_len
              do while (body_left .gt. 0)
                 take_bytes = min(body_left, size(byte_sink))
                 read(lu, iostat=ios) byte_sink(1 : take_bytes)
                 if (ios /= 0) exit
                 body_left = body_left - take_bytes
              end do
              cycle
           end if
           read(lu, iostat=ios) ctl_bytes(1:frame_len)
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
              prev_mode = mode
              call apply_configure_fields(cfg, mode, TRperiod,             &
                   shared_data%params, baseline_nfa, baseline_nfb)
              ! Mode changed: in-flight samples are wrong format. Discard
              ! and recompute npts. Producer should configure FIRST so
              ! this branch is rare/diagnostic.
              if (cfg%mode_set .and. mode .ne. prev_mode) then
                 npts = int(TRperiod * NFSAMPLE)
                 if (mode .eq. 5) npts = 21 * 3456
                 if (npts .gt. size(shared_data%id2)) npts = size(shared_data%id2)
                 if (npts .lt. 1) npts = NFSAMPLE
                 if (mode .eq. 8 .and. npts .gt. 50 * 3456) npts = 50 * 3456
                 shared_data%id2 = 0
                 k       = 0
                 nhsym   = 0
                 nhsym0  = 0
              else if (cfg%trperiod_set) then
                 ! TRperiod-only change: recompute npts but keep accumulated samples.
                 npts = int(TRperiod * NFSAMPLE)
                 if (mode .eq. 5) npts = 21 * 3456
                 if (mode .eq. 8 .and. npts .gt. 50 * 3456) npts = 50 * 3456
                 if (npts .gt. size(shared_data%id2)) npts = size(shared_data%id2)
              end if
           case (CTRL_PARSE_ERR)
              call streaming_emit_error_code('configure_parse_error',          &
                   'malformed control frame (missing or invalid t field)')
           end select

        else
           ! Unknown frame type: drain body
           body_left = frame_len
           do while (body_left .gt. 0)
              take_bytes = min(body_left, size(byte_sink))
              read(lu, iostat=ios) byte_sink(1 : take_bytes)
              if (ios /= 0) then
                 eof_period = .true.; exit
              end if
              body_left = body_left - take_bytes
           end do
        end if
     end do

     ! Run decoder if we accumulated any samples this period.
     if (k .gt. 0) then
        shared_data%params%newdat = .true.
        shared_data%params%nzhsym = nhsym
        ! FT8 override: decoder is tuned for nzhsym=50 (cf. jt9.f90:552).
        if (mode .eq. 8) shared_data%params%nzhsym = 50
        ! Non-FT8/FST4/Q65: cap at 181 to match WAV-path exit (ss array bound).
        if (mode .ne. 8   .and. mode .ne. 240 .and. mode .ne. 241 .and.    &
            mode .ne. 242 .and. mode .ne. 66  .and. shared_data%params%nzhsym .gt. 181) then
           shared_data%params%nzhsym = 181
        end if
        shared_data%params%kin    = 64800
        if (mode .eq. 240) shared_data%params%kin = 720000
        if (mode .eq. 241) shared_data%params%kin = 720000
        if (mode .eq. 242) shared_data%params%kin = 720000

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
     end if

     if (halt_req .or. eof_period) then
        close(lu)
        return
     end if
  end do

end subroutine jt9_stream
