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
  use jt9_input_validation, only: parse_integer, parse_real, parse_wav_filename_nutc
  use, intrinsic :: iso_fortran_env, only: error_unit
  use ft8_mod1, only : dd8
  use jt65_mod6, only : dd
  use streaming_emit, only: streaming_emit_set_enabled, streaming_emit_error
  use jt9_params_init, only: init_default_params, init_streaming_extra_fields, &
       apply_per_mode_policy, cli_args_t

  include 'jt9com.f90'

  interface
     subroutine jt9_print_version() bind(C, name='jt9_print_version')
     end subroutine jt9_print_version
  end interface

  type(cli_args_t) :: args
  integer*2 id2a(180000)
  integer(C_INT) iret
  type(wav_header) wav
  real*4 s(NSMAX)
  real*8 TRperiod
  integer npct_unused

  character c
  character(len=500) optarg, infile
  character wisfile*256

  integer :: arglen,stat,offset,remain,mode=0,flow=200,fsplit=2700,          &
       fhigh=4000,nrxfreq=1500,ndepth=1,nexp_decode=0,nQSOProg=0,ncycles=3,  &
       nft8rxfsens=3,nmt=0,nmtft8decsens=3,ndecoderstart=3, samples_read, wav_status
  logical :: read_files = .true., tx9 = .false., display_help = .false.,     &
       display_version = .false.,                                            &
       bLowSidelobes = .false., nexp_decode_set = .false.,                   &
       have_ntol = .false.,multift8 = .false.,hidedupes = .false.,           &
       lft8lowth = .true.,lft8subpass = .true.,lwidedxcsearch = .true.,      &
       stream_mode = .false.
  type (option) :: long_options(43) = [                                      &
    option ('help', .false., 'h', 'Display this help message', ''),          &
    option ('version', .false., 'v', 'Display version and build revision', ''),&
    option ('shmem',.true.,'s','Use shared memory for sample data','KEY'),   &
    option ('stream', .false., '0',                                          &
        'Read framed PCM samples from stdin',                                &
        ''),                                                                 &
    option ('tr-period', .true., 'p', 'Tx/Rx period, default SECONDS=60',    &
        'SECONDS'),                                                          &
    option ('executable-path', .true., 'e',                                  &
        'Location of subordinate executables (KVASD) default PATH="."',      &
        'PATH'),                                                             &
    option ('data-path', .true., 'a',                                        &
        'Location of writeable data files, default PATH="."', 'PATH'),       &
    option ('temp-path', .true., 't',                                        &
        'Temporary files path, default PATH="."', 'PATH'),                   &
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
    option ('fst4w', .false., 'Y', 'FST4W mode, print hash22 values', ''),   &
    option ('ft8', .false., '8', 'FT8 mode', ''),                            &
    option ('jt9', .false., '9', 'JT9 mode', ''),                            &
    option ('quiet', .false., 'q', 'Quiet mode, no <DecodeFinished>', ''),   &
    option ('msk144', .false., 'k', 'MSK144 mode', ''),                      &
    option ('QSOprog', .true., 'Q', 'QSO progress (0-5), default PROGRESS=1',&
        'QSOprogress'),                                                      &
    option ('sub-mode', .true., 'b', 'Sub mode, default SUBMODE=A', 'A'),    &
    option ('depth', .true., 'd',                                            &
        'Decoding depth (1-3), default DEPTH=1', 'DEPTH'),                   &
    option ('tx-jt9', .false., 'T', 'Tx mode is JT9', ''),                   &
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

  nsubmode = 0
  ntol = 20
  TRperiod=60.d0

  do
     call getopt('hvs:e:a:b:r:m:p:d:f:F:w:t:9876543WYqkTMUSZL:S:H:c:G:x:g:X:Q:C:R:N:E:D:',    &
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
        case ('0')
           read_files = .false.
           stream_mode = .true.
           call streaming_emit_set_enabled(.true.)
        case ('e')
           exe_dir = optarg(:arglen)
        case ('a')
           data_dir = optarg(:arglen)
        case ('r')
           share_dir = optarg(:arglen)   ! read-only shipped-data dir (cty.dat, ALLCALL7.TXT, ...)
        case ('b')
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
        case ('Q')
           call require_integer('Q', optarg(:arglen), nQSOProg)
           if (nQSOProg < 0 .or. nQSOProg > 5) then
              call invalid_option('Q', optarg(:arglen), 'must be between 0 and 5')
           end if
        case ('3')
           mode = 66
        case ('4')
           mode = 4
        case ('5')
           mode = 5
        case ('6')
           if (mode.lt.65) mode = mode + 65
        case ('7')
           mode = 240
        case ('8')
           mode = 8
        case ('9')
           if (mode.lt.9.or.mode.eq.65) mode = mode + 9
        case ('T')
           tx9 = .true.
        case ('w')
           call require_integer('w', optarg(:arglen), npatience)
        case ('W')
           mode = 241
        case ('Y')
           mode = 242
        case ('c')
           read (optarg(:arglen), *) mycall
        case ('G')
           read (optarg(:arglen), *) mygrid
        case ('x')
           read (optarg(:arglen), *) hiscall
        case ('g')
           read (optarg(:arglen), *) hisgrid
        case ('X')
           call require_integer('X', optarg(:arglen), nexp_decode)
           nexp_decode_set = .true.
     end select
  end do

  if (display_version) then
     call jt9_print_version()
     stop
  endif
  
  if (display_help .or. stat .lt. 0                      &
       .or. (.not. read_files .and. remain .gt. 0)       &
       .or. (read_files .and. remain .lt. 1)) then

     print *, 'Usage: jt9 [OPTIONS] file1 [file2 ...]'
     print *, '       Reads data from *.wav files.'
     print *, ''
     print *, '       jt9 -s <key> [-w patience] [-m threads] [-e path] [-a path] [-t path] [-r path]'
     print *, '       Gets data from shared memory region with key==<key>'
     print *, ''
     print *, '       cat <pcm-stream> | jt9 --stream'
     print *, '       Reads framed PCM samples from stdin.'
     print *, ''
     print *, 'OPTIONS:'
     print *, ''
     do i = 1, size (long_options)
       call long_options(i) % print (6)
     end do
     stop
  endif

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
        ! Streaming subprocess mode. Mirror the WAV path's shared_data setup,
        ! then hand off to the streaming reader.
        if (mode .eq. 0) then
           mode = 8                            ! default: FT8
           if (TRperiod .eq. 60.d0) TRperiod = 15.d0
        end if
        if (mode .eq. 5  .and. TRperiod .eq. 60.d0) TRperiod = 7.5d0   ! FT4
        if (mode .eq. 144 .and. TRperiod .eq. 60.d0) TRperiod = 30.d0  ! MSK144

        ! Per-mode ntol defaulting (mirrors WAV path lines 277–285).
        if (mode .eq. 241 .or. mode .eq. 242) then
           ntol = min(ntol, 100)
        else if (mode .eq. 65 + 9 .and. .not. have_ntol) then
           ntol = 20
        else if (mode .eq. 66 .and. .not. have_ntol) then
           ntol = 10
        else
           ntol = min(ntol, 1000)
        end if

        ! Per-mode nexp_decode default (mirrors WAV path lines 287–291).
        if (.not. nexp_decode_set) then
           if (mode .eq. 240 .or. mode .eq. 241 .or. mode .eq. 242) then
              nexp_decode = 3 * 256   ! FST4: single decode off, nb=0
           end if
        end if

        if (mycall.eq.'b') mycall = '            '
        if (hiscall.eq.'b') then
           hiscall = '            '
           hisgrid = '      '
        end if
        allocate(shared_data)
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
        ! (single-pass FT8 only in stream — already enforced via args.multift8=.false.)

        call jt9_stream(shared_data, mode, TRperiod)
     else
        call jt9a()       !We're running under control of WSJT-X (shared memory)
     end if
     go to 999
  endif

  if(mycall.eq.'b') mycall='            '
  if(hiscall.eq.'b') then
     hiscall='            '
     hisgrid='      '
  endif

  if (mode .eq. 241 .or. mode .eq. 242) then
     ntol = min (ntol, 100)
  else if (mode .eq. 65 + 9 .and. .not. have_ntol) then
     ntol = 20
  else if (mode .eq. 66 .and. .not. have_ntol) then
     ntol = 10
  else
     ntol = min (ntol, 1000)
  end if

  if (.not. nexp_decode_set) then
     if (mode .eq. 240 .or. mode .eq. 241 .or. mode .eq. 242) then
        nexp_decode = 3 * 256   ! single decode off and nb=0
     end if
  end if
  
  allocate(shared_data)
  nflatten=0
  do iarg = offset + 1, offset + remain
     call get_command_argument (iarg, optarg, arglen)
     infile = optarg(:arglen)
     call wav%read (infile, wav_status, optarg)
     if (wav_status /= 0) then
        write(error_unit, '(A)') 'jt9: cannot read WAV file ' // trim(infile) // ': ' // trim(optarg)
        stop 2
     end if
     nfsample=wav%audio_format%sample_rate
     if (nfsample /= 12000 .and. .not. (mode == 4 .and. nfsample == 11025)) then
        close(unit=wav%lun)
        write(error_unit, '(A,I0,A)') 'jt9: unsupported ',nfsample, &
             ' Hz WAV sample rate for the selected mode'
        stop 2
     end if
     call parse_wav_filename_nutc(infile, nutc)
     nsps=6912
     npts=TRperiod*12000.d0
     kstep=nsps/2
     k=0
     nhsym=0
     nhsym0=-999
     if(iarg .eq. offset + 1) then
        call init_timer (trim(data_dir)//'/timer.out')
        call timer('jt9     ',0)
     endif
     shared_data%id2=0          !??? Why is this necessary ???
     if(mode.eq.5) npts=21*3456
     if(mode.eq.66) npts=TRperiod*12000
     do iblk=1,npts/kstep
        k=iblk*kstep
        if(mode.eq.8 .and. k.gt.179712) exit
        call timer('read_wav',0)
        call wav%read_samples(shared_data%id2(k-kstep+1:k), samples_read, &
             wav_status, optarg)
        call timer('read_wav',1)
        if (wav_status /= 0) then
           close(unit=wav%lun)
           write(error_unit, '(A)') 'jt9: cannot read WAV file ' // &
                trim(infile) // ': ' // trim(optarg)
           stop 2
        end if
        if (samples_read == 0) then
           print*,'EOF on input file ',trim(infile)
           exit
        end if
        nhsym=(k-2048)/kstep
        if(nhsym.ge.1 .and. nhsym.ne.nhsym0) then
           if(mode.eq.9 .or. mode.eq.74) then
! Compute rough symbol spectra for the JT9 decoder
              ingain=0
              call timer('symspec ',0)
              nminw=1
              call symspec(shared_data,k,nsps,ingain,      &
                   bLowSidelobes,nminw,pxdb,s,df3,ihsym,npts8,pxdbmax,npct_unused)
              call timer('symspec ',1)
           endif
           nhsym0=nhsym
           if(nhsym.ge.181 .and. mode.ne.240 .and. mode.ne.241 .and. &
              mode.ne.242 .and. mode.ne.66) exit
        endif
        if (samples_read < kstep) then
           print*,'EOF on input file ',trim(infile)
           exit
        end if
     enddo
     close(unit=wav%lun)

     ! WAV-path-only mode adjustment (mode=164 with submode<100 bumps by 100).
     ! Hoisted before init_default_params so the bumped value is threaded
     ! through the args bundle.
     if (mode.eq.164 .and. nsubmode.lt.100) nsubmode = nsubmode + 100

     ! WAV mode initializes only fields historically set by this path.
     args = cli_args_t(                                                    &
          flow=flow, fsplit=fsplit, fhigh=fhigh, nrxfreq=nrxfreq,           &
          ndepth=ndepth, ntol=ntol, nQSOProg=nQSOProg,                     &
          nexp_decode=nexp_decode, ncycles=ncycles,                         &
          nft8rxfsens=nft8rxfsens, nmt=nmt, ndecoderstart=ndecoderstart,   &
          nsubmode=nsubmode, mycall=mycall, hiscall=hiscall,               &
          mygrid=mygrid, hisgrid=hisgrid,                                   &
          datetime='2013-Apr-16 15:13   ',                                 &
          tx9=tx9, multift8=multift8, hidedupes=hidedupes,                 &
          lft8lowth=lft8lowth, lft8subpass=lft8subpass,                    &
          lwidedxcsearch=lwidedxcsearch, have_ntol=have_ntol,              &
          nexp_decode_set=nexp_decode_set)
     call init_default_params(shared_data%params, mode, TRperiod, args)

     ! WAV-path post-init overrides.
     shared_data%params%nutc      = nutc        ! parsed from filename
     shared_data%params%ndiskdat  = .true.      ! reading from disk
     shared_data%params%nzhsym    = nhsym       ! per-period (matches former jt9.f90:486)
     if (mode.eq.0) shared_data%params%nmode = 65 + 9   ! WAV-only mode=0 → JT65+JT9

     if (multift8 .and. mode.eq.8) then
        shared_data%params%lft8subpass = .true.
        shared_data%params%lhideft8dupes = hidedupes
        shared_data%params%nft8cycles = ncycles
        shared_data%params%nmt = nmt
        if(nmtft8decsens.eq.1) then
           lft8lowth = .false.
           lft8subpass = .false.
        elseif(nmtft8decsens.eq.2) then
           lft8lowth = .true.
           lft8subpass = .false.        
        elseif(nmtft8decsens.eq.3) then
           lft8lowth = .true.
           lft8subpass = .true.              
        end if
        shared_data%params%lft8lowth = lft8lowth
        shared_data%params%lft8subpass = lft8subpass
        shared_data%params%ndecoderstart = ndecoderstart 
        shared_data%params%lwidedxcsearch = lwidedxcsearch
        shared_data%params%ndtcenter = 0
        shared_data%params%lmodechanged = .false.
        shared_data%params%nsecbandchanged = 0
        shared_data%params%ncandthin = 100
        shared_data%params%nft8rxfsens = nft8rxfsens
        shared_data%params%lhound = .false.
        shared_data%params%lcommonft8b = .true.
        shared_data%params%lbandchanged = .false.
        shared_data%params%lmultinst = .false.
        shared_data%params%nlasttx = 0
        shared_data%params%nhint = .false.
        shared_data%params%ndelay = 0
        shared_data%params%nprepass = 4
        shared_data%params%nharmonicsdepth = 0
        shared_data%params%nsdecatt = 1
      !print*,'lwidedxcsearch ',shared_data%params%lwidedxcsearch
     end if
     ! emedelay (Q65 60s), datetime placeholder, and mode=9 nfa=fsplit
     ! handled by init_default_params + apply_per_mode_policy above.

     nearly=50
     if(mode.eq.8 .and. .not.  shared_data%params%lmultift8) then
! "Early" decoding pass, FT8 only, when jt9 reads data from disk
        nearly=41
        shared_data%params%nzhsym=nearly
        id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
        id2a(nearly*3456+1:)=0
        call multimode_decoder(shared_data%ss,id2a,      &
             shared_data%params,nfsample)
        nearly=47
        shared_data%params%nzhsym=nearly
        id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
        id2a(nearly*3456+1:)=0
        call multimode_decoder(shared_data%ss,id2a,      &
             shared_data%params,nfsample)
        id2a(nearly*3456+1:50*3456)=shared_data%id2(nearly*3456+1:50*3456)
        id2a(50*3456+1:)=0
        shared_data%params%nzhsym=50
        call multimode_decoder(shared_data%ss,id2a,      &
             shared_data%params,nfsample)
        cycle
     else if(mode.eq.8 .and. shared_data%params%lmultift8) then
        if(shared_data%params%ndecoderstart.lt.2) then
           nearly=41
           shared_data%params%lmultift8=.false.
           shared_data%params%nzhsym=nearly
           id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
           id2a(nearly*3456+1:)=0
           call multimode_decoder(shared_data%ss,id2a,      &
                shared_data%params,nfsample)
           if(shared_data%params%ndecoderstart.lt.2) then
              nearly=46
              shared_data%params%lmultift8=.false.
              shared_data%params%nzhsym=nearly
              id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
              id2a(nearly*3456+1:)=0
              call multimode_decoder(shared_data%ss,id2a,      &
                   shared_data%params,nfsample)
           endif
           if(shared_data%params%ndecoderstart.eq.0) nearly=49
           if(shared_data%params%ndecoderstart.eq.1) nearly=50
           shared_data%params%lmultift8=.true.
           shared_data%params%nzhsym=nearly
           id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
           id2a(nearly*3456+1:)=0
           dd(1:nearly*3456)=shared_data%id2(1:nearly*3456)
           dd(nearly*3456+1:)=0
           dd8(1:nearly*3456)=shared_data%id2(1:nearly*3456)
           dd8(nearly*3456+1:)=0
        else
           if(shared_data%params%ndecoderstart.eq.2) nearly=48
           if(shared_data%params%ndecoderstart.eq.3) nearly=49
           if(shared_data%params%ndecoderstart.eq.4) nearly=50
           shared_data%params%nzhsym=nearly
           id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
           id2a(nearly*3456+1:)=0
           dd(1:nearly*3456)=shared_data%id2(1:nearly*3456)
           dd(nearly*3456+1:)=0
           dd8(1:nearly*3456)=shared_data%id2(1:nearly*3456)
           dd8(nearly*3456+1:)=0
        endif

     ! MSK144        
     else if (mode .eq. 144) then
        call decode_msk144(shared_data%id2, shared_data%params, data_dir)
     endif

! Normal decoding pass
     call multimode_decoder(shared_data%ss,shared_data%id2, &
          shared_data%params,nfsample)
  enddo

  call timer('jt9     ',1)
  call timer('jt9     ',101)

999 continue

! Output decoder statistics
  call fini_timer ()

! Save FFTW wisdom and free memory
  if(len(trim(wisfile)).gt.0) iret=fftwf_export_wisdom_to_filename(wisfile)
  if(mode.eq.8 .and. shared_data%params%lmultift8) then
     call four2avar(a,-1,1,1,1)
     call filbigvar(-1.,0,0.,0,0.,0.,0)       !used for FFT plans for FT8 multithread detector
  else
     call four2a(a,-1,1,1,1)
     call filbig(a,-1,1,0.0,0,0,0,0,0)        !used for all other FFT plans
  endif
  call fftwf_cleanup_threads()
  call fftwf_cleanup()

contains

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

    write(error_unit, '(A)') 'jt9: invalid value for -' // trim(option) // ': ' // &
         trim(text) // ' (' // trim(reason) // ')'
    stop 2
  end subroutine invalid_option

end program jt9
