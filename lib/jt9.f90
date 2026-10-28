program jt9

! Decoder for JT9.  Can run stand-alone, reading data from *.wav files;
! or as the back end of wsjt-x, with data placed in a shared memory region.

  use options
  use prog_args
  use, intrinsic :: iso_c_binding
  use FFTW3
  use timer_module, only: timer
  use timer_impl, only: init_timer, fini_timer
  use readwav
  use jt9_input_validation, only: parse_integer, parse_real, parse_fixed_text, parse_wav_filename_nutc, &
       adjust_wav_nutc, submode_limit
  use, intrinsic :: iso_fortran_env, only: error_unit
  use streaming_emit, only: streaming_emit_set_enabled, streaming_emit_error
  use jt9_params_init, only: init_default_params, init_streaming_extra_fields, &
       cli_args_t, cli_tolerance, cli_exp_decode, normalize_station_options
  use decode_completion_module, only: decode_completion_result, write_decode_completion
  use decoder_engine, only: attempt_request, host_decode_context, engine_host_decode_request
  use jt9_wav_request, only: build_wav_request
  use jt9_wav_policy, only: WAV_BLOCK_SAMPLES, wav_read_plan, wav_decode_pass, wav_pass_plan, &
       plan_wav_read, plan_wav_passes
  use msk144_file, only: msk144_file_options, decode_msk144_file

  include 'jt9com.f90'

  interface
     subroutine jt9_print_version() bind(C, name='jt9_print_version')
     end subroutine jt9_print_version
     ! True in wsjt-codec (lib/streaming_io.f90), false in jt9 (lib/jt9_stream_stub.f90).
     logical function jt9_stream_available()
     end function jt9_stream_available
     ! wsjt-codec's is in lib/stream_setmode.c; jt9's stub is never asked.
     function stream_stdin_is_terminal() bind(C, name='stream_stdin_is_terminal')
       import :: c_int
       integer(c_int) :: stream_stdin_is_terminal
     end function stream_stdin_is_terminal
  end interface

  type(cli_args_t) :: args
  type(decode_completion_result) :: msk_completion
  type(msk144_file_options) :: msk_options
  type(wav_read_plan) :: read_plan
  type(wav_pass_plan) :: pass_plan
  integer :: ipass, prefix_samples
  integer*2 id2a(180000)
  integer(c_short), allocatable :: wav_samples(:)
  integer(C_INT) iret
  type(wav_header) wav
  real*8 TRperiod
  integer :: validation_mode, wav_mode=0
  integer(c_int) :: engine_attempt_no, engine_valid_samples
  integer(c_int64_t) :: engine_input_id

  character c
  character(len=500) optarg, infile
  character wisfile*256
  character(len=10) :: program_name

  integer :: arglen,stat,offset,remain,mode=0,flow=200,fsplit=2700,          &
       fhigh=4000,nrxfreq=1500,ndepth=1,nexp_decode=0,nQSOProg=0,ncycles=3,  &
       nft8rxfsens=3,nmt=0,nmtft8decsens=3,ndecoderstart=3, samples_read, wav_status
  logical :: read_files = .true., tx9 = .false., display_help = .false.,     &
       display_version = .false.,                                            &
       nexp_decode_set = .false.,                                             &
       have_ntol = .false.,multift8 = .false.,hidedupes = .false.,           &
       lwidedxcsearch = .true.,                                           &
       stream_mode = .false., use_mtd_fft_plans = .false., wav_utc_has_seconds
  type (option) :: long_options(45) = [                                      &
    option ('help', .false., 'h', 'Display this help message', ''),          &
    option ('version', .false., 'v', 'Display version and build revision', ''),&
    option ('shmem',.true.,'s','Use native shared memory for sample data','NAME'), &
    option ('ipc-lock', .true., '1', 'Shared-memory worker lifetime lock', 'PATH'), &
    option ('stream', .false., '0',                                          &
        'Read framed PCM and control frames from stdin',                     &
        ''),                                                                 &
    option ('tr-period', .true., 'p', 'Tx/Rx period, default SECONDS=60',    &
        'SECONDS'),                                                          &
    option ('executable-path', .true., 'e',                                  &
        'Location of subordinate executables, default PATH="."',            &
        'PATH'),                                                             &
    option ('data-path', .true., 'a',                                        &
        'Location of writeable data files, default PATH="."', 'PATH'),       &
    option ('temp-path', .true., 't',                                        &
        'Temporary files path, default PATH="."', 'PATH'),                   &
    option ('share-path', .true., 'r',                                       &
        'Location of read-only data files (ALLCALL7.TXT), default PATH="."', &
        'PATH'),                                                             &
    option ('lowest', .true., 'L',                                           &
        'Lowest frequency decoded (JT65), default HERTZ=200', 'HERTZ'),      &
    option ('highest', .true., 'H',                                          &
        'Highest frequency decoded, default HERTZ=4000', 'HERTZ'),           &
    option ('split', .true., 'S',                                            &
        'Lowest JT9 frequency decoded, default HERTZ=2700', 'HERTZ'),        &
    option ('rx-frequency', .true., 'f',                                     &
        'Receive frequency offset, default HERTZ=1500', 'HERTZ'),            &
    option ('freq-tolerance', .true., 'F',                                   &
        'Receive frequency tolerance, default HERTZ=20', 'HERTZ'),           &
    option ('patience', .true., 'w',                                         &
        'FFTW3 planing patience (0-4), default PATIENCE=1', 'PATIENCE'),     &
    option ('fft-threads', .true., 'm',                                      &
        'Number of threads to process large FFTs, default THREADS=1',        &
        'THREADS'),                                                          &
    option ('multithreadft8', .false., 'M', 'Use Multithread FT8 Decoder',   &
        ''),                                                                 &
    option ('MTft8-cycles', .true., 'C',                                     &
        'Number of cycles for MTft8 decoder (1-3), default CYCLES=3',        &
        'CYCLES'),                                                           &
    option ('MTft8-hidedupes', .false., 'U', 'Hide MTft8 Dupes',             &
        ''),                                                                 &
    option ('MTft8-RxFreqSens', .true., 'R',                                 &
        'RxFreqSensitivity for MTft8 decoder (1-3), default RXFREQSENS=3',   &
        'RXFREQSENS'),                                                       &
    option ('MTft8-NumThreads', .true., 'N',                                 &
        'Number of threads for MTft8 decoder (0-12), default NUMTHREADS=0',  &
        'NUMTHREADS'),                                                       &
    option ('MTft8-DecSens', .true., 'E',                                    &
        'MTft8 decoder sensitivity (1-3), default SENSITIVITY=3',            &
        'SENSITIVITY'),                                                      &
    option ('MTft8-StartTime', .true., 'D',                                  &
        'MTft8 decoder start (0-4), default START_TIME=3 (Normal)',          &
        'START_TIME'),                                                       &
    option ('Skip-MTft8-WideDxCallSearch', .false., 'Z',                     & 
        'SKIP MTft8 Wideband DX Call Search', ''),                           &        
    option ('q65', .false., '3', 'Q65 mode', ''),                            &
    option ('jt4', .false., '4', 'JT4 mode', ''),                            &
    option ('ft4', .false., '5', 'FT4 mode', ''),                            &
    option ('jt65', .false.,'6', 'JT65 mode', ''),                           &
    option ('fst4', .false., '7', 'FST4 mode', ''),                          &
    option ('fst4w', .false., 'W', 'FST4W mode', ''),                        &
    option ('fst4w-hash', .false., 'Y', 'FST4W mode, print hash22 values', ''), &
    option ('ft8', .false., '8', 'FT8 mode', ''),                            &
    option ('jt9', .false., '9', 'JT9 mode', ''),                            &
    option ('quiet', .false., 'q', 'Quiet mode, no <DecodeFinished>', ''),   &
    option ('msk144', .false., 'k', 'MSK144 mode', ''),                      &
    option ('QSOprog', .true., 'Q', 'QSO progress (0-5), default PROGRESS=1',&
        'QSOprogress'),                                                      &
    option ('sub-mode', .true., 'b',                                        &
        'Sub mode (mode-dependent A-H), default SUBMODE=A', 'SUBMODE'),      &
    option ('depth', .true., 'd',                                            &
        'Decoding depth (1-3), default DEPTH=1', 'DEPTH'),                   &
    option ('tx-jt9', .false., 'T',                                         &
        'Tx mode is JT9 (combined-mode streaming only)', ''),                &
    option ('my-call', .true., 'c', 'my callsign', 'CALL'),                  &
    option ('my-grid', .true., 'G', 'my grid locator', 'GRID'),              &
    option ('his-call', .true., 'x', 'his callsign', 'CALL'),                &
    option ('his-grid', .true., 'g', 'his grid locator', 'GRID'),            &
    option ('experience-decode', .true., 'X',                                &
        'experience based decoding flags (1..n), default FLAGS=0',           &
        'FLAGS') ]

  type(dec_data), allocatable :: shared_data
  character(len=12) :: mycall='K1ABC', hiscall='W9XYZ'
  character(len=6) :: mygrid='', hisgrid='EN37'
  common/patience/npatience,nthreads
  common/decstats/ntry65a,ntry65b,n65a,n65b,num9,numfano
  data npatience/1/,nthreads/1/,wisfile/' '/

  program_name = 'jt9'
  if (jt9_stream_available()) program_name = 'wsjt-codec'
  nsubmode = 0
  ntol = 20
  TRperiod=60.d0

  do
     call getopt('hvs:e:a:b:r:m:p:d:f:F:w:t:98765430WYqkTMUSZL:S:H:c:G:x:g:X:Q:C:R:N:E:D:1:',   &
          long_options,c,optarg,arglen,stat,offset,remain,.true.)
     if (stat .ne. 0) then
        exit
     end if
     select case (c)
        case ('h')
           display_help = .true.
        case ('v')
           display_version = .true.
        case ('s')
           read_files = .false.
           shm_key = optarg(:arglen)
        case ('1')
           ipc_lock_path = optarg(:arglen)
        case ('0')
           stream_mode = .true.
        case ('e')
           exe_dir = optarg(:arglen)
        case ('a')
           data_dir = optarg(:arglen)
        case ('r')
           share_dir = optarg(:arglen)   ! read-only shipped-data dir (cty.dat, ALLCALL7.TXT, ...)
        case ('b')
           if (arglen /= 1 .or. optarg(:1) < 'A' .or. optarg(:1) > 'H') then
              call invalid_option('b', optarg, 'must be one uppercase letter A-H')
           end if
           nsubmode = ichar (optarg(:1)) - ichar ('A')
        case ('t')
           temp_dir = optarg(:arglen)
        case ('m')
           call require_integer('m', optarg(:arglen), nthreads)
        case ('p')
           call require_real('p', optarg(:arglen), TRperiod)
           if (TRperiod <= 0.d0 .or. TRperiod > dble(NTMAX)) then
              call invalid_option('p', optarg(:arglen), 'must be between 0 and 1800 seconds')
           end if
        case ('d')
           call require_integer('d', optarg(:arglen), ndepth)
        case ('f')
           call require_integer('f', optarg(:arglen), nrxfreq)
        case ('F')
           call require_integer('F', optarg(:arglen), ntol)
           have_ntol = .true.
        case ('L')
           call require_integer('L', optarg(:arglen), flow)
        case ('S')
           call require_integer('S', optarg(:arglen), fsplit)
        case ('H')
           call require_integer('H', optarg(:arglen), fhigh)
        case ('M')
           multift8 = .true.
        case ('C')
           call require_integer('C', optarg(:arglen), ncycles)
        case ('U')
           hidedupes = .true.
        case ('R')
           call require_integer('R', optarg(:arglen), nft8rxfsens)
        case ('N')
           call require_integer('N', optarg(:arglen), nmt)
           if (nmt < 0 .or. nmt > 24) then
              call invalid_option('N', optarg(:arglen), 'must be between 0 and 24')
           end if
        case ('E')
           call require_integer('E', optarg(:arglen), nmtft8decsens)
        case ('D')
           call require_integer('D', optarg(:arglen), ndecoderstart)
        case ('Z')
           lwidedxcsearch = .false.
        case ('q')
           lquiet=.true.
        case ('k')
           mode = 144
           wav_mode = mode
        case ('Q')
           call require_integer('Q', optarg(:arglen), nQSOProg)
           if (nQSOProg < 0 .or. nQSOProg > 5) then
              call invalid_option('Q', optarg(:arglen), 'must be between 0 and 5')
           end if
        case ('3')
           mode = 66
           wav_mode = mode
        case ('4')
           mode = 4
           wav_mode = mode
        case ('5')
           mode = 5
           wav_mode = mode
        case ('6')
           if (mode.lt.65) mode = mode + 65
           wav_mode = 65
        case ('7')
           mode = 240
           wav_mode = mode
        case ('8')
           mode = 8
           wav_mode = mode
        case ('9')
           if (mode.lt.9.or.mode.eq.65) mode = mode + 9
           wav_mode = 9
        case ('T')
           tx9 = .true.
        case ('w')
           call require_integer('w', optarg(:arglen), npatience)
        case ('W')
           mode = 241
           wav_mode = mode
        case ('Y')
           mode = 242
           wav_mode = mode
        case ('c')
           call require_text('c', optarg, arglen, mycall)
        case ('G')
           call require_text('G', optarg, arglen, mygrid)
        case ('x')
           call require_text('x', optarg, arglen, hiscall)
        case ('g')
           call require_text('g', optarg, arglen, hisgrid)
        case ('X')
           call require_integer('X', optarg(:arglen), nexp_decode)
           nexp_decode_set = .true.
     end select
  end do

  if (display_version) then
     call jt9_print_version()
     stop
  endif

  ! wsjt-codec runs the stream whenever it has no file and no -s; --stream and -0 change nothing.
  if (jt9_stream_available()) stream_mode = read_files .and. remain .lt. 1
  if (stream_mode) then
     read_files = .false.
     if (jt9_stream_available()) then
        if (stream_stdin_is_terminal() .ne. 0) display_help = .true.
     end if
  end if

  if (display_help .or. stat .lt. 0                      &
       .or. (.not. read_files .and. remain .gt. 0)       &
       .or. (read_files .and. remain .lt. 1)) then

     print *, 'Usage: '//trim(program_name)//' [OPTIONS] file1 [file2 ...]'
     print *, '       Reads data from *.wav files; select a mode (for example, --ft8).'
     print *, ''
     if (jt9_stream_available()) then
        print *, '       cat <pcm-stream> | '//trim(program_name)//' [OPTIONS]'
        print *, '       Reads framed PCM and control frames from stdin.'
     else
        print *, '       '//trim(program_name)//                                   &
             ' -s <key> [-w patience] [-m threads] [-e path] [-a path] [-t path] [-r path]'
        print *, '       Gets data from the native shared memory name supplied with -s'
     end if
     print *, ''
     print *, 'OPTIONS:'
     print *, ''
     do i = 1, size (long_options)
       select case (trim (long_options(i) % name))
       case ('stream')
          cycle
       case ('shmem', 'ipc-lock')
          if (jt9_stream_available()) cycle
       end select
       call long_options(i) % print (6)
     end do
     stop
  endif

  ! WAV decoding selects one mode; stream startup retains the legacy mode combinations.
  if (read_files) mode = wav_mode
  if (read_files .and. mode == 0) then
     write(error_unit, '(A)') trim(program_name) // ': WAV decoding requires a mode (for example, --ft8)'
     stop 2
  end if
  if (read_files .or. stream_mode) then
     validation_mode = mode
     if (stream_mode .and. validation_mode == 0) validation_mode = 8
     call require_mode_submode(validation_mode)
  end if

  if (read_files) then
     if (ndecoderstart < 0 .or. ndecoderstart > 4) then
        write(optarg, '(I0)') ndecoderstart
        call invalid_option('D', optarg, 'must be between 0 and 4')
     end if
     if (nmtft8decsens < 1 .or. nmtft8decsens > 3) then
        write(optarg, '(I0)') nmtft8decsens
        call invalid_option('E', optarg, 'must be between 1 and 3')
     end if
  end if

  iret=fftwf_init_threads()            !Initialize FFTW threading

