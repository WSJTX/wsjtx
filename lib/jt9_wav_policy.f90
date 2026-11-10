module jt9_wav_policy
  implicit none
  private
  public :: WAV_BLOCK_SAMPLES, wav_read_plan, wav_decode_pass, wav_pass_plan
  public :: plan_wav_read, plan_wav_passes

  integer, parameter :: WAV_BLOCK_SAMPLES = 3456

  type :: wav_read_plan
     integer :: sample_count = 0
     integer :: block_count = 0
     integer :: read_limit = 0
     integer :: half_symbol_limit = 181
  end type wav_read_plan

  type :: wav_decode_pass
     integer :: stage = 0
     integer :: sample_count = 0
     logical :: prefix_copy = .false.
     logical :: mtd = .false.
  end type wav_decode_pass

  type :: wav_pass_plan
     type(wav_decode_pass) :: passes(3)
     integer :: count = 0
     logical :: low_threshold = .true.
     logical :: subtract_pass = .true.
  end type wav_pass_plan

contains

  pure function plan_wav_read(mode, period, sample_rate) result(plan)
    integer, intent(in) :: mode, sample_rate
    real(8), intent(in) :: period
    type(wav_read_plan) :: plan

    plan%sample_count = int(period * 12000.d0)
    if (mode == 4) plan%sample_count = 52 * sample_rate
    if (mode == 5) plan%sample_count = 21 * WAV_BLOCK_SAMPLES
    plan%block_count = plan%sample_count / WAV_BLOCK_SAMPLES
    select case (mode)
    case (4, 9, 240, 241, 242)
       plan%block_count = (plan%sample_count + WAV_BLOCK_SAMPLES - 1) / WAV_BLOCK_SAMPLES
    end select
    plan%read_limit = plan%sample_count
    if (mode == 8) plan%read_limit = 179712
    select case (mode)
    case (9, 66, 240, 241, 242)
       plan%half_symbol_limit = 0
    end select
  end function plan_wav_read

  ! Multithreaded FT8 expects start 0-4 and sensitivity 1-3.
  pure function plan_wav_passes(mode, multift8, start, sensitivity, nhsym, valid_samples) result(plan)
    integer, intent(in) :: mode, start, sensitivity, nhsym, valid_samples
    logical, intent(in) :: multift8
    type(wav_pass_plan) :: plan
    integer :: final_stage

    plan%count = 1
    plan%passes(1) = wav_decode_pass(nhsym, valid_samples, .false., .false.)
    if (mode == 5) plan%passes(1)%sample_count = min(valid_samples, 72576)
    if (mode /= 8) return

    if (.not. multift8) then
       plan%count = 3
       plan%passes(1) = prefix_pass(41, valid_samples)
       plan%passes(2) = prefix_pass(47, valid_samples)
       plan%passes(3) = prefix_pass(50, valid_samples)
       return
    end if

    select case (sensitivity)
    case (1)
       plan%low_threshold = .false.
       plan%subtract_pass = .false.
    case (2)
       plan%low_threshold = .true.
       plan%subtract_pass = .false.
    end select

    final_stage = 50
    if (start < 2) then
       plan%count = 3
       plan%passes(1) = prefix_pass(41, valid_samples)
       plan%passes(2) = prefix_pass(46, valid_samples)
       final_stage = 46
       if (start == 0) final_stage = 49
       if (start == 1) final_stage = 50
    else
       if (start == 2) final_stage = 48
       if (start == 3) final_stage = 49
    end if
    plan%passes(plan%count) = wav_decode_pass(final_stage, valid_samples, .false., .true.)
  end function plan_wav_passes

  pure function prefix_pass(stage, valid_samples) result(pass)
    integer, intent(in) :: stage, valid_samples
    type(wav_decode_pass) :: pass

    pass = wav_decode_pass(stage, min(valid_samples, stage * WAV_BLOCK_SAMPLES), .true., .false.)
  end function prefix_pass

end module jt9_wav_policy
