subroutine jt9a()
  use, intrinsic :: iso_c_binding, only: c_f_pointer, c_null_char, c_bool
  use prog_args
  use timer_module, only: timer
  use timer_impl, only: init_timer !, limtrace
  use shmem

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
  msdelay=30
  call c_f_pointer(shmem_address(),shared_data)

! Terminate if ipc(2) is 999
10 ok=shmem_lock()
  if(.not.ok) call abort
  if(shared_data%ipc(2).eq.999.0) then
     ok=shmem_unlock()
     ok=shmem_detach()
     go to 999
  endif
! Wait here until GUI has set ipc(2) to 1
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
  if(local_params%nmode.eq.8 .and. local_params%ndiskdat) then
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
! Normal decoding pass
  call multimode_decoder(shared_data%ss,shared_data%id2,local_params,12000)
  call timer('decoder ',1)


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
