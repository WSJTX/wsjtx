program synctest

  ! Program to test an algorithm for detecting sync signals for both
  ! JT65 and Q65-60x signals and rejecting birdies in MAP65 data.
  ! The important work is done in module wideband_sync.

  use timer_module, only: timer
  use timer_impl, only: init_timer, fini_timer
  use wideband_sync

  real ss(4,322,NFFT),savg(4,NFFT)
!  real candidate(MAX_CANDIDATES,5)             !snr1,f0,xdt0,ipol,flip
  character*8 arg
  type(candidate) :: cand(MAX_CANDIDATES)
  
  nargs=iargc()
  if(nargs.ne.5) then
     print*,'Usage:   synctest iutc nfa nfb nts_jt65 nts_q65'
     print*,'Example: synctest 1814  23  83      2        1'
     go to 999
  endif
  call getarg(1,arg)
  read (arg,*) iutc
  call getarg(2,arg)
  read (arg,*) nfa
  call getarg(3,arg)
  read (arg,*) nfb
  call getarg(4,arg)
  read (arg,*) nts_jt65
  call getarg(5,arg)
  read (arg,*) nts_q65

  open(50,file='50.a',form='unformatted',status='old')
  do ifile=1,9999
     read(50,end=998) nutc,npol,ss(1:npol,:,:),savg(1:npol,:)
     if(nutc.eq.iutc) exit
  enddo
  close(50)

  call init_timer('timer.out')
  call timer('synctest',0)

  call timer('get_cand',0)
  call  get_candidates(ss,savg,302,.true.,nfa,nfb,nts_jt65,nts_q65,cand,ncand)
  call timer('get_cand',1)

  do k=1,ncand
     write(*,1010) k,cand(k)%snr,cand(k)%f,cand(k)%f+77,cand(k)%xdt,    &
          cand(k)%ipol,cand(k)%iflip
1010 format(i3,4f10.3,2i3)
  enddo

998 call timer('synctest',1)
  call timer('synctest',101)
  call fini_timer()

999 end program synctest
