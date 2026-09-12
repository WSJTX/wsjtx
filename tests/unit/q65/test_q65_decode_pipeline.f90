module q65_pipeline_callback

  use q65_decode, only: q65_decoder
  implicit none

  integer :: callback_count
  integer :: callback_idec,callback_nused,callback_ntrperiod
  integer :: callback_nutc,callback_nsnr
  integer :: callback_iflagdec
  real :: callback_snr1,callback_dt,callback_freq
  character(len=37) :: callback_message

contains


  subroutine capture_callback(this,nutc,snr1,nsnr,dt,freq,decoded,idec, &
       nused,ntrperiod,iflagdec)
    class(q65_decoder), intent(inout) :: this
    integer, intent(in) :: nutc,nsnr,idec,nused,ntrperiod,iflagdec
    real, intent(in) :: snr1,dt,freq
    character(len=37), intent(in) :: decoded

    callback_count=callback_count+1
    callback_nutc=nutc
    callback_snr1=snr1
    callback_nsnr=nsnr
    callback_dt=dt
    callback_freq=freq
    callback_message=decoded
    callback_idec=idec
    callback_nused=nused
    callback_ntrperiod=ntrperiod
    callback_iflagdec=iflagdec
  end subroutine capture_callback

end module q65_pipeline_callback

program test_q65_decode_pipeline

  use iso_fortran_env, only: int16
  use map65_mmdec_mod, only: map65_mmdec
  use q65_pipeline_callback
  use prog_args, only: data_dir,temp_dir
  use q65_decode, only: q65_decoder,cq0,msg0,nsnr0,nfreq0,xdt0
  use q65_test_fixture, only: make_q65_wave,q65_nsamples,q65_ntrperiod
  use types, only: q3list
  implicit none

  integer(int16), allocatable :: iwave(:)
  integer :: nqf(20),navg0,nsubmode
  logical :: lclearave,single_decode,lagain,lnewdat,lapcqonly
  type(q65_decoder) :: decoder
  character(len=12) :: mycall,hiscall
  character(len=6) :: hisgrid

  data_dir='.'
  temp_dir='.'
  mycall=' '
  hiscall=' '
  hisgrid=' '
  nqf=0
  navg0=0
  lclearave=.true.
  single_decode=.true.
  lagain=.false.
  lnewdat=.false.
  lapcqonly=.false.
  call check_q65_ap_flag_masks()
  allocate(iwave(q65_nsamples))

  do nsubmode=0,4
     call make_q65_wave(iwave,nsubmode)
     call run_direct_decode(decoder,iwave,nsubmode,1,want_iflagdec=0)
  enddo

  iwave=0_int16
  call run_direct_decode(decoder,iwave,0,0)

  do nsubmode=0,4
     call make_q65_wave(iwave,nsubmode)
     call run_map65_decode(iwave,nsubmode,.true.)
  enddo

  iwave=0_int16
  call run_map65_decode(iwave,0,.false.)

  write(*,'(a)') 'Q65 raw decoder and MAP65 handoff tests passed'

