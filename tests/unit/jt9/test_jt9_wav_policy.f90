program test_jt9_wav_policy
  use jt9_wav_policy
  implicit none

  integer, parameter :: modes(*) = [4, 5, 8, 9, 65, 66, 144, 240, 241, 242]
  integer, parameter :: lengths(*) = [0, 100, 150000, 180000, 720000]
  integer, parameter :: final_stages(*) = [49, 50, 48, 49, 50]
  integer :: i, j, start, sensitivity, threaded, stage, expected_count, n
  logical :: multift8
  type(wav_read_plan) :: read_plan
  type(wav_pass_plan) :: plan

  call check_read(4, 60.d0, 11025, 573300, 166, 573300, 181)
  call check_read(4, 60.d0, 12000, 624000, 181, 624000, 181)
  call check_read(5, 7.5d0, 12000, 72576, 21, 72576, 181)
  call check_read(8, 15.d0, 12000, 180000, 52, 179712, 181)
  call check_read(8, 60.d0, 12000, 720000, 208, 179712, 181)
  call check_read(9, 60.d0, 12000, 720000, 209, 720000, 0)
  call check_read(65, 60.d0, 12000, 720000, 208, 720000, 181)
  call check_read(66, 30.d0, 12000, 360000, 104, 360000, 0)
  call check_read(66, 300.d0, 12000, 3600000, 1041, 3600000, 0)
  call check_read(144, 30.d0, 12000, 360000, 104, 360000, 181)
  do i = 240, 242
     call check_read(i, 1800.d0, 12000, 21600000, 6250, 21600000, 0)
     call check_read(i, 60.d0, 12000, 720000, 209, 720000, 0)
  end do
  call check_read(9, 0.864d0, 12000, 10368, 3, 10368, 0)
  call check_read(9, 0.2881d0, 12000, 3457, 2, 3457, 0)
  call check_read(65, 0.2881d0, 12000, 3457, 1, 3457, 181)
  call check_read(9, 0.00001d0, 12000, 0, 0, 0, 0)

  read_plan = plan_wav_read(8, 60.d0, 12000)
  call check(52 * WAV_BLOCK_SAMPLES <= read_plan%read_limit .and. &
       53 * WAV_BLOCK_SAMPLES > read_plan%read_limit, 'FT8 stops before the next block read')

  do threaded = 0, 1
     multift8 = threaded == 1
     do start = 0, 4
        do sensitivity = 1, 3
           do j = 1, size(lengths)
              n = lengths(j)
              plan = plan_wav_passes(8, multift8, start, sensitivity, 17, n)
              expected_count = 1
              if (.not. multift8 .or. start < 2) expected_count = 3
              call check(plan%count == expected_count, 'FT8 pass count')
              do i = 1, plan%count
                 if (.not. multift8) then
                    select case (i)
                    case (1); stage = 41
                    case (2); stage = 47
                    case (3); stage = 50
                    end select
                 else if (i < plan%count) then
                    stage = 41
                    if (i == 2) stage = 46
                 else
                    stage = final_stages(start + 1)
                 end if
                 call check(plan%passes(i)%stage == stage, 'FT8 pass stage and order')
                 call check(plan%passes(i)%prefix_copy .eqv. (.not. multift8 .or. i < plan%count), &
                      'FT8 sample source')
                 call check(plan%passes(i)%mtd .eqv. (multift8 .and. i == plan%count), 'FT8 pass multithreading')
                 if (plan%passes(i)%prefix_copy) then
                    call check(plan%passes(i)%sample_count == min(n, stage * 3456), 'prefix valid sample count')
                 else
                    call check(plan%passes(i)%sample_count == n, 'final MTD keeps the full buffer count')
                 end if
              end do
              call check(plan%low_threshold .eqv. (.not. multift8 .or. sensitivity /= 1), 'low-threshold policy')
              call check(plan%subtract_pass .eqv. (.not. multift8 .or. sensitivity == 3), 'subtraction policy')
           end do
        end do
     end do
  end do

  do i = 1, size(modes)
     if (modes(i) == 8) cycle
     do j = 1, size(lengths)
        plan = plan_wav_passes(modes(i), .true., 0, 1, 137, lengths(j))
        call check(plan%count == 1 .and. plan%passes(1)%stage == 137, 'other modes retain the read stage')
        call check(.not. plan%passes(1)%prefix_copy .and. .not. plan%passes(1)%mtd, &
             'other modes use one full-buffer pass')
        n = lengths(j)
        if (modes(i) == 5) n = min(n, 72576)
        call check(plan%passes(1)%sample_count == n, 'normal pass sample count')
        call check(plan%low_threshold .and. plan%subtract_pass, 'non-FT8 sensitivity is unchanged')
     end do
  end do

contains

  subroutine check_read(mode, period, sample_rate, samples, blocks, limit, half_symbols)
    integer, intent(in) :: mode, sample_rate, samples, blocks, limit, half_symbols
    real(8), intent(in) :: period
    type(wav_read_plan) :: actual

    actual = plan_wav_read(mode, period, sample_rate)
    call check(actual%sample_count == samples, 'read sample extent')
    call check(actual%block_count == blocks, 'read block rounding')
    call check(actual%read_limit == limit, 'pre-read sample limit')
    call check(actual%half_symbol_limit == half_symbols, 'post-read half-symbol limit')
  end subroutine check_read

  subroutine check(condition, message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message

    if (.not. condition) then
       write(*, '(A)') message
       error stop 1
    end if
  end subroutine check

end program test_jt9_wav_policy
