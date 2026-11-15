! SPDX-License-Identifier: GPL-3.0-or-later
! The GUI's Fox path, for test_streaming_side: jt9 in WAV mode run as
! `jt9 -8 -X 6 -c CALL -G GRID [-M -N 2 -D 3] file...` (lib/jt9.f90), but in
! one process, as the GUI's decoder process decodes period after period, and
! with houndcallers.txt deleted between two periods where an argument says
! "reset", as the GUI's FoxReset does (widgets/mainwindow.cpp), so that the
! decoder starts a new caller list. Its settings come from jt9's command-line
! values and its passes from jt9's own plan (lib/jt9_wav_policy.f90), not
! from the codec's configure. After each pass it prints "pass|HHMMSS|N",
! then each line of houndcallers.txt as "hound|<line>".
!
! usage: fox_gui_model <directory> <share dir> <CALL> <GRID> <0|1 multithreaded>
!                      <WAV@HHMMSS | reset>...
program fox_gui_model
  use FFTW3
  use, intrinsic :: iso_c_binding, only: c_int, c_int64_t, c_short
  use prog_args, only: temp_dir, data_dir, share_dir
  use jt9_params_init, only: cli_args_t
  use jt9_wav_request, only: build_wav_request
  use jt9_wav_policy, only: WAV_BLOCK_SAMPLES, wav_read_plan, wav_decode_pass, wav_pass_plan, &
       plan_wav_read, plan_wav_passes
  use decoder_engine, only: attempt_request, host_decode_context, engine_host_decode_request
  use decode_completion_module, only: decode_completion_result
  implicit none
  integer, parameter :: MODE = 8, NFSAMPLE = 12000
  real(8), parameter :: TRPERIOD = 15.d0
  type(cli_args_t) :: args
  type(wav_read_plan) :: read_plan
  type(wav_pass_plan) :: pass_plan
  character(len=4096) :: directory, spec, text
  character(len=12) :: mycall
  character(len=6) :: mygrid
  character(len=80) :: line
  integer(c_short), allocatable :: wav_samples(:), id2a(:)
  integer :: unit, bytes, available, iret, i, at, nutc, ios, ipass, nhsym, valid, iblk, k, k0
  integer :: files, attempt
  logical :: multift8

  if (command_argument_count() .lt. 6) stop 2
  call get_command_argument(1, directory)
  call get_command_argument(2, share_dir)
  call get_command_argument(3, text)
  mycall = text
  call get_command_argument(4, text)
  mygrid = text
  call get_command_argument(5, text)
  multift8 = trim(text) .eq. '1'
  temp_dir = directory
  data_dir = directory
  iret = fftwf_init_threads()
  call fftwf_plan_with_nthreads(1)
  ! jt9's values for -8 -X 6 -c CALL -G GRID, and -M -N 2 -D 3: its
  ! defaults (lib/jt9.f90) and cli_tolerance's ntol of 20 in FT8.
  args = cli_args_t(flow=200, fsplit=2700, fhigh=4000, nrxfreq=1500, ndepth=1, ntol=20, nQSOProg=0, &
       nexp_decode=6, ncycles=3, nft8rxfsens=3, nmt=merge(2, 0, multift8), nsubmode=0,             &
       mycall=mycall, hiscall=' ', mygrid=mygrid, hisgrid=' ', multift8=multift8,                 &
       hidedupes=.false., lwidedxcsearch=.true., have_ntol=.false.)
  allocate(wav_samples(15 * NFSAMPLE), id2a(180000))
  files = 0
  do i = 6, command_argument_count()
     call get_command_argument(i, spec)
     if (trim(spec) .eq. 'reset') then
        open(newunit=unit, file=trim(temp_dir)//'/houndcallers.txt', status='old', iostat=iret)
        if (iret .eq. 0) close(unit, status='delete')
        cycle
     end if
     at = index(spec, '@', back=.true.)
     if (at .lt. 2) stop 4
     read(spec(at+1:), *) nutc
     open(newunit=unit, file=spec(:at-1), access='stream', form='unformatted', status='old', &
          action='read', iostat=iret)
     if (iret .ne. 0) stop 5
     inquire(unit=unit, size=bytes)
     available = min((bytes - 44) / 2, size(wav_samples))
     wav_samples = 0
     read(unit, pos=45) wav_samples(1:available)
     close(unit)
     ! jt9's read of the file, block by block, to its read limit.
     read_plan = plan_wav_read(MODE, TRPERIOD, NFSAMPLE)
     valid = 0
     nhsym = 0
     do iblk = 1, read_plan%block_count
        k0 = (iblk - 1) * WAV_BLOCK_SAMPLES
        k = min(iblk * WAV_BLOCK_SAMPLES, read_plan%sample_count)
        if (k .gt. read_plan%read_limit) exit
        if (available - k0 .le. 0) exit
        valid = k0 + min(k - k0, available - k0)
        nhsym = (k - 2048) / WAV_BLOCK_SAMPLES
        if (read_plan%half_symbol_limit .gt. 0 .and. nhsym .ge. read_plan%half_symbol_limit) exit
        if (available - k0 .lt. k - k0) exit
     end do
     wav_samples(valid+1:) = 0
     files = files + 1
     attempt = 0
     pass_plan = plan_wav_passes(MODE, multift8, 3, 3, nhsym, valid)
     args%lft8lowth = pass_plan%low_threshold
     args%lft8subpass = pass_plan%subtract_pass
     do ipass = 1, pass_plan%count
        if (pass_plan%passes(ipass)%prefix_copy) then
           k = pass_plan%passes(ipass)%stage * WAV_BLOCK_SAMPLES
           id2a(:k) = wav_samples(:k)
           id2a(k+1:) = 0
           call run_pass(id2a, pass_plan%passes(ipass))
        else
           call run_pass(wav_samples, pass_plan%passes(ipass))
        end if
        write(*, '(a,i6.6,a,i0)') 'pass|', nutc, '|', pass_plan%passes(ipass)%stage
        open(newunit=unit, file=trim(temp_dir)//'/houndcallers.txt', status='old', action='read', &
             iostat=iret)
        if (iret .ne. 0) cycle
        do
           read(unit, '(a)', iostat=ios) line
           if (ios .ne. 0) exit
           write(*, '(2a)') 'hound|', trim(line)
        end do
        close(unit)
     end do
  end do

contains

  ! One pass as jt9's run_engine_wav runs it.
  subroutine run_pass(samples, pass)
    integer(c_short), intent(inout) :: samples(*)
    type(wav_decode_pass), intent(in) :: pass
    type(attempt_request) :: request
    type(host_decode_context) :: context
    type(decode_completion_result) :: completion
    attempt = attempt + 1
    call build_wav_request(args, MODE, TRPERIOD, nutc, pass%stage, pass%mtd, int(files, c_int64_t), &
         attempt, 0, request)
    context = host_decode_context(legacy_mode=MODE, utc=nutc, half_symbol_stage=pass%stage, &
         print_hash22=.false.)
    call engine_host_decode_request(samples, request, context, NFSAMPLE, completion, 0, pass%sample_count)
  end subroutine run_pass
end program fox_gui_model
