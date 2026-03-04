subroutine jt9a()
  use, intrinsic :: iso_c_binding, only: c_f_pointer, c_null_char, c_bool
  use prog_args
  use timer_module, only: timer
  use timer_impl, only: init_timer !, limtrace
  use shmem
  use ft8_mod1, only : dd8
  use jt65_mod6, only : dd

  include 'jt9com.f90'

  integer*2 id2a(180000)
! Multiple instances:
  type(dec_data), pointer, volatile :: shared_data !also makes target volatile
  type(params_block) :: local_params
  logical(c_bool) :: ok

  call init_timer (trim(data_dir)//'/timer.out')
!  open(23,file=trim(data_dir)//'/CALL3.TXT',status='unknown')

!  limtrace=-1                            !Disable all calls to timer()

! Multiple instances: set the shared memory key before attaching
  call shmem_setkey(trim(shm_key)//c_null_char)
  ok=shmem_attach()
  if(.not.ok) call abort
  msdelay=10
  call c_f_pointer(shmem_address(),shared_data)

! Terminate if ipc(2) is 999
10 ok=shmem_lock()
  if(.not.ok) call abort
  if(shared_data%ipc(2).eq.999.0) then
     ok=shmem_unlock()
     ok=shmem_detach()
     go to 999
  endif! Wait here until GUI has set ipc(2) to 1
  if(shared_data%ipc(2).ne.1) then
     ok=shmem_unlock()
     if(.not.ok) call abort
     call sleep_msec(msdelay)
     go to 10
  endif
  shared_data%ipc(2)=0
  nbytes=shmem_size()
  if(nbytes.le.0) then
     ok=shmem_unlock()
     ok=shmem_detach()
     print*,'jt9a: Shared memory does not exist.'
     print*,"Must start 'jt9 -s <thekey>' from within WSJT-X."
     go to 999
  endif
  local_params=shared_data%params !save a copy because wsjtx carries on accessing  
  ok=shmem_unlock()
  if(.not.ok) call abort
  call flush(6)
  call timer('decoder ',0)
  if(local_params%nmode.eq.8 .and. local_params%ndiskdat .and.    &
       .not. local_params%nagain .and.  .not. local_params%lmultift8) then
! Early decoding pass, FT8 only, when wsjtx reads from disk
     nearly=41
     local_params%nzhsym=nearly
     id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
     id2a(nearly*3456+1:)=0
     call multimode_decoder(shared_data%ss,id2a,local_params,12000)
     nearly=47
     local_params%nzhsym=nearly
     id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
     id2a(nearly*3456+1:)=0
     call multimode_decoder(shared_data%ss,id2a,local_params,12000)
     local_params%nzhsym=50
  endif
  
  !ft8md
  if(local_params%nmode.eq.8 .and. local_params%lmultift8 .and.   &
       .not. local_params%nagain .and. local_params%ndiskdat) then 
     npts1=180000
     if(local_params%ndecoderstart.lt.2) then
        nearly=41
        local_params%lmultift8=.false.
        local_params%nzhsym=nearly
        id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
        id2a(nearly*3456+1:)=0
        call multimode_decoder(shared_data%ss,id2a,local_params,12000)
        if(local_params%ndecoderstart.lt.2) then
           nearly=46
           local_params%lmultift8=.false.
           local_params%nzhsym=nearly
           id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
           id2a(nearly*3456+1:)=0
           call multimode_decoder(shared_data%ss,id2a,local_params,12000)
        endif
        if(local_params%ndecoderstart.eq.0) nearly=49
        if(local_params%ndecoderstart.eq.1) nearly=50
        local_params%lmultift8=.true.
        shared_data%params%nzhsym=nearly
        id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
        id2a(nearly*3456+1:)=0
        dd(1:nearly*3456)=shared_data%id2(1:nearly*3456)
        dd(nearly*3456+1:)=0
        dd8(1:nearly*3456)=shared_data%id2(1:nearly*3456)
        dd8(nearly*3456+1:)=0
        if(local_params%ndecoderstart.eq.0) shared_data%params%nzhsym=49
        if(local_params%ndecoderstart.eq.1) shared_data%params%nzhsym=50
     else
        nearly=50
        if(local_params%ndecoderstart.eq.2) nearly=48
        if(local_params%ndecoderstart.eq.3) nearly=49
        if(local_params%ndecoderstart.eq.4) nearly=50
        shared_data%params%nzhsym=nearly
        id2a(1:nearly*3456)=shared_data%id2(1:nearly*3456)
        id2a(nearly*3456+1:)=0
        dd(1:nearly*3456)=shared_data%id2(1:nearly*3456)
        dd(nearly*3456+1:)=0
        dd8(1:nearly*3456)=shared_data%id2(1:nearly*3456)
        dd8(nearly*3456+1:)=0
        if(local_params%ndecoderstart.eq.2) shared_data%params%nzhsym=48
        if(local_params%ndecoderstart.eq.3) shared_data%params%nzhsym=49
        if(local_params%ndecoderstart.eq.4) shared_data%params%nzhsym=50
     endif
  elseif (local_params%nmode.eq.8 .and. local_params%lmultift8 .and. .not. &
       local_params%ndiskdat) then
     dd(1:npts1)=shared_data%id2(1:npts1)
     rms=sum(abs(dd(1:10))) + sum(abs(dd(76001:76010))) + sum(abs(dd(151670:151680)))
     dd8(1:npts1)=dd(1:npts1)

!### WHY WAS THIS STUFF HERE ??? ###
!     if(rms.gt.0.001) then
!        dd(1:npts1)=shared_data%dd2(1:npts1)
!        dd8(1:npts1)=dd(1:npts1)
!print *,'win7',rms
!     else ! workaround for zero data values of dd2 array under WinXP
!        dd(1:npts1)=shared_data%id2(1:npts1)
!        dd8(1:npts1)=dd(1:npts1)
!print *, 'winxp',rms
!     endif
!###
  endif
  !end ft8md

  if(local_params%nmode .eq. 144) then
    ! MSK144
     call decode_msk144(shared_data%id2, shared_data%params, data_dir)
  else
    ! Normal decoding pass
     call multimode_decoder(shared_data%ss,shared_data%id2,local_params,12000)
  endif

  call timer('decoder ',1)

!print*,time, ' jt9a before final loop which waits for ipc(3) ==1' !ft8md
! Wait here until GUI routine decodeDone() has set ipc(3) to 1
100 ok=shmem_lock()
  if(.not.ok) call abort
  if(shared_data%ipc(3).ne.1) then
     ok=shmem_unlock()
     if(.not.ok) call abort
     call sleep_msec(msdelay)
     go to 100
  endif
  shared_data%ipc(3)=0
  ok=shmem_unlock()
  if(.not.ok) call abort
  go to 10
  
999 call timer('decoder ',101)

  return
end subroutine jt9a
