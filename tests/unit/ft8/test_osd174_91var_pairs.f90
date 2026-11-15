program test_osd174_91var_pairs
! Compares osd174_91var, which uses a packed pair table for ndeep 3, with the
! same decoder using the original pair table (boxit91var, fetchit91var).  Every
! output must be identical, the floating-point ones to the last bit.  Most cases
! use ndeep 3; every tenth uses ndeep 1 to 5, and ndeep 5 (ntau 19) takes the
! original table in both.  The packed table must be used for ndeep 3 and for no
! other ndeep.
!
! usage: test_osd174_91var_pairs [ncases [ties]]   (default 3000)
! "ties": every input is hard decisions only, so many candidates tie exactly and
! the order in which the search visits them decides the result.
  use iso_fortran_env, only: int64
  implicit none
  integer, parameter :: N=174
  integer :: ncases,nd,ic,i,nargs,nfail,nimproved,nok,iap,nblk
  integer(int64) :: state
  integer, allocatable :: ndeeps(:)
  real, allocatable :: llrs(:,:)
  integer*1, allocatable :: aps(:,:)
  real :: llr(N),dmin1,dmin2,dmin3,dmin4,sigma,amp,u1,u2,g,lvl,apmag
  integer*1 :: apmask(N),cw1(N),cw2(N),m1(77),m2(77),msg77(77),cwtrue(N),cw3(N),m3(77),cw4(N),m4(77)
  integer :: nh1,nh2,nh3,nh4
  character(len=32) :: arg
  logical :: bad,tiesonly,pu1,pu3,pu4

  ncases=3000
  nargs=command_argument_count()
  if(nargs.ge.1) then; call get_command_argument(1,arg); read(arg,*) ncases; endif
  tiesonly=.false.
  if(nargs.ge.2) then; call get_command_argument(2,arg); tiesonly=(trim(arg).eq.'ties'); endif
  allocate(llrs(N,ncases),aps(N,ncases),ndeeps(ncases))
  state=20261114_int64

  do ic=1,ncases
    ! random message, true codeword, noisy LLRs
    do i=1,77
      state=modulo(48271_int64*state,2147483647_int64)
      msg77(i)=int(iand(state/65536,1_int64),1)
    enddo
    call encode174_91var(msg77,cwtrue)
    state=modulo(48271_int64*state,2147483647_int64)
    select case(int(modulo(state/256,6_int64)))
    case(0); sigma=2.0
    case(1); sigma=3.0
    case(2); sigma=4.0
    case(3); sigma=5.5
    case(4); sigma=8.0
    case default; sigma=1000.0          ! pure noise: nothing decodes
    end select
    amp=3.0
    if(sigma.gt.100.0) amp=0.0
    do i=1,N
      state=modulo(48271_int64*state,2147483647_int64)
      u1=(real(state)+1.0)/2147483649.0
      state=modulo(48271_int64*state,2147483647_int64)
      u2=real(state)/2147483647.0
      g=sqrt(-2.0*log(u1))*cos(6.2831853*u2)
      if(sigma.gt.100.0) then
        llrs(i,ic)=4.0*g
      else
        llrs(i,ic)=amp*(2.0*cwtrue(i)-1.0)*2.0+sigma*g
      endif
    enddo
    ! occasional exact ties and zeros in the received values
    state=modulo(48271_int64*state,2147483647_int64)
    if(modulo(state/1024,7_int64).eq.0) then
      do i=1,N,9
        llrs(i,ic)=0.0
      enddo
    endif
    if(modulo(state/4096,5_int64).eq.0) then
      do i=2,N,11
        llrs(i,ic)=sign(2.0,llrs(i,ic))
      enddo
    endif
    ! coarsely quantized values: many candidate codewords end up at exactly the
    ! same distance, so the order in which the search visits them decides
    if(modulo(state/8192,3_int64).eq.0) then
      lvl=2.0+2.0*real(modulo(state/65536,2_int64))
      do i=1,N
        llrs(i,ic)=real(nint(max(-lvl,min(lvl,llrs(i,ic)))))
      enddo
    endif
    ! hard decisions only: every candidate's distance is a whole number
    if(modulo(state/131072,6_int64).eq.0) then
      do i=1,N
        llrs(i,ic)=sign(1.0,llrs(i,ic))
      enddo
    endif
    if(tiesonly) then
      do i=1,N
        llrs(i,ic)=sign(1.0,llrs(i,ic))
      enddo
    endif
    ! a-priori mask on bits set to the known value, at a magnitude from weak to
    ! far above the data, or on a leading block of message bits as the FT8 AP
    ! decoder sets it
    aps(:,ic)=0
    state=modulo(48271_int64*state,2147483647_int64)
    iap=int(modulo(state/512,4_int64))
    if(iap.eq.1) then
      nblk=58
      if(modulo(state/2048,2_int64).eq.0) nblk=77
      apmag=1.01*maxval(abs(llrs(:,ic)))
      do i=1,nblk
        aps(i,ic)=1
        llrs(i,ic)=apmag*(2.0*cwtrue(i)-1.0)
      enddo
    endif
    if(iap.eq.0) then
      select case(int(modulo(state/2048,3_int64)))
      case(0); apmag=3.0
      case(1); apmag=8.0
      case default; apmag=50.0
      end select
      do i=1,N
        state=modulo(48271_int64*state,2147483647_int64)
        if(modulo(state/128,9_int64).eq.0) then
          aps(i,ic)=1
          llrs(i,ic)=apmag*(2.0*cwtrue(i)-1.0)
        endif
      enddo
    endif
    ndeeps(ic)=3
    if(modulo(ic,10).eq.0) ndeeps(ic)=1+modulo(ic/10,5)
    if(tiesonly) ndeeps(ic)=3
  enddo

  nfail=0; nimproved=0; nok=0
  do ic=1,ncases
    llr=llrs(:,ic); apmask=aps(:,ic)
    nd=ndeeps(ic)
    call osd174_91var_core(llr,apmask,nd,m1,cw1,nh1,dmin1,1,0,.true.,pu1)
    nd=ndeeps(ic)
    call osd174_91var(llr,apmask,nd,m2,cw2,nh2,dmin2,1,0)
    nd=ndeeps(ic)
    call osd174_91var_core(llr,apmask,nd,m3,cw3,nh3,dmin3,1,0,.false.,pu3)   ! as the wrapper does
    bad = any(cw1.ne.cw2) .or. any(m1.ne.m2) .or. nh1.ne.nh2 .or. &
          transfer(dmin1,1).ne.transfer(dmin2,1) .or. &
          any(cw1.ne.cw3) .or. any(m1.ne.m3) .or. nh1.ne.nh3 .or. &
          transfer(dmin1,1).ne.transfer(dmin3,1)
    ! the packed table is used exactly for ndeep 3, and never when it is not asked for
    bad = bad .or. pu1 .or. (pu3 .neqv. (ndeeps(ic).eq.3))
    if(ndeeps(ic).eq.3) then
      nd=2
      call osd174_91var_core(llr,apmask,nd,m4,cw4,nh4,dmin4,1,0,.true.,pu4)
      if(dmin1.lt.dmin4) nimproved=nimproved+1
    endif
    if(nh1.gt.0) nok=nok+1
    if(bad) then
      nfail=nfail+1
      if(nfail.le.5) print '(a,i6,a,i2,a,3i5,3es15.7)','MISMATCH case',ic,' ndeep',ndeeps(ic),' nhard',nh1,nh2,nh3,dmin1,dmin2,dmin3
    endif
  enddo
  print '(a,i7,a,i6,a,i6)','cases',ncases,'  pair pass improved over ndeep 2:',nimproved,'  CRC ok, nhard>0:',nok

  if(nfail.ne.0) error stop 'test_osd174_91var_pairs failed'
! The comparison means little if the pair pass never changes the result.
  if(nimproved.lt.ncases/50 .or. nok.lt.ncases/10) error stop 'test inputs do not exercise the pair pass'
  print *,'test_osd174_91var_pairs passed'
end program
