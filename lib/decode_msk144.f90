! The stream's MSK144, decoded as the GUI's hspec decodes it (lib/hspec.f90):
! at each 3584-sample block end k of a period, from 7168 to 30 s, mskrtd on
! the period's samples [k-7168, k), unless either half is all zeros.
module msk144_stream
  use streaming_emit, only: streaming_emit_decode, streaming_emit_msk144_trained
  use msk144_training, only: trainings_done, trained_file, trained_call, trained_terms
  implicit none
  private
  public :: msk144_settings, msk144_window, msk144_line_fields

  integer, parameter, public :: MSK144_BLOCK = 3584
  integer, parameter :: WINDOW = 2 * MSK144_BLOCK
  integer, parameter :: LAST_K = 30 * 12000

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
  ! event, then msk144_trained if its call completed a training.
  subroutine msk144_window(id2, k, nutc, ntol, nfqso, ndepth, settings, data_dir)
    integer*2,             intent(in)    :: id2(*)
    integer,               intent(in)    :: k, nutc, ntol, nfqso, ndepth
    type(msk144_settings), intent(inout) :: settings
    character(len=*),      intent(in)    :: data_dir
    logical*1 :: bshmsg, bswl, btrain
    real :: tsec, tt1, tt2, dt
    integer :: done, utc, snr, freq, patience
    logical :: training, ok
    character(len=80) :: line
    character(len=37) :: message
    integer :: npatience, nthreads
    common/patience/npatience,nthreads

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
    patience = npatience
    npatience = 0
    call mskrtd(id2(k-WINDOW+1:k), nutc, tsec, ntol, nfqso, iand(ndepth, 3),  &
         settings%mycall, settings%hiscall, bshmsg, btrain, settings%phase_eq, &
         bswl, trim(data_dir), line)
    npatience = patience
    settings%train = btrain
    if (line(1:1) .ne. char(0)) then
       call msk144_line_fields(line, utc, snr, dt, freq, message, training, ok)
       if (ok) call streaming_emit_decode('MSK144', utc, snr, dt, freq,       &
            message, training=training)
    end if
    if (trainings_done .ne. done) call streaming_emit_msk144_trained(        &
         trained_call, trained_file, trained_terms)
  end subroutine msk144_window

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

subroutine decode_msk144_core(audio_samples, params, data_dir, completion)
  use decode_completion_module, only: decode_completion_result,           &
       reset_decode_completion, set_decode_completion
  include 'jt9com.f90'

  ! constants
  integer, parameter :: SAMPLING_RATE = 12000
  integer, parameter :: BLOCK_SIZE = 7168
  integer, parameter :: STEP_SIZE = BLOCK_SIZE / 2
  integer, parameter :: CALL_LENGTH = 12
  
  ! aguments
  integer*2 audio_samples(NMAX)
  type(params_block) :: params
  character(len = 500) :: data_dir
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
  sample_count = params%ntr * SAMPLING_RATE
  mycall = transfer(params%mycall, mycall)    ! string to char[]
  hiscall = transfer(params%hiscall, hiscall)

  do position = 1, sample_count - BLOCK_SIZE + 1, STEP_SIZE
    buffer =  audio_samples(position : position + BLOCK_SIZE - 1)
    tsec = position / REAL(SAMPLING_RATE)

    call mskrtd(buffer, params%nutc, tsec, params%ntol, params%nfqso, params%ndepth, &
      mycall, hiscall, bshmsg, btrain, pcoeffs, bswl, data_dir, line)

    if (line(1:1) .ne. char(0)) then
      line = line(1:index(line, char(0))-1)
      write(*, 1001) line
      1001 format(a80)
      message_count = message_count + 1;
    end if
  end do

  call set_decode_completion(completion, 0, message_count, 0)

end subroutine decode_msk144_core

subroutine decode_msk144(audio_samples, params, data_dir)
  use decode_completion_module, only: decode_completion_result,           &
       write_decode_completion

  include 'jt9com.f90'

  integer*2 audio_samples(NMAX)
  type(params_block) :: params
  character(len = 500) :: data_dir
  type(decode_completion_result) :: completion

  call decode_msk144_core(audio_samples, params, data_dir, completion)
  if (.not. params%ndiskdat) then
    call write_decode_completion(completion)
    call flush(6)
  end if
end subroutine decode_msk144
