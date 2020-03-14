subroutine jt9a()
  use, intrinsic :: iso_c_binding, only: c_f_pointer
  use prog_args
  use timer_module, only: timer
  use timer_impl, only: init_timer !, limtrace

  include 'jt9com.f90'

! These routines connect the shared memory region to the decoder.
  interface
     function address_jt9()
       use, intrinsic :: iso_c_binding, only: c_ptr
       type(c_ptr) :: address_jt9
     end function address_jt9
  end interface

  integer*2 id2a(180000)
  integer*1 attach_jt9
!  integer*1 lock_jt9,unlock_jt9
  integer size_jt9
! Multiple instances:
  character*80 mykey
  type(dec_data), pointer :: shared_data
  type(params_block) :: local_params
  volatile shared_data

! Multiple instances:
  i0 = len(trim(shm_key))

  call init_timer (trim(data_dir)//'/timer.out')
!  open(23,file=trim(data_dir)//'/CALL3.TXT',status='unknown')

!  limtrace=-1                            !Disable all calls to timer()

! Multiple instances: set the shared memory key before attaching
  mykey=trim(repeat(shm_key,1))
  i0 = len(mykey)
  i0=setkey_jt9(trim(mykey))
  i1=attach_jt9()
  msdelay=30
  call c_f_pointer(address_jt9(),shared_data)

! Wait here until GUI has set ss(2,1) to 1.0
10 if(shared_data%ss(2,1).eq.999.0) then
     i1=detach_jt9()
     go to 999
  endif
   if(shared_data%ss(2,1).ne.1.0) then
     call sleep_msec(msdelay)
     go to 10
  endif
  shared_data%ss(2,1)=0.0

  nbytes=size_jt9()
  if(nbytes.le.0) then
     print*,'jt9a: Shared memory mem_jt9 does not exist.'
     print*,"Must start 'jt9 -s <thekey>' from within WSJT-X."
     go to 999
  endif
  local_params=shared_data%params !save a copy because wsjtx carries on accessing  
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


! Wait here until GUI routine decodeDone() has set ss(3,1) to 1.0
100  if(shared_data%ss(3,1).ne.1.0) then
     call sleep_msec(msdelay)
     go to 100
  endif
  shared_data%ss(3,1)=0.
  go to 10
  
999 call timer('decoder ',101)

  return
end subroutine jt9a