! Default to 1 thread, but use nthreads for the big ones
  call fftwf_plan_with_nthreads(1)

! Import FFTW wisdom, if available
  wisfile=trim(data_dir)//'/jt9_wisdom.dat'// C_NULL_CHAR
  iret=fftwf_import_wisdom_from_filename(wisfile)

  ntry65a=0
  ntry65b=0
  n65a=0
  n65b=0
  num9=0
  numfano=0

  if (.not. read_files) then
     if (stream_mode) then
        call streaming_emit_set_enabled(.true.)
        if (mode .eq. 0) then
           mode = 8                            ! default: FT8
           if (TRperiod .eq. 60.d0) TRperiod = 15.d0
        end if
        if (mode .eq. 5  .and. TRperiod .eq. 60.d0) TRperiod = 7.5d0   ! FT4
        if (mode .eq. 144 .and. TRperiod .eq. 60.d0) TRperiod = 30.d0  ! MSK144

        ntol=cli_tolerance(mode,ntol,have_ntol)
        nexp_decode=cli_exp_decode(mode,nexp_decode,nexp_decode_set)
        call normalize_station_options(mycall,hiscall,hisgrid)
        allocate(shared_data)
        ! jt9's stub refuses here, before anything is written.
        if (.not. jt9_stream_available()) call jt9_stream(shared_data, mode, TRperiod)
        call init_timer (trim(data_dir)//'/timer.out')
        shared_data%id2 = 0

        ! Common params first, then streaming-only fields.
        args = cli_args_t(                                                     &
             flow=flow, fsplit=fsplit, fhigh=fhigh, nrxfreq=nrxfreq,            &
             ndepth=ndepth, ntol=ntol, nQSOProg=nQSOProg,                      &
             nexp_decode=nexp_decode, ncycles=ncycles,                          &
             nft8rxfsens=nft8rxfsens, nmt=nmt, ndecoderstart=ndecoderstart,    &
             nsubmode=nsubmode, mycall=mycall, hiscall=hiscall,                &
             mygrid=mygrid, hisgrid=hisgrid,                                    &
             datetime='2026-Apr-25 00:00   ',                                  &
             tx9=tx9, multift8=.false., hidedupes=.false.,                     &
             lft8lowth=.true., lft8subpass=.true.,                             &
             lwidedxcsearch=lwidedxcsearch, have_ntol=have_ntol,               &
             nexp_decode_set=nexp_decode_set)
        call init_default_params(shared_data%params, mode, TRperiod, args)
        call init_streaming_extra_fields(shared_data%params, args)

        ! Streaming-side overrides post-init.
        ! (mode=9 nfa=fsplit handled by init_default_params per-mode adjustment.)
        ! (configure switches multithreaded FT8 on; -M/-U/-E do not apply, and
        !  -N/-D/-C/-R/-Z only set starting values.)

        call jt9_stream(shared_data, mode, TRperiod)
     else
        call jt9a()       !We're running under control of WSJT-X (shared memory)
     end if
     go to 999
  endif

  call normalize_station_options(mycall,hiscall,hisgrid)
  ntol=cli_tolerance(mode,ntol,have_ntol)
  nexp_decode=cli_exp_decode(mode,nexp_decode,nexp_decode_set)

  allocate(wav_samples(NMAX))
  nflatten=0
  do iarg = offset + 1, offset + remain
     call get_command_argument (iarg, optarg, arglen)
     infile = optarg(:arglen)
     call wav%read (infile, wav_status, optarg)
     if (wav_status /= 0) then
        write(error_unit, '(A)') trim(program_name) // ': cannot read WAV file ' // trim(infile) // &
             ': ' // trim(optarg)
        stop 2
     end if
     nfsample=wav%audio_format%sample_rate
     if (nfsample /= 12000 .and. .not. (mode == 4 .and. nfsample == 11025)) then
        close(unit=wav%lun)
        write(error_unit, '(A,I0,A)') trim(program_name) // ': unsupported ',nfsample, &
             ' Hz WAV sample rate for the selected mode'
        stop 2
     end if
     call parse_wav_filename_nutc(infile, nutc, wav_utc_has_seconds)
     nutc=adjust_wav_nutc(mode,nutc,wav_utc_has_seconds)
     read_plan=plan_wav_read(mode,TRperiod,nfsample)
     kstep=WAV_BLOCK_SAMPLES
     k=0
     engine_valid_samples=0
     nhsym=0
     if(iarg .eq. offset + 1) then
        call init_timer (trim(data_dir)//'/timer.out')
        call timer('jt9     ',0)
     endif
     wav_samples=0          ! Fixed-window decoders and gates need silence beyond this file.
     do iblk=1,read_plan%block_count
        k0=(iblk-1)*kstep
        k=min(iblk*kstep,read_plan%sample_count)
        if(k.gt.read_plan%read_limit) exit
        call timer('read_wav',0)
        call wav%read_samples(wav_samples(k0+1:k), samples_read, &
             wav_status, optarg)
        call timer('read_wav',1)
        if (wav_status /= 0) then
           close(unit=wav%lun)
           write(error_unit, '(A)') trim(program_name) // ': cannot read WAV file ' // &
                trim(infile) // ': ' // trim(optarg)
           stop 2
        end if
        if (samples_read == 0) then
           if(mode.ne.9.or.engine_valid_samples.eq.0) print*,'EOF on input file ',trim(infile)
           exit
        end if
        engine_valid_samples=k0+samples_read
        nhsym=(k-2048)/kstep
        if(read_plan%half_symbol_limit>0 .and. nhsym>=read_plan%half_symbol_limit) exit
        if (samples_read < k-k0) then
           if(mode.ne.9) print*,'EOF on input file ',trim(infile)
           exit
        end if
     enddo
     close(unit=wav%lun)
     engine_input_id=int(iarg-offset,c_int64_t)
     engine_attempt_no=0

     args = cli_args_t(                                                    &
          flow=flow, fsplit=fsplit, fhigh=fhigh, nrxfreq=nrxfreq,            &
          ndepth=ndepth, ntol=ntol, nQSOProg=nQSOProg,                      &
          nexp_decode=nexp_decode, ncycles=ncycles,                         &
          nft8rxfsens=nft8rxfsens, nmt=nmt,                                 &
          nsubmode=nsubmode, mycall=mycall, hiscall=hiscall,                 &
          mygrid=mygrid, hisgrid=hisgrid,                                   &
          multift8=multift8, hidedupes=hidedupes,                            &
          lwidedxcsearch=lwidedxcsearch, have_ntol=have_ntol)
     if (mode.eq.144) then
        msk_options=msk144_file_options(int(TRperiod),nutc,ntol,nrxfreq,ndepth,mycall,hiscall)
        call decode_msk144_file(wav_samples, msk_options, data_dir, msk_completion)
        if (.not. lquiet) call write_decode_completion(msk_completion)
        call flush(6)
        cycle
     endif

     pass_plan=plan_wav_passes(mode,multift8,ndecoderstart,nmtft8decsens,nhsym,engine_valid_samples)
     args%lft8lowth=pass_plan%low_threshold
     args%lft8subpass=pass_plan%subtract_pass
     do ipass=1,pass_plan%count
        if(pass_plan%passes(ipass)%prefix_copy) then
           prefix_samples=pass_plan%passes(ipass)%stage*WAV_BLOCK_SAMPLES
           id2a(:prefix_samples)=wav_samples(:prefix_samples)
           id2a(prefix_samples+1:)=0
           call run_engine_wav(id2a,pass_plan%passes(ipass))
        else
           call run_engine_wav(wav_samples,pass_plan%passes(ipass))
        endif
     enddo
  enddo

  call timer('jt9     ',1)
  call timer('jt9     ',101)

999 continue

! Output decoder statistics
  call fini_timer ()

! Save FFTW wisdom and free memory
  if(len(trim(wisfile)).gt.0) iret=fftwf_export_wisdom_to_filename(wisfile)
  if (allocated(wav_samples)) then
     use_mtd_fft_plans=mode.eq.8 .and. multift8
  else if (allocated(shared_data)) then
     use_mtd_fft_plans=mode.eq.8 .and. shared_data%params%lmultift8
  endif
  if(use_mtd_fft_plans) then
     call four2avar(a,-1,1,1,1)
     call filbigvar(-1.,0,0.,0,0.,0.,0)       !used for FFT plans for FT8 multithread detector
  else
     call four2a(a,-1,1,1,1)
  endif
  call fftwf_cleanup_threads()
  call fftwf_cleanup()
  if (allocated(wav_samples)) deallocate(wav_samples)
  if (allocated(shared_data)) deallocate(shared_data)

contains

  subroutine require_mode_submode(selected_mode)
    integer, intent(in) :: selected_mode

    integer :: maximum
    character(len=16) :: mode_name
    character(len=64) :: reason

    maximum = submode_limit(selected_mode)
    if (selected_mode == 74 .and. tx9) maximum = submode_limit(65)
    if (nsubmode <= maximum) return
    select case (selected_mode)
    case (4)
       mode_name = 'JT4'
    case (65)
       mode_name = 'JT65'
    case (66)
       mode_name = 'Q65'
    case (74)
       mode_name = 'JT9+JT65'
    case default
       mode_name = 'selected mode'
    end select
    reason = 'must be A-' // achar(iachar('A') + maximum) // ' for ' // trim(mode_name)
    call invalid_option('b', achar(iachar('A') + nsubmode), trim(reason))
  end subroutine require_mode_submode

  subroutine require_text(option, text, text_length, value)
    character(len=*), intent(in) :: option, text
    integer, intent(in) :: text_length
    character(len=*), intent(out) :: value

    logical :: ok
    character(len=96) :: reason

    call parse_fixed_text(text, text_length, value, ok)
    if (.not. ok) then
       write(reason, '(A,I0,A)') 'must be at most ', len(value), ' characters with no internal whitespace'
       call invalid_option(option, text, trim(reason))
    end if
  end subroutine require_text

  subroutine run_engine_wav(samples, pass)
    integer*2, intent(inout) :: samples(*)
    type(wav_decode_pass), intent(in) :: pass
    type(decode_completion_result) :: completion
    type(attempt_request) :: wav_request
    type(host_decode_context) :: wav_context
    integer :: now, time
    intrinsic :: time

    now=0
    if(mode==66) now=time()
    engine_attempt_no=engine_attempt_no+1
    call build_wav_request(args,mode,TRperiod,nutc,pass%stage,pass%mtd, &
         engine_input_id,engine_attempt_no,now,wav_request)
    wav_context=host_decode_context(legacy_mode=mode,utc=nutc,half_symbol_stage=pass%stage, &
         print_hash22=mode.eq.242)
    call engine_host_decode_request(samples,wav_request,wav_context,nfsample,completion,0, &
         pass%sample_count)
    if(.not.completion%available) return
    if(.not.lquiet) call write_decode_completion(completion)
    call flush(6)
  end subroutine run_engine_wav

  subroutine require_integer(option, text, value)
    character(len=*), intent(in) :: option, text
    integer, intent(out) :: value

    logical :: ok

    call parse_integer(text, value, ok)
    if (.not. ok) call invalid_option(option, text, 'must be an integer')
  end subroutine require_integer

  subroutine require_real(option, text, value)
    character(len=*), intent(in) :: option, text
    real(kind=8), intent(out) :: value

    logical :: ok

    call parse_real(text, value, ok)
    if (.not. ok) call invalid_option(option, text, 'must be a finite number')
  end subroutine require_real

  subroutine invalid_option(option, text, reason)
    character(len=*), intent(in) :: option, text, reason

    write(error_unit, '(A)') trim(program_name) // ': invalid value for -' // trim(option) // ': ' // &
         trim(text) // ' (' // trim(reason) // ')'
    stop 2
  end subroutine invalid_option

end program jt9
