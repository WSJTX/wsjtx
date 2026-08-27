subroutine decode_msk144_core(audio_samples, params, data_dir, completion)
  use streaming_emit, only: streaming_emit_enabled,                        &
       streaming_emit_decode
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
  logical :: bshmsg = .false. ! enables shorthand messages  
  logical :: btrain = .false. ! turns on training in MSK144 mode
  real*8 :: pcoeffs(5) = (/ 0.0, 0.0, 0.0, 0.0, 0.0 /); ! phase equalization
  logical :: bswl = .false.
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
      if (streaming_emit_enabled()) then
         call emit_msk144_line(line)
      else
         write(*, 1001) line
      end if
      1001 format(a80)
      message_count = message_count + 1;
    end if
  end do

  call set_decode_completion(completion, 0, message_count, 0)

contains

  ! Parse the 80-char mskrtd line and emit it as NDJSON. mskrtd's format
  ! mirrors the FT8/FT4/Q65 emit shape:
  !   cols 1-6:   HHMMSS  (i6.6)
  !   cols 7-10:  snr     (i4)
  !   cols 11-15: dt      (f5.1)
  !   cols 16-20: freq    (i5)
  !   col   22:   '&'     (mode separator)
  !   cols 24-60: message (a37, trimmed)
  subroutine emit_msk144_line(line_in)
    character(len=*), intent(in) :: line_in
    integer :: nutc_l, snr_l, freq_l, ios_l
    real    :: dt_l
    character(len=37) :: msg_l
    read(line_in, '(i6,i4,f5.1,i5,1x,1x,1x,a37)',                          &
         iostat=ios_l) nutc_l, snr_l, dt_l, freq_l, msg_l
    if (ios_l /= 0) return    ! parse failure: silently drop
    call streaming_emit_decode("MSK144", nutc_l, snr_l, dt_l, freq_l,      &
         adjustl(msg_l))
  end subroutine emit_msk144_line

end subroutine decode_msk144_core

subroutine decode_msk144(audio_samples, params, data_dir)
  use streaming_emit, only: streaming_emit_enabled,                        &
       streaming_emit_decode_finished
  use decode_completion_module, only: decode_completion_result,           &
       write_decode_completion

  include 'jt9com.f90'

  integer*2 audio_samples(NMAX)
  type(params_block) :: params
  character(len = 500) :: data_dir
  type(decode_completion_result) :: completion

  call decode_msk144_core(audio_samples, params, data_dir, completion)
  if (streaming_emit_enabled()) then
    call streaming_emit_decode_finished(params%nutc)
  else if (.not. params%ndiskdat) then
    call write_decode_completion(completion)
    call flush(6)
  end if
end subroutine decode_msk144
