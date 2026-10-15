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
  use q65_callers, only: Q65_MAX_CALLERS,Q65_MAX_CODEWORDS
  implicit none

  integer(int16), allocatable :: iwave(:)
  integer, parameter :: map65_submodes(2)=[0,4]
  integer :: nqf(20),navg0,nsubmode,isubmode
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
  call check_q65_rr73_ap_payload()
  allocate(iwave(q65_nsamples))

  do nsubmode=0,4
     call make_q65_wave(iwave,nsubmode)
     call run_direct_decode(decoder,iwave,nsubmode,1,want_iflagdec=0)
  enddo

  iwave=0_int16
  call run_direct_decode(decoder,iwave,0,0,ntrperiod=30)
  if(decoder%state%jz0.ne.(30*12000-30*120)/(30*120/8)+1) then
     error stop 'Q65 spectra do not cover every complete input window'
  endif

  call check_q65_drift_compensation()

  ! The direct decoder loop owns exhaustive submode coverage. MAP65 is a thin
  ! handoff, so exercise both ends of its forwarded submode range.
  do isubmode=1,size(map65_submodes)
     nsubmode=map65_submodes(isubmode)
     call make_q65_wave(iwave,nsubmode)
     call run_map65_decode(iwave,nsubmode,.true.)
  enddo

  ! The wrapper's silent case also proves that the decoder emits no callback,
  ! while additionally checking that no stale MAP65 state escapes.
  iwave=0_int16
  call run_map65_decode(iwave,0,.false.)

  call decoder%destroy()
  deallocate(iwave)
  write(*,'(a)') 'Q65 raw decoder and MAP65 handoff tests passed'

