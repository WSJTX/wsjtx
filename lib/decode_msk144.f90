! The stream's MSK144, decoded as the GUI's hspec decodes it (lib/hspec.f90):
! at each 3584-sample block end k of a period, from 7168 to 30 s, mskrtd on
! the period's samples [k-7168, k), unless either half is all zeros. Its Fast
! Graph's columns, and fast JT9's, are hspec's, as the GUI draws them
! (lib/spectrum_rows.f90).
module msk144_stream
  use streaming_emit, only: streaming_emit_decode, streaming_emit_msk144_trained, &
       time_form, streaming_emit_fast_spectrum
  use msk144_training, only: trainings_done, trained_file, trained_call, trained_terms
  use four2a_patience, only: patience_override
  use streaming_receive, only: receive
  use spectrum_rows, only: fast_rows, fast_begin, fast_block, FAST_COLUMNS
  implicit none
  private
  public :: msk144_settings, msk144_window, msk144_line_fields, msk144_fast_spectrum, &
       msk144_fast_reset

  integer, parameter, public :: MSK144_BLOCK = 3584
  integer, parameter :: WINDOW = 2 * MSK144_BLOCK
  integer, parameter :: LAST_K = 30 * 12000
  ! hspec's FFT length, below which it draws nothing.
  integer, parameter :: HSPEC_NFFT = 512

  ! The Fast Graph's state; the k it last drew at; and the block end of the
  ! period it was last given.
  type(fast_rows), save :: fast_
  integer, save :: drawn_k_ = 99999999
  integer, save :: given_k_ = 0

  ! mskrtd's receive options, and the calls accepted configures set (blank
  ! until one does). train is cleared by the decoder when training succeeds.
  type :: msk144_settings
     logical           :: short_messages = .false.
     logical           :: swl            = .false.
     logical           :: train          = .false.
     real(8)           :: phase_eq(5)    = 0.d0
     character(len=12) :: mycall         = ' '
     character(len=12) :: hiscall        = ' '
  end type msk144_settings

contains

  ! The window ending at block end k of id2, the period's samples; its decode
  ! event, labelled in form when given, then msk144_trained if its call
  ! completed a training.
  subroutine msk144_window(id2, k, nutc, ntol, nfqso, ndepth, settings, data_dir, form)
    integer*2,             intent(in)    :: id2(*)
    integer,               intent(in)    :: k, nutc, ntol, nfqso, ndepth
    type(msk144_settings), intent(inout) :: settings
    character(len=*),      intent(in)    :: data_dir
    type(time_form), optional, intent(in) :: form
    logical*1 :: bshmsg, bswl, btrain
    real :: tsec, tt1, tt2, dt
    integer :: done, utc, snr, freq, patience
    logical :: training, ok
    character(len=80) :: line
    character(len=37) :: message

    if (k .lt. WINDOW .or. k .gt. LAST_K .or. mod(k, MSK144_BLOCK) .ne. 0) return
    tt1 = sum(float(abs(id2(k-WINDOW+1:k-MSK144_BLOCK))))
    tt2 = sum(float(abs(id2(k-MSK144_BLOCK+1:k))))
    if (tt1 .eq. 0.0 .or. tt2 .eq. 0.0) return
    tsec = (k - WINDOW) / 12000.0
    bshmsg = settings%short_messages
    bswl = settings%swl
    btrain = settings%train
    done = trainings_done
    ! The GUI decodes MSK144 in its own process, which plans FFTs FFTW_ESTIMATE;
    ! a more patient plan the session or -a's wisdom already holds still wins.
    patience = patience_override
    patience_override = 0
    call mskrtd(id2(k-WINDOW+1:k), nutc, tsec, ntol, nfqso, iand(ndepth, 3),  &
         settings%mycall, settings%hiscall, bshmsg, btrain, settings%phase_eq, &
         bswl, trim(data_dir), line)
    patience_override = patience
    settings%train = btrain
    if (line(1:1) .ne. char(0)) then
       call msk144_line_fields(line, utc, snr, dt, freq, message, training, ok)
       ! Its period, trperiod's whole seconds, as the GUI passes it to hspec.
       if (ok .and. present(form)) then
          call streaming_emit_decode('MSK144', utc, snr, dt, freq, message,   &
               training=training, form=form, period=int(form%trperiod))
       else if (ok) then
          call streaming_emit_decode('MSK144', utc, snr, dt, freq, message,   &
               training=training)
       end if
    end if
    if (trainings_done .ne. done) call streaming_emit_msk144_trained(        &
         trained_call, trained_file, trained_terms)
  end subroutine msk144_window

  ! A new period: the next columns start at 0 whatever their k, where the
  ! GUI's hspec starts them when k falls.
  subroutine msk144_fast_reset()
    call fast_begin(fast_)
    drawn_k_ = 99999999
    given_k_ = 0
  end subroutine msk144_fast_reset

  ! The Fast Graph at block end k of id2, the period's samples, while
  ! configure's spectrum is on: hspec's columns as the GUI's fastSink has hspec
  ! write them at each block end, MSK144's blocks of 3584 samples or fast
  ! JT9's of 3456, and a fast_spectrum row of the columns written. The
  ! period's blocks it has not been given, while spectrum was off, go through
  ! it first without rows, so that its columns stay the GUI's.
  subroutine msk144_fast_spectrum(id2, k, trperiod, block)
    integer*2, intent(in) :: id2(*)
    integer,   intent(in) :: k, trperiod, block
    integer :: kk
    if (.not. receive%spectrum) return
    do kk = given_k_ + block, k - block, block
       call fast_draw_(id2, kk, trperiod, .false.)
    end do
    call fast_draw_(id2, k, trperiod, .true.)
  end subroutine msk144_fast_spectrum

  ! The columns through block end k; with row, those written.
  subroutine fast_draw_(id2, k, trperiod, row)
    integer*2, intent(in) :: id2(*)
    integer,   intent(in) :: k, trperiod
    logical,   intent(in) :: row
    real      :: peak, power
    integer   :: before, first
    logical   :: fresh
    before = fast_%jh
    fresh = k .lt. drawn_k_
    call fast_block(fast_, id2, k, trperiod, power, peak)
    given_k_ = k
    if (k .lt. HSPEC_NFFT .or. k .gt. LAST_K) return
    drawn_k_ = k
    first = 0
    if (.not. fresh) first = min(before + 1, FAST_COLUMNS - 1)
    if (row) call streaming_emit_fast_spectrum(k, first, power, peak,            &
         fast_%green(first:fast_%jh), fast_%bins(:, first:fast_%jh))
  end subroutine fast_draw_

  ! mskrtd's line, (i6.6,i4,f5.1,i5,a4,a37): utc, snr, dt, freq, the decode
  ! symbol, whose '^' marks a decode made while training, and the message.
  subroutine msk144_line_fields(line, utc, snr, dt, freq, message, training, ok)
    character(len=*),  intent(in)  :: line
    integer,           intent(out) :: utc, snr, freq
    real,              intent(out) :: dt
    character(len=37), intent(out) :: message
    logical,           intent(out) :: training, ok
    character(len=4) :: symbol
    integer :: ios
    read(line, '(i6,i4,f5.1,i5,a4,a37)', iostat=ios) utc, snr, dt, freq,     &
         symbol, message
    ok = ios .eq. 0
    training = .false.
    if (ok) training = symbol(2:2) .eq. '^'
    message = adjustl(message)
  end subroutine msk144_line_fields

