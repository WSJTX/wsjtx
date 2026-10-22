! SPDX-License-Identifier: GPL-3.0-or-later
! jt9's worker on one FT8 period from a file, for settings jt9's command line
! cannot give (SuperFox; the station flags its WAV mode leaves unset): the
! stream's starting params with a configure applied as the stream applies it,
! decoded at the file's final pass through multimode_decoder with the stream
! off, which prints the lines the worker prints.
!
! usage: ft8_worker_model <12 kHz WAV> <configure JSON> <final half-symbol> <share dir>
program ft8_worker_model
  use FFTW3
  use prog_args, only: share_dir
  use jt9_params_init, only: init_default_params, init_streaming_extra_fields, cli_args_t
  use streaming_control, only: parse_control_frame, configure_fields, control_type_error, &
       CTRL_CONFIGURE
  use streaming_apply, only: apply_configure_fields
  include 'jt9com.f90'
  type(dec_data), allocatable :: shared
  type(cli_args_t) :: args
  type(configure_fields) :: cfg
  type(control_type_error) :: terr
  character(len=4096) :: wav, json, text
  integer :: action, mode, unit, bytes, samples, iret, nzhsym
  real(8) :: trperiod
  common/patience/npatience,nthreads
  data npatience/1/,nthreads/1/

  if (command_argument_count() .ne. 4) stop 2
  call get_command_argument(1, wav)
  call get_command_argument(2, json)
  call get_command_argument(3, text)
  read(text, *) nzhsym
  call get_command_argument(4, share_dir)
  iret = fftwf_init_threads()
  call fftwf_plan_with_nthreads(1)
  allocate(shared)
  shared%id2 = 0
  ! wsjt-codec's start without options (lib/jt9.f90).
  mode = 8
  trperiod = 15.d0
  args = cli_args_t(ntol=20, mycall='K1ABC', hiscall='W9XYZ', hisgrid='EN37')
  call init_default_params(shared%params, mode, trperiod, args)
  call init_streaming_extra_fields(shared%params, args)
  call parse_control_frame(trim(json), action, cfg, terr)
  if (action .ne. CTRL_CONFIGURE .or. terr%present) stop 3
  call apply_configure_fields(cfg, mode, trperiod, shared%params, 200, 4000)
  ! What lib/streaming_io.f90 sets for a file's final FT8 pass.
  shared%params%emedelay = 0.0
  shared%params%ndiskdat = .true.
  shared%params%newdat = .true.
  shared%params%nagain = .false.
  shared%params%ntr = int(trperiod)
  shared%params%nzhsym = nzhsym
  shared%params%kin = 64800
  open(newunit=unit, file=trim(wav), access='stream', form='unformatted', status='old', &
       action='read', iostat=iret)
  if (iret .ne. 0) stop 4
  inquire(unit=unit, size=bytes)
  samples = min((bytes - 44) / 2, 15 * 12000)
  ! The multithreaded decoder's final reads to its stop, a file's standard final the period.
  if (shared%params%lmultift8) samples = min(samples, nzhsym * 3456)
  read(unit, pos=45) shared%id2(1:samples)
  close(unit)
  call multimode_decoder(shared%ss, shared%id2, shared%params, 12000)
end program ft8_worker_model
