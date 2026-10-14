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
! session line and its last update only error lines appear; an
! odd_audio_frame error comes before the "ended" lines it causes. I is unique
! in the process, across sessions and mode changes, and the same on every
! update of one message. A message's last line is its "complete", "expired"
! or "ended" one. A message whose final frame has not arrived is "expired"
! once about 8 s of audio follow its latest frame, and ending a session gives
! each message still in progress an "ended" line; after a fatal error
! (exit 1) nothing follows, so messages in progress get no terminal line.
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
! The decoder searches rxfreq +/- ntol, and two 300 Hz channels centred on
! 1350 and 1650 Hz, each centre first moved into nfa..nfb and the channel
! clipped to it (neither when nfa > nfb); with the default nfa and nfb, 1200
! to 1800 Hz. It works on the audio resampled to 6 kHz, so nothing whose
! lowest tone is above about 2950 Hz decodes, as in the GUI. A configure that
! would leave rxfreq, ntol, nfa or nfb outside 0 to 6000 Hz is declined
! (jtty_range_key): that is the range the keys accept, not the band searched.
!
! A decoder step that needs FFT sizes with no saved FFTW wisdom in -a measures
! them: up to tens of seconds on ARM Linux, during which the stream reads no
! input. The sizes follow from rxfreq, ntol, nfa and nfb; a step over zero
! samples needs none, so the first to measure is the first step over other
! audio (noise is enough), and zero samples warm nothing. Wisdom is saved only
! when the stream ends at halt or end of input (a killed process saves none),
! so keep -a across runs.

module streaming_jtty
  use, intrinsic :: iso_c_binding, only: c_int, c_int64_t, c_float, c_double,  &
       c_char
  use, intrinsic :: iso_fortran_env, only: int16, int64, real64, output_unit,  &
       error_unit
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  use jtty_receive_context, only: jtty_rx_create, jtty_rx_destroy,             &
       jtty_rx_begin, jtty_rx_process, jtty_rx_next_required_sample,           &
       jtty_rx_next_search_sample, jtty_rx_take_updates_with_gaps, jtty_rx_end
  use jtty_mdec, only: UPDATE_GROWING, UPDATE_COMPLETE, UPDATE_EXPIRED,        &
       UPDATE_RECEPTION_ENDED, MAX_DISPLAY_GAPS, jtty_release_fft_resources
  use jtty_filler, only: strip_filler_text
  use streaming_emit, only: streaming_emit_error, streaming_emit_json_escape
  use streaming_apply, only: params_block
  implicit none
  private

  public :: jtty_audio, jtty_end, jtty_range_key, jtty_emit_range_error,      &
       jtty_release

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
    call decode_available_(params)
  end subroutine jtty_audio

  subroutine jtty_end()
    if (.not. session_open_) return
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
  ! available, taking the updates after each step.
  subroutine decode_available_(params)
    type(params_block), intent(in) :: params
    integer(int64) :: required, available_end
    integer(c_int) :: processed
    integer :: first
    do
       available_end = pcm_base_ + pcm_count_
       if (jtty_rx_next_search_sample(handle_) + WINDOW .gt. available_end) return
       required = jtty_rx_next_required_sample(handle_)
       if (required .lt. pcm_base_)                                         &
            call fatal_('required receive audio is no longer retained')
       first = int(required - pcm_base_) + 1
       processed = jtty_rx_process(handle_, pcm_(first:pcm_count_),         &
            int(pcm_count_ - first + 1, c_int), required, available_end, 1,  &
            params%nfa, params%nfb, real(params%nfqso, c_float),             &
            real(params%ntol, c_float))
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
                write(error_unit, '(a,i0,a)') 'wsjt-codec --stream: the gap markers of message ', &
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
    write(error_unit, '(2a)') 'wsjt-codec --stream: ', message
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

end module streaming_jtty
