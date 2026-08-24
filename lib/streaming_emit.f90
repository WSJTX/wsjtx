! Streaming NDJSON emitter.
!
! Hand-rolled JSON output for the streaming protocol.
! Zero-dependency (no Qt, no nlohmann). One line per message on stdout.
!
! Schema (v=1):
!   ready             {"v":1,"t":"ready","modes":[...],"protocol":1}
!   decode            {"v":1,"t":"decode","mode":"FT8","time":"HHMMSS",
!                      "snr":-12,"dt":0.6,"freq":1500,"message":"CQ ..."}
!   decode_finished   {"v":1,"t":"decode_finished","period_end":"HHMMSS"}
!   error             {"v":1,"t":"error","msg":"..."}
!   error (coded)     {"v":1,"t":"error","code":"configure_parse_error","detail":"..."}
!   error (version)   {"v":1,"t":"error","code":"unknown_schema_version","got":N}
!   error (type)      {"v":1,"t":"error","code":"configure_type_error",
!                      "key":"depth","expected":"int","got":"string"}
!   warning           {"v":1,"t":"warning","code":"period_boundary_discard",
!                      "discarded_samples":N}
!
! Activation: program jt9 calls streaming_emit_set_enabled(.true.) when
! --stream is parsed. Decoder per-mode sites in lib/decoder.f90 query
! streaming_emit_enabled() and call streaming_emit_decode() instead of
! the legacy write(*,...) text format. When disabled, every routine in
! this module is a no-op so non-streaming jt9 (WAV / shmem) is unchanged.

module streaming_emit
  use, intrinsic :: iso_fortran_env, only: int64, output_unit
  implicit none
  private

  logical, save :: enabled_ = .false.

  public :: streaming_emit_set_enabled
  public :: streaming_emit_enabled
  public :: streaming_emit_ready
  public :: streaming_emit_decode
  public :: streaming_emit_decode_finished
  public :: streaming_emit_error
  public :: streaming_emit_error_code
  public :: streaming_emit_error_version
  public :: streaming_emit_error_type
  public :: streaming_emit_warning_samples