end module msk144_stream

module msk144_file
  use decode_completion_module, only: decode_completion_result, &
       reset_decode_completion, set_decode_completion
  implicit none
  private
  public :: msk144_file_options, decode_msk144_file

  type :: msk144_file_options
     integer :: period_seconds, utc, tolerance_hz, receive_frequency_hz, depth
     character(len=12) :: mycall, hiscall
  end type msk144_file_options

contains

subroutine decode_msk144_file(audio_samples, options, data_dir, completion)

  ! constants
  integer, parameter :: SAMPLING_RATE = 12000
  integer, parameter :: BLOCK_SIZE = 7168
  integer, parameter :: STEP_SIZE = BLOCK_SIZE / 2
  integer, parameter :: CALL_LENGTH = 12
  
  ! aguments
  integer*2, intent(in) :: audio_samples(*)
  type(msk144_file_options), intent(in) :: options
  character(len=*), intent(in) :: data_dir
  type(decode_completion_result), intent(out) :: completion

  ! parameters of mskrtd
  integer*2 :: buffer(BLOCK_SIZE)
  real :: tsec   
  logical*1 :: bshmsg = .false. ! enables shorthand messages
  logical*1 :: btrain = .false. ! turns on training in MSK144 mode
  real*8 :: pcoeffs(5) = (/ 0.0, 0.0, 0.0, 0.0, 0.0 /); ! phase equalization
  logical*1 :: bswl = .false.
  character(len = 80) :: line
  character(len = CALL_LENGTH) :: mycall 
  character(len = CALL_LENGTH) :: hiscall

  ! local variables
  integer :: sample_count
  integer :: position
  integer :: message_count

  call reset_decode_completion(completion)
  message_count = 0


  ! decode in 0.3s blocks
  sample_count = options%period_seconds * SAMPLING_RATE
  mycall = options%mycall
  hiscall = options%hiscall

  do position = 1, sample_count - BLOCK_SIZE + 1, STEP_SIZE
    buffer =  audio_samples(position : position + BLOCK_SIZE - 1)
    tsec = position / REAL(SAMPLING_RATE)

    call mskrtd(buffer, options%utc, tsec, options%tolerance_hz, options%receive_frequency_hz, options%depth, &
      mycall, hiscall, bshmsg, btrain, pcoeffs, bswl, data_dir, line)

    if (line(1:1) .ne. char(0)) then
      line = line(1:index(line, char(0))-1)
      write(*, 1001) line
      1001 format(a80)
      message_count = message_count + 1;
    end if
  end do

  call set_decode_completion(completion, 0, message_count, 0)

end subroutine decode_msk144_file

end module msk144_file