contains

  subroutine check_q65_drift_compensation()
    use q65, only: q65_dec0,q65_select,q65_work,q65_state, &
         s1a,s1w,iseq,iz0,j0,NSTEP,df,ncw,max_drift,drift
    type(q65_state), pointer :: previous_state
    integer, parameter :: drift_cases(3)=[-50,0,50]
    integer :: case_number,want_drift,k,i,j,peak_min,peak_max,dat4(13),idec
    real :: xdt,f0,snr1,width,snr2

    iwave=0_int16
    call run_direct_decode(decoder,iwave,0,0)
    previous_state=>q65_work
    call q65_select(decoder%state)
    ncw=0
    max_drift=50
    do case_number=1,size(drift_cases)
       want_drift=drift_cases(case_number)
       s1a(:,:,iseq)=0.0
       do k=1,85
          j=j0+NSTEP*(k-1)+1
          i=nint(1000.0/df)+nint(real(want_drift)*(k-43)/85.0)
          s1a(i,j,iseq)=100.0
       enddo
       if(associated(s1w)) deallocate(s1w)
       nullify(q65_work%s1w)
       ! Averaged spectra without list candidates reach stage 5 without an earlier decode.
       call q65_dec0(1,iwave,q65_ntrperiod,1000,20,lclearave,2.5, &
            xdt,f0,snr1,width,dat4,snr2,idec,5)
       ! Integer bin rounding makes zero drift indistinguishable from a one-bin sweep.
       if(abs(nint(drift/df)-want_drift).gt.1) then
          error stop 'Q65 synchronization missed the planted drift'
       endif
       if(.not.associated(s1w)) error stop 'Q65 stage-5 drift compensation did not run'
       peak_min=iz0
       peak_max=1
       do k=1,85
          j=j0+NSTEP*(k-1)+1
          if(maxval(s1w(:,j)).lt.99.0) error stop 'Q65 drift compensation lost a symbol'
          i=maxloc(s1w(:,j),dim=1)
          peak_min=min(peak_min,i)
          peak_max=max(peak_max,i)
       enddo
       if(peak_max-peak_min.gt.1) then
          error stop 'Q65 drift compensation depends on the receive horizon'
       endif
    enddo
    call q65_select(previous_state)
    lclearave=.true.
  end subroutine check_q65_drift_compensation

  subroutine check_q65_rr73_ap_payload()
    use packjt77, only: pack77_state,pack77_for_state
    type(pack77_state) :: knowledge
    character(len=37) :: message
    character(len=77) :: packed
    integer :: bits(77),apmask(78),apsymbols(78),iaptype,i3,n3

    message='K1ABC W9XYZ RR73'
    i3=-1
    n3=-1
    call pack77_for_state(knowledge,message,i3,n3,packed)
    if(i3.ne.1) error stop 'Q65 RR73 AP fixture is not a standard message'
    read(packed,'(77i1)') bits
    call q65_ap(3,4,0,.false.,.false.,iaptype,bits(1:58),apmask,apsymbols)
    if(iaptype.ne.6) error stop 'Q65 RR73 AP pass was not selected'
    if(any(apmask(1:71).ne.1).or.any(apmask(72:74).ne.0).or.any(apmask(75:78).ne.1)) then
       error stop 'Q65 RR73 AP mask changed'
    endif
    if(any(apmask(1:77).eq.1.and.apsymbols(1:77).ne.bits)) then
       error stop 'Q65 RR73 AP hypothesis disagrees with the packed payload'
    endif
    if(apsymbols(78).ne.0) error stop 'Q65 RR73 AP changed bit 78 policy'
  end subroutine check_q65_rr73_ap_payload

  subroutine check_q65_ap_flag_masks()
    integer :: apsym0(58),apmask(78),apsymbols(78),iaptype
    integer :: codewords(63,Q65_MAX_CODEWORDS),ncw,j
    character(len=12) :: list_mycall,list_hiscall
    character(len=6) :: list_hisgrid
    type(q3list) :: callers(Q65_MAX_CALLERS)

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
    do j=1,Q65_MAX_CALLERS
       callers(j)%call='K1'//achar(65+(j-1)/26)//achar(65+mod(j-1,26))//'A'
       callers(j)%grid='FN42'
    enddo
    call q65_set_list2(list_mycall,list_hiscall,list_hisgrid,callers, &
         Q65_MAX_CALLERS,codewords,ncw)
    if(ncw.ne.511) error stop 'Q65 Pileup full AP list omitted a stored or current caller'
    if(any(codewords(:,1).ne.0)) error stop 'Q65 Pileup all-zero candidate changed'
    call check_caller_codewords(codewords(:,ncw-19:ncw-10),list_mycall, &
         callers(Q65_MAX_CALLERS)%call,callers(Q65_MAX_CALLERS)%grid)
    call check_caller_codewords(codewords(:,ncw-9:ncw),list_mycall, &
         list_hiscall,list_hisgrid)

    list_hiscall=callers(Q65_MAX_CALLERS)%call
    call q65_set_list2(list_mycall,list_hiscall,list_hisgrid,callers, &
         Q65_MAX_CALLERS,codewords,ncw)
    if(ncw.ne.501) error stop 'Q65 Pileup duplicated a stored current caller'
    call check_caller_codewords(codewords(:,ncw-9:ncw),list_mycall, &
         callers(Q65_MAX_CALLERS)%call,callers(Q65_MAX_CALLERS)%grid)

    list_hiscall=' '
    call q65_set_list2(list_mycall,list_hiscall,list_hisgrid,callers, &
         Q65_MAX_CALLERS,codewords,ncw)
    if(ncw.ne.501) error stop 'Q65 Pileup added an invalid current caller'
    call check_caller_codewords(codewords(:,ncw-9:ncw),list_mycall, &
         callers(Q65_MAX_CALLERS)%call,callers(Q65_MAX_CALLERS)%grid)

    call q65_set_list2(list_mycall,list_hiscall,list_hisgrid,callers,-1,codewords,ncw)
    if(ncw.ne.0) error stop 'Q65 Pileup accepted a negative caller count'
    call q65_set_list2(list_mycall,list_hiscall,list_hisgrid,callers, &
         Q65_MAX_CALLERS+1,codewords,ncw)
    if(ncw.ne.0) error stop 'Q65 Pileup accepted an oversized caller count'
  end subroutine check_q65_ap_flag_masks

  subroutine check_caller_codewords(codewords,mycall,dxcall,grid)
    integer, intent(in) :: codewords(63,10)
    character(len=*), intent(in) :: mycall,dxcall,grid
    integer, parameter :: sync_positions(22)=[ &
         1,9,12,13,15,22,23,26,27,33,35,38,46,50,55,60,62,66,69,74,76,85]
    character(len=6) :: tails(5)
    character(len=37) :: message,msgsent
    integer :: tones(85),expected(63),kind,flag,i3,n3,j,k,column

    tails=[character(len=6) :: grid(1:4),'R '//grid(1:4),'RRR','RR73','73']
    do kind=1,size(tails)
       message=trim(mycall)//' '//trim(dxcall)//' '//trim(tails(kind))
       do flag=0,1
          call genq65(message,0,msgsent,tones,i3,n3,flag)
          if(msgsent.ne.message) error stop 'Q65 Pileup test message failed to encode'
          j=0
          do k=1,size(tones)
             if(any(sync_positions.eq.k)) cycle
             j=j+1
             expected(j)=tones(k)-1
          enddo
          column=2*(kind-1)+flag+1
          if(any(codewords(:,column).ne.expected)) then
             error stop 'Q65 Pileup lost a message form or flag at the caller boundary'
          endif
       enddo
    enddo
  end subroutine check_caller_codewords

  subroutine run_direct_decode(decoder,samples,nsubmode,want_callback,want_iflagdec,ntrperiod)
    type(q65_decoder), intent(inout) :: decoder
    integer(int16), intent(in) :: samples(:)
    integer, intent(in) :: nsubmode,want_callback
    integer, intent(in), optional :: want_iflagdec,ntrperiod
    integer :: decode_period
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
    decode_period=q65_ntrperiod
    if(present(ntrperiod)) decode_period=ntrperiod
    call decoder%decode(capture_callback,samples,1,100,decode_period,nsubmode, &
         1000,150,3,850,1150,lclearave,single_decode,lagain,0,lnewdat,2.5, &
         mycall,hiscall,hisgrid,0,0,.false.,lapcqonly,navg0,nqf)
    if(want_success) then
       if(callback_count.ne.1) error stop 'direct Q65 decode callback count changed'
       if(trim(callback_message).ne.'K1ABC W9XYZ FN42') then
          error stop 'direct Q65 decoder returned the wrong message'
       endif
       if(callback_idec.lt.0 .or. callback_ntrperiod.ne.decode_period) then
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
