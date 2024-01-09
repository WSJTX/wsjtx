subroutine decode_msk144(audio_samples, params, data_dir)
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
  integer :: message_count = 0


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

  if (.not. params%ndiskdat) then
    write(*, 1002) 0, message_count, 0
    1002 format('<DecodeFinished>', 2i4, i9)
  end if

end subroutine decode_msk144