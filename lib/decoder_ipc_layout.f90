module decoder_ipc_layout_module
  use, intrinsic :: iso_c_binding
  include 'jt9com.f90'
  private
  public :: decoder_ipc_fortran_layout, decoder_ipc_fortran_validate
  public :: decoder_ipc_report_layout_error
  public :: DECODER_IPC_LAYOUT_VERSION, DECODER_IPC_LAYOUT_ATTACH

  integer(c_int), parameter :: DECODER_IPC_LAYOUT_VERSION = 4
  integer(c_int), parameter :: DECODER_IPC_LAYOUT_ATTACH = 11

  interface
     subroutine decoder_ipc_report_layout_error(status, storage, available_bytes) bind(C)
       import :: c_ptr, c_size_t, c_int
       integer(c_int), value :: status
       type(c_ptr), value :: storage
       integer(c_size_t), value :: available_bytes
     end subroutine decoder_ipc_report_layout_error

     function validate_c(storage, available_bytes, peer_layout) &
          bind(C, name="decoder_ipc_validate_layout") result(status)
       import :: c_ptr, c_size_t, c_int
       type(c_ptr), value :: storage, peer_layout
       integer(c_size_t), value :: available_bytes
       integer(c_int) :: status
     end function validate_c
  end interface

contains

  subroutine decoder_ipc_fortran_layout(layout) bind(C)
    type(decoder_ipc_layout), intent(out) :: layout
    type(shared_dec_data), target, save :: local
    integer(c_intptr_t) :: base
    integer :: index

    base=transfer(c_loc(local),base)
    layout%magic=int(z'57534a54',c_int)
    layout%protocol_version=DECODER_IPC_VERSION
    layout%header_bytes=int(transfer(c_loc(local%payload),base)-base,c_int)
    layout%payload_bytes=int(c_sizeof(local%payload),c_int)
    layout%logical_bytes=int(c_sizeof(local),c_int)
    layout%payload_offset=layout%header_bytes
    layout%capabilities=7
    layout%int_bytes=int(c_sizeof(local%control%generation),c_int)
    layout%short_bytes=int(c_sizeof(local%payload%id2(1)),c_int)
    layout%float_bytes=int(c_sizeof(local%payload%ss(1,1)),c_int)
    layout%bool_bytes=int(c_sizeof(local%payload%params%ndiskdat),c_int)
    layout%char_bytes=int(c_sizeof(local%payload%params%datetime(1)),c_int)
    layout%endian_marker=int(z'01020304',c_int)
    layout%field_count=size(layout%fields)
    index=0
    call field(c_loc(local%control%generation), c_sizeof(local%control%generation))
    call field(c_loc(local%control%state), c_sizeof(local%control%state))
    call field(c_loc(local%control%version), c_sizeof(local%control%version))
    call field(c_loc(local%control%progress), c_sizeof(local%control%progress))
    call field(c_loc(local%payload%ss), c_sizeof(local%payload%ss))
    call field(c_loc(local%payload%savg), c_sizeof(local%payload%savg))
    call field(c_loc(local%payload%sred), c_sizeof(local%payload%sred))
    call field(c_loc(local%payload%id2), c_sizeof(local%payload%id2))
    call field(c_loc(local%payload%params), c_sizeof(local%payload%params))
    call field(c_loc(local%payload%params%nutc), c_sizeof(local%payload%params%nutc))
    call field(c_loc(local%payload%params%ndiskdat), c_sizeof(local%payload%params%ndiskdat))
    call field(c_loc(local%payload%params%ntr), c_sizeof(local%payload%params%ntr))
    call field(c_loc(local%payload%params%nQSOProgress), c_sizeof(local%payload%params%nQSOProgress))
    call field(c_loc(local%payload%params%nfqso), c_sizeof(local%payload%params%nfqso))
    call field(c_loc(local%payload%params%nftx), c_sizeof(local%payload%params%nftx))
    call field(c_loc(local%payload%params%newdat), c_sizeof(local%payload%params%newdat))
    call field(c_loc(local%payload%params%nfa), c_sizeof(local%payload%params%nfa))
    call field(c_loc(local%payload%params%nfb), c_sizeof(local%payload%params%nfb))
    call field(c_loc(local%payload%params%ntol), c_sizeof(local%payload%params%ntol))
    call field(c_loc(local%payload%params%kin), c_sizeof(local%payload%params%kin))
    call field(c_loc(local%payload%params%nzhsym), c_sizeof(local%payload%params%nzhsym))
    call field(c_loc(local%payload%params%nsubmode), c_sizeof(local%payload%params%nsubmode))
    call field(c_loc(local%payload%params%nagain), c_sizeof(local%payload%params%nagain))
    call field(c_loc(local%payload%params%ndepth), c_sizeof(local%payload%params%ndepth))
    call field(c_loc(local%payload%params%lft8apon), c_sizeof(local%payload%params%lft8apon))
    call field(c_loc(local%payload%params%lapcqonly), c_sizeof(local%payload%params%lapcqonly))
    call field(c_loc(local%payload%params%ljt65apon), c_sizeof(local%payload%params%ljt65apon))
    call field(c_loc(local%payload%params%napwid), c_sizeof(local%payload%params%napwid))
    call field(c_loc(local%payload%params%nmode), c_sizeof(local%payload%params%nmode))
    call field(c_loc(local%payload%params%minw), c_sizeof(local%payload%params%minw))
    call field(c_loc(local%payload%params%nclearave), c_sizeof(local%payload%params%nclearave))
    call field(c_loc(local%payload%params%minSync), c_sizeof(local%payload%params%minSync))
    call field(c_loc(local%payload%params%emedelay), c_sizeof(local%payload%params%emedelay))
    call field(c_loc(local%payload%params%dttol), c_sizeof(local%payload%params%dttol))
    call field(c_loc(local%payload%params%nlist), c_sizeof(local%payload%params%nlist))
    call field(c_loc(local%payload%params%listutc), c_sizeof(local%payload%params%listutc))
    call field(c_loc(local%payload%params%n2pass), c_sizeof(local%payload%params%n2pass))
    call field(c_loc(local%payload%params%nranera), c_sizeof(local%payload%params%nranera))
    call field(c_loc(local%payload%params%naggressive), c_sizeof(local%payload%params%naggressive))
    call field(c_loc(local%payload%params%nexp_decode), c_sizeof(local%payload%params%nexp_decode))
    call field(c_loc(local%payload%params%max_drift), c_sizeof(local%payload%params%max_drift))
    call field(c_loc(local%payload%params%datetime), c_sizeof(local%payload%params%datetime))
    call field(c_loc(local%payload%params%mycall), c_sizeof(local%payload%params%mycall))
    call field(c_loc(local%payload%params%mygrid), c_sizeof(local%payload%params%mygrid))
    call field(c_loc(local%payload%params%hiscall), c_sizeof(local%payload%params%hiscall))
    call field(c_loc(local%payload%params%hisgrid), c_sizeof(local%payload%params%hisgrid))
    call field(c_loc(local%payload%params%b_even_seq), c_sizeof(local%payload%params%b_even_seq))
    call field(c_loc(local%payload%params%b_superfox), c_sizeof(local%payload%params%b_superfox))
    call field(c_loc(local%payload%params%yymmdd), c_sizeof(local%payload%params%yymmdd))
    call field(c_loc(local%payload%params%mybcall), c_sizeof(local%payload%params%mybcall))
    call field(c_loc(local%payload%params%hisbcall), c_sizeof(local%payload%params%hisbcall))
    call field(c_loc(local%payload%params%ncandthin), c_sizeof(local%payload%params%ncandthin))
    call field(c_loc(local%payload%params%ndtcenter), c_sizeof(local%payload%params%ndtcenter))
    call field(c_loc(local%payload%params%nft8cycles), c_sizeof(local%payload%params%nft8cycles))
    call field(c_loc(local%payload%params%ntrials10), c_sizeof(local%payload%params%ntrials10))
    call field(c_loc(local%payload%params%ntrialsrxf10), c_sizeof(local%payload%params%ntrialsrxf10))
    call field(c_loc(local%payload%params%nharmonicsdepth), c_sizeof(local%payload%params%nharmonicsdepth))
    call field(c_loc(local%payload%params%ntopfreq65), c_sizeof(local%payload%params%ntopfreq65))
    call field(c_loc(local%payload%params%nprepass), c_sizeof(local%payload%params%nprepass))
    call field(c_loc(local%payload%params%nsdecatt), c_sizeof(local%payload%params%nsdecatt))
    call field(c_loc(local%payload%params%nlasttx), c_sizeof(local%payload%params%nlasttx))
    call field(c_loc(local%payload%params%ndelay), c_sizeof(local%payload%params%ndelay))
    call field(c_loc(local%payload%params%nmt), c_sizeof(local%payload%params%nmt))
    call field(c_loc(local%payload%params%nft8rxfsens), c_sizeof(local%payload%params%nft8rxfsens))
    call field(c_loc(local%payload%params%nft4depth), c_sizeof(local%payload%params%nft4depth))
    call field(c_loc(local%payload%params%nsecbandchanged), c_sizeof(local%payload%params%nsecbandchanged))
    call field(c_loc(local%payload%params%nagainfil), c_sizeof(local%payload%params%nagainfil))
    call field(c_loc(local%payload%params%nstophint), c_sizeof(local%payload%params%nstophint))
    call field(c_loc(local%payload%params%nhint), c_sizeof(local%payload%params%nhint))
    call field(c_loc(local%payload%params%fmaskact), c_sizeof(local%payload%params%fmaskact))
    call field(c_loc(local%payload%params%lmultift8), c_sizeof(local%payload%params%lmultift8))
    call field(c_loc(local%payload%params%lft8lowth), c_sizeof(local%payload%params%lft8lowth))
    call field(c_loc(local%payload%params%lft8subpass), c_sizeof(local%payload%params%lft8subpass))
    call field(c_loc(local%payload%params%ltxing), c_sizeof(local%payload%params%ltxing))
    call field(c_loc(local%payload%params%lhideft8dupes), c_sizeof(local%payload%params%lhideft8dupes))
    call field(c_loc(local%payload%params%lhound), c_sizeof(local%payload%params%lhound))
    call field(c_loc(local%payload%params%lcommonft8b), c_sizeof(local%payload%params%lcommonft8b))
    call field(c_loc(local%payload%params%lmycallstd), c_sizeof(local%payload%params%lmycallstd))
    call field(c_loc(local%payload%params%lhiscallstd), c_sizeof(local%payload%params%lhiscallstd))
    call field(c_loc(local%payload%params%lapmyc), c_sizeof(local%payload%params%lapmyc))
    call field(c_loc(local%payload%params%lmodechanged), c_sizeof(local%payload%params%lmodechanged))
    call field(c_loc(local%payload%params%lbandchanged), c_sizeof(local%payload%params%lbandchanged))
    call field(c_loc(local%payload%params%lenabledxcsearch), c_sizeof(local%payload%params%lenabledxcsearch))
    call field(c_loc(local%payload%params%lwidedxcsearch), c_sizeof(local%payload%params%lwidedxcsearch))
    call field(c_loc(local%payload%params%lmultinst), c_sizeof(local%payload%params%lmultinst))
    call field(c_loc(local%payload%params%lskiptx1), c_sizeof(local%payload%params%lskiptx1))
    call field(c_loc(local%payload%params%ndecoderstart), c_sizeof(local%payload%params%ndecoderstart))

    call field(c_loc(local%metadata), c_sizeof(local%metadata))
    call field(c_loc(local%metadata%input_id), c_sizeof(local%metadata%input_id))
    call field(c_loc(local%metadata%analysis_id), c_sizeof(local%metadata%analysis_id))
    call field(c_loc(local%metadata%attempt_no), c_sizeof(local%metadata%attempt_no))
    call field(c_loc(local%metadata%valid_samples), c_sizeof(local%metadata%valid_samples))

  contains

    subroutine field(address, bytes)
      type(c_ptr), value :: address
      integer(c_size_t), value :: bytes
      integer(c_intptr_t) :: location

      index=index+1
      location=transfer(address,location)
      layout%fields(index)%offset=int(location-base,c_int)
      layout%fields(index)%bytes=int(bytes,c_int)
    end subroutine field
  end subroutine decoder_ipc_fortran_layout

  function decoder_ipc_fortran_validate(storage, available_bytes) bind(C) result(status)
    type(c_ptr), value :: storage
    integer(c_size_t), value :: available_bytes
    integer(c_int) :: status
    type(decoder_ipc_layout), target :: expected

    call decoder_ipc_fortran_layout(expected)
    status=validate_c(storage,available_bytes,c_loc(expected))
  end function decoder_ipc_fortran_validate
end module decoder_ipc_layout_module