contains

  subroutine streaming_emit_set_enabled(flag)
    logical, intent(in) :: flag
    enabled_ = flag
  end subroutine streaming_emit_set_enabled

  pure function streaming_emit_enabled() result(v)
    logical :: v
    v = enabled_
  end function streaming_emit_enabled

  subroutine streaming_emit_ready()
    if (.not. enabled_) return
    ! Full jt9-internal mode support (FT8/FT4/JT9/JT65/JT4/FST4/
    ! FST4W/Q65/MSK144). Modes outside this set (e.g., wsprd C2 streaming,
    ! map65 I/Q) may extend this list later.
    write(output_unit, '(a)') &
       '{"v":1,"t":"ready","modes":["FT8","FT4","JT9","JT65","JT4","FST4","FST4W","Q65","MSK144"],"protocol":1}'
    flush(output_unit)
  end subroutine streaming_emit_ready

  ! Emit one decode line.
  !
  !   mode_str  : "FT8" / "FT4" / "JT9" / etc.  (no escaping)
  !   nutc      : 4-digit (HHMM) or 6-digit (HHMMSS) integer time
  !   snr       : SNR in dB (integer; negative allowed)
  !   dt        : DT seconds (float)
  !   freq      : decode frequency offset in Hz (integer)
  !   msg       : decoded message text (gets JSON-escaped + trimmed)
  subroutine streaming_emit_decode(mode_str, nutc, snr, dt, freq, msg)
    character(len=*), intent(in) :: mode_str
    integer,          intent(in) :: nutc
    integer,          intent(in) :: snr
    real,             intent(in) :: dt
    integer,          intent(in) :: freq
    character(len=*), intent(in) :: msg

    character(len=8)   :: time_str
    character(len=320) :: msg_escaped
    character(len=16)  :: dt_str
    integer            :: msg_len

    if (.not. enabled_) return

    call format_utc_(nutc, time_str)
    call json_escape_(msg, msg_escaped, msg_len)
    call format_dt_(dt, dt_str)

    write(output_unit, '(5a,i0,3a,i0,3a)')                                 &
         '{"v":1,"t":"decode","mode":"', trim(mode_str),                   &
         '","time":"', trim(time_str),                                     &
         '","snr":', snr,                                                  &
         ',"dt":', trim(dt_str),                                           &
         ',"freq":', freq,                                                 &
         ',"message":"', msg_escaped(1:msg_len),                           &
         '"}'
    flush(output_unit)
  end subroutine streaming_emit_decode

  subroutine streaming_emit_decode_finished(nutc)
    integer, intent(in) :: nutc
    character(len=8)    :: time_str
    if (.not. enabled_) return
    call format_utc_(nutc, time_str)
    write(output_unit, '(3a)')                                             &
         '{"v":1,"t":"decode_finished","period_end":"', trim(time_str),    &
         '"}'
    flush(output_unit)
  end subroutine streaming_emit_decode_finished

  subroutine streaming_emit_error(msg)
    character(len=*), intent(in) :: msg
    character(len=320)           :: msg_escaped
    integer                      :: msg_len
    if (.not. enabled_) return
    call json_escape_(msg, msg_escaped, msg_len)
    write(output_unit, '(3a)')                                             &
         '{"v":1,"t":"error","msg":"', msg_escaped(1:msg_len), '"}'
    flush(output_unit)
  end subroutine streaming_emit_error

  ! Structured configure-frame error (RFC v1 §4.1). Machine-readable `code`
  ! plus a free-text `detail` (JSON-escaped). Used for configure_parse_error.
  !   {"v":1,"t":"error","code":"<code>","detail":"<detail>"}
  subroutine streaming_emit_error_code(code, detail)
    character(len=*), intent(in) :: code
    character(len=*), intent(in) :: detail
    character(len=320)           :: d_escaped
    integer                      :: d_len
    if (.not. enabled_) return
    call json_escape_(detail, d_escaped, d_len)
    write(output_unit, '(5a)')                                             &
         '{"v":1,"t":"error","code":"', trim(code),                        &
         '","detail":"', d_escaped(1:d_len), '"}'
    flush(output_unit)
  end subroutine streaming_emit_error_code

  ! Unknown schema-version error (RFC v1 §3/§4.1). `got` is the rejected
  ! schema version the producer sent in the configure frame.
  !   {"v":1,"t":"error","code":"unknown_schema_version","got":<n>}
  subroutine streaming_emit_error_version(got)
    integer, intent(in) :: got
    if (.not. enabled_) return
    write(output_unit, '(a,i0,a)')                                         &
         '{"v":1,"t":"error","code":"unknown_schema_version","got":',      &
         got, '}'
    flush(output_unit)
  end subroutine streaming_emit_error_version

  ! Per-key type-mismatch error (RFC v1 §4.1). The producer sent `key` with a
  ! JSON value of type `got` where the schema expects `expected`; the whole
  ! configure frame is declined (no partial application). `key`/`expected`/`got`
  ! are controlled-vocabulary ASCII tokens (no escaping needed).
  !   {"v":1,"t":"error","code":"configure_type_error","key":"<k>",
  !    "expected":"<e>","got":"<g>"}
  subroutine streaming_emit_error_type(key, expected, got)
    character(len=*), intent(in) :: key, expected, got
    if (.not. enabled_) return
    write(output_unit, '(7a)')                                             &
         '{"v":1,"t":"error","code":"configure_type_error","key":"',       &
         trim(key), '","expected":"', trim(expected),                      &
         '","got":"', trim(got), '"}'
    flush(output_unit)
  end subroutine streaming_emit_error_type

  subroutine streaming_emit_warning_samples(code, discarded_samples)
    character(len=*), intent(in) :: code
    integer(int64),   intent(in) :: discarded_samples
    if (.not. enabled_) return
    write(output_unit, '(3a,i0,a)')                                        &
         '{"v":1,"t":"warning","code":"', trim(code),                 &
         '","discarded_samples":', discarded_samples, '}'
    flush(output_unit)
  end subroutine streaming_emit_warning_samples

  ! Internals -------------------------------------------------------------

  ! Format dt (real seconds) as a JSON-safe number. gfortran's f0.x format
  ! omits the leading zero (".25" instead of "0.25"), which violates RFC
  ! 8259. This wrapper formats with f0.2, then re-inserts a leading zero
  ! when the result starts with "." or "-.".
  subroutine format_dt_(dt, str)
    real,             intent(in)  :: dt
    character(len=*), intent(out) :: str
    character(len=16) :: tmp
    write(tmp, '(f0.2)') dt
    if (tmp(1:1) .eq. '.') then
       str = '0' // trim(tmp)
    else if (tmp(1:2) .eq. '-.') then
       str = '-0' // trim(tmp(2:))
    else
       str = trim(tmp)
    end if
  end subroutine format_dt_

  ! Format the integer nutc time as 6 chars. 4-digit HHMM gets
  ! zero-extended to HHMM00 so consumers always see a 6-char field.
  subroutine format_utc_(nutc, str)
    integer,          intent(in)  :: nutc
    character(len=*), intent(out) :: str
    integer :: hh, mm, ss
    str = '000000'
    if (nutc .ge. 100000) then
       hh = nutc / 10000
       mm = mod(nutc/100, 100)
       ss = mod(nutc, 100)
       write(str, '(i2.2,i2.2,i2.2)') hh, mm, ss
    else if (nutc .ge. 0) then
       hh = nutc / 100
       mm = mod(nutc, 100)
       write(str, '(i2.2,i2.2,a2)') hh, mm, '00'
    end if
  end subroutine format_utc_

  ! Minimal RFC-8259 JSON string escape covering the chars that can
  ! appear in WSJT decode payloads:
  !   - "  ->  \"
  !   - \  ->  \\
  !   - \b \t \n \f \r  ->  \b \t \n \f \r
  !   - other 0..0x1F   ->  \u00xx
  !   - everything else (incl. printable ASCII, UTF-8 bytes) passes through
  !
  ! Trailing trim: the input is silently trimmed to its non-blank length
  ! before escaping (decoder messages are right-padded fixed-width strings).
  ! Output buffer must be large enough for worst case 6x input len.
  subroutine json_escape_(in_str, out_str, out_len)
    character(len=*), intent(in)  :: in_str
    character(len=*), intent(out) :: out_str
    integer,          intent(out) :: out_len

    integer :: i, n, code
    character :: c

    n = len_trim(in_str)
    out_len = 0
    out_str = ' '

    do i = 1, n
       c = in_str(i:i)
       code = iachar(c)
       select case (code)
       case (iachar('"'))
          call append_pair_('\', '"', out_str, out_len)
       case (iachar('\'))
          call append_pair_('\', '\', out_str, out_len)
       case (8)   ! BS
          call append_pair_('\', 'b', out_str, out_len)
       case (9)   ! HT
          call append_pair_('\', 't', out_str, out_len)
       case (10)  ! LF
          call append_pair_('\', 'n', out_str, out_len)
       case (12)  ! FF
          call append_pair_('\', 'f', out_str, out_len)
       case (13)  ! CR
          call append_pair_('\', 'r', out_str, out_len)
       case (0:7, 11, 14:31)
          call append_unicode_(code, out_str, out_len)
       case default
          out_len = out_len + 1
          out_str(out_len:out_len) = c
       end select
    end do
  end subroutine json_escape_

  subroutine append_pair_(a, b, buf, idx)
    character,        intent(in)    :: a, b
    character(len=*), intent(inout) :: buf
    integer,          intent(inout) :: idx
    idx = idx + 1; buf(idx:idx) = a
    idx = idx + 1; buf(idx:idx) = b
  end subroutine append_pair_

  subroutine append_unicode_(code, buf, idx)
    integer,          intent(in)    :: code
    character(len=*), intent(inout) :: buf
    integer,          intent(inout) :: idx
    character(len=4) :: hex4
    write(hex4, '(z4.4)') code
    idx = idx + 1; buf(idx:idx) = '\'
    idx = idx + 1; buf(idx:idx) = 'u'
    buf(idx+1:idx+4) = hex4
    idx = idx + 4
  end subroutine append_unicode_

end module streaming_emit
