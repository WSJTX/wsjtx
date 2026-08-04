  use, intrinsic :: iso_c_binding, only: c_int, c_short, c_float, c_char, c_bool
  include 'constants.f90'

  integer(c_int), parameter :: DECODER_IPC_VERSION = 1
  integer(c_int), parameter :: DECODER_IPC_IDLE = 0
  integer(c_int), parameter :: DECODER_IPC_READY = 1
  integer(c_int), parameter :: DECODER_IPC_DECODING = 2
  integer(c_int), parameter :: DECODER_IPC_COMPLETE = 3
  integer(c_int), parameter :: DECODER_IPC_SHUTDOWN = 999

  !
  ! these structures must be kept in sync with ../commons.h
  !
  type, bind(C) :: decoder_ipc_control
     integer(c_int) :: generation
     integer(c_int) :: state
     integer(c_int) :: version
  end type decoder_ipc_control

  type, bind(C) :: params_block
     integer(c_int) :: nutc
     logical(c_bool) :: ndiskdat
     integer(c_int) :: ntr
     integer(c_int) :: nQSOProgress ! Values are defined by ../QsoProgress.hpp
     integer(c_int) :: nfqso
     integer(c_int) :: nftx
     logical(c_bool) :: newdat
     integer(c_int) :: npts8
     integer(c_int) :: nfa
     integer(c_int) :: nfsplit
     integer(c_int) :: nfb
     integer(c_int) :: ntol
     integer(c_int) :: kin
     integer(c_int) :: nzhsym
     integer(c_int) :: nsubmode
     logical(c_bool) :: nagain
     integer(c_int) :: ndepth
     logical(c_bool) :: lft8apon 
     logical(c_bool) :: lapcqonly
     logical(c_bool) :: ljt65apon 
     integer(c_int) :: napwid
     integer(c_int) :: ntxmode
     integer(c_int) :: nmode
     integer(c_int) :: minw
     logical(c_bool) :: nclearave
     integer(c_int) :: minsync
     real(c_float) :: emedelay
     real(c_float) :: dttol
     integer(c_int) :: nlist
     integer(c_int) :: listutc(10)
     integer(c_int) :: n2pass
     integer(c_int) :: nranera
     integer(c_int) :: naggressive
     logical(c_bool) :: nrobust
     integer(c_int) :: nexp_decode
     integer(c_int) :: max_drift
     character(kind=c_char) :: datetime(20)
     character(kind=c_char) :: mycall(12)
     character(kind=c_char) :: mygrid(6)
     character(kind=c_char) :: hiscall(12)
     character(kind=c_char) :: hisgrid(6)
     logical(c_bool) :: b_even_seq
     logical(c_bool) :: b_superfox
     integer(c_int) :: yymmdd

     ! below is ft8md
     character(kind=c_char) :: mybcall(12)
     character(kind=c_char) :: hisbcall(12)
     integer(c_int) :: ncandthin
     integer(c_int) :: ndtcenter
     integer(c_int) :: nft8cycles
     integer(c_int) :: ntrials10
     integer(c_int) :: ntrialsrxf10
     integer(c_int) :: nharmonicsdepth
     integer(c_int) :: ntopfreq65
     integer(c_int) :: nprepass
     integer(c_int) :: nsdecatt
     integer(c_int) :: nlasttx
     integer(c_int) :: ndelay
     integer(c_int) :: nmt
     integer(c_int) :: nft8rxfsens
     integer(c_int) :: nft4depth
     integer(c_int) :: nsecbandchanged
     logical(c_bool) :: nagainfil
     logical(c_bool) :: nstophint
     logical(c_bool) :: nhint
     logical(c_bool) :: fmaskact
     logical(c_bool) :: lmultift8
     logical(c_bool) :: lft8lowth
     logical(c_bool) :: lft8subpass
     logical(c_bool) :: ltxing
     logical(c_bool) :: lhideft8dupes
     logical(c_bool) :: lhound
     logical(c_bool) :: lcommonft8b
     logical(c_bool) :: lmycallstd
     logical(c_bool) :: lhiscallstd
     logical(c_bool) :: lapmyc
     logical(c_bool) :: lmodechanged
     logical(c_bool) :: lbandchanged
     logical(c_bool) :: lenabledxcsearch
     logical(c_bool) :: lwidedxcsearch
     logical(c_bool) :: lmultinst
     logical(c_bool) :: lskiptx1
     integer(c_int) :: ndecoderstart
  end type params_block

  type, bind(C) :: dec_data
     real(c_float) :: ss(184,NSMAX)
     real(c_float) :: savg(NSMAX)
     real(c_float) :: sred(5760)
     integer(c_short) :: id2(NMAX)
     type(params_block) :: params
  end type dec_data

  type, bind(C) :: shared_dec_data
     type(decoder_ipc_control) :: control
     type(dec_data) :: payload
  end type shared_dec_data