contains

  subroutine check_q65_ap_flag_masks()
    integer :: apsym0(58),apmask(78),apsymbols(78),iaptype
    integer :: codewords(63,411),ncw,j
    character(len=12) :: list_mycall,list_hiscall
    character(len=6) :: list_hisgrid
    type(q3list) :: callers(50)

    apsym0=0
    call q65_ap(5,1,0,.false.,.false.,iaptype,apsym0,apmask,apsymbols)
    if(iaptype.ne.3 .or. apmask(78).ne.1 .or. apsymbols(78).ne.0) then
       error stop 'ordinary Q65 AP decoding changed bit 78 policy'
    endif

    call q65_ap(5,1,1,.true.,.false.,iaptype,apsym0,apmask,apsymbols)
    if(iaptype.ne.3 .or. apmask(78).ne.0) then
       error stop 'Q65 Pileup AP decoding fixed bit 78 for call/grid messages'
    endif

    list_mycall='K1ABC'
    list_hiscall='W9XYZ'
    list_hisgrid='FN42'
    do j=1,40
       write(callers(j)%call,'(a2,i4.4)') 'K1',j
       callers(j)%grid='FN42'
    enddo
    call q65_set_list2(list_mycall,list_hiscall,list_hisgrid,callers,40,codewords,ncw)
    if(ncw.ne.411) error stop 'Q65 Pileup full AP list truncated at 40 callers'
  end subroutine check_q65_ap_flag_masks

  subroutine run_direct_decode(decoder,samples,nsubmode,want_callback,want_iflagdec)
    type(q65_decoder), intent(inout) :: decoder
    integer(int16), intent(in) :: samples(:)
    integer, intent(in) :: nsubmode,want_callback
    integer, intent(in), optional :: want_iflagdec
    logical :: want_success

    callback_count=0
    callback_message=' '
    callback_idec=-1
    callback_nused=-1
    callback_ntrperiod=-1
    callback_nutc=-1
    callback_nsnr=-999
    callback_snr1=0.0
    callback_dt=0.0
    callback_freq=0.0
    callback_iflagdec=-1
    want_success=want_callback.ne.0
    call decoder%decode(capture_callback,samples,1,100,q65_ntrperiod,nsubmode, &
         1000,150,3,850,1150,lclearave,single_decode,lagain,0,lnewdat,2.5, &
         mycall,hiscall,hisgrid,0,0,.false.,lapcqonly,navg0,nqf)
    if(want_success) then
       if(callback_count.ne.1) error stop 'direct Q65 decode callback count changed'
       if(trim(callback_message).ne.'K1ABC W9XYZ FN42') then
          error stop 'direct Q65 decoder returned the wrong message'
       endif
       if(callback_idec.lt.0 .or. callback_ntrperiod.ne.q65_ntrperiod) then
          error stop 'direct Q65 callback metadata changed'
       endif
       if(abs(callback_freq-1000.0).gt.8.0) error stop 'direct Q65 frequency changed'
       if(abs(callback_dt).gt.0.15) error stop 'direct Q65 timing changed'
       if(present(want_iflagdec)) then
          if(callback_iflagdec.ne.want_iflagdec) then
             error stop 'direct Q65 decode did not recover the expected flag bit'
          endif
       endif
    else
       if(callback_count.ne.0) error stop 'silent Q65 input produced a callback'
    endif
  end subroutine run_direct_decode

  subroutine run_map65_decode(samples,nsubmode,want_callback)
    integer(int16), intent(in) :: samples(:)
    integer, intent(in) :: nsubmode
    logical, intent(in) :: want_callback
    character(len=37) :: previous_message
    integer :: nutc,nqd,nfa,nfb,nfqso,ntol,newdat,nagain,max_drift,ndepth

    nutc=100
    nqd=1
    nfa=850
    nfb=1150
    nfqso=1000
    ntol=150
    newdat=0
    nagain=1
    max_drift=0
    ndepth=3

    nsnr0=-99
    nfreq0=-1
    xdt0=-99.0
    msg0=' '
    cq0='   '
    previous_message=msg0

    call map65_mmdec(nutc,samples,nqd,q65_ntrperiod,nsubmode,nfa,nfb,nfqso, &
         ntol,newdat,nagain,max_drift,ndepth,mycall,hiscall,hisgrid)
    if(want_callback) then
       if(nsnr0.le.-99) error stop 'MAP65 Q65 handoff missed a decode'
       if(trim(msg0).ne.'K1ABC W9XYZ FN42') then
          error stop 'MAP65 Q65 handoff returned the wrong message'
       endif
       if(cq0(1:2).ne.'q0') error stop 'MAP65 Q65 decode type changed'
       if(abs(nfreq0-1000).gt.8) error stop 'MAP65 Q65 frequency changed'
       if(abs(xdt0).gt.0.15) error stop 'MAP65 Q65 timing changed'
    else
       if(nsnr0.ne.-99 .or. nfreq0.ne.-1 .or. xdt0.ne.-99.0 .or. &
            msg0.ne.previous_message .or. cq0.ne.'   ') then
          error stop 'MAP65 Q65 handoff exposed stale decode state'
       endif
    endif
  end subroutine run_map65_decode


end program test_q65_decode_pipeline
