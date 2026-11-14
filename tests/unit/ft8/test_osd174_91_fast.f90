program test_osd174_91_fast
! osd174_91_fast (bit-packed, ndeep=1,2) and the osd174_91 dispatcher against
! osd174_91_slow, the byte-array decoder.  Every output must be identical, the
! floating-point ones to the last bit.  Both decoders are called in each process
! with the same k, because osd174_91_slow builds its generator matrix once.
!
! usage: test_osd174_91_fast [ncases [k [ties]]]   (defaults 3000 and 91)
! "ties": every input is hard decisions only, at ndeep=2, so many candidates tie
! exactly and the order in which the search visits them decides the result.
  use iso_fortran_env, only: int64
  use, intrinsic :: ieee_arithmetic, only: ieee_value,ieee_positive_inf,ieee_quiet_nan
  implicit none
  integer, parameter :: N=174
  integer :: ncases,k,nd,ic,i,nargs,nfail,nimproved,nok,iap,nblk
  integer(int64) :: state
  integer, allocatable :: ndeeps(:)
  real, allocatable :: llrs(:,:)
  integer*1, allocatable :: aps(:,:)
  real :: llr(N),dmin1,dmin2,dmin3,dmin0,sigma,amp,u1,u2,g,lvl,apmag
  integer*1 :: apmask(N),cw1(N),cw2(N),cw3(N),m1(91),m2(91),m3(91),msg77(77),cwtrue(N),dummy(N),m0(91)
  integer :: nh1,nh2,nh3,nh0
  character(len=32) :: arg
  logical :: bad,tiesonly

  ncases=3000
  k=91
  nargs=command_argument_count()
  if(nargs.ge.1) then; call get_command_argument(1,arg); read(arg,*) ncases; endif
  if(nargs.ge.2) then; call get_command_argument(2,arg); read(arg,*) k; endif
  tiesonly=.false.
  if(nargs.ge.3) then; call get_command_argument(3,arg); tiesonly=(trim(arg).eq.'ties'); endif
  allocate(llrs(N,ncases),aps(N,ncases),ndeeps(ncases))
  state=20261113_int64

  do ic=1,ncases
    ! random message, true codeword, noisy LLRs
    do i=1,77
      state=modulo(48271_int64*state,2147483647_int64)
      msg77(i)=int(iand(state/65536,1_int64),1)
    enddo
    call encode174_91(msg77,cwtrue)
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
    ! a-priori mask on bits that are set to the known value, at a magnitude from
    ! weak to far above the data (a skipped flip of a weak AP bit can still win),
    ! or on a leading block of message bits as the FT8 AP decoder sets it
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
    ndeeps(ic)=1+mod(ic,2)
    if(tiesonly) ndeeps(ic)=2
  enddo

  nfail=0; nimproved=0; nok=0
  do ic=1,ncases
    llr=llrs(:,ic); apmask=aps(:,ic); nd=ndeeps(ic)
    call osd174_91_slow(llr,k,apmask,nd,m1,cw1,nh1,dmin1)
    call osd174_91(llr,k,apmask,nd,m2,cw2,nh2,dmin2)
    call osd174_91_fast(llr,k,apmask,nd,m3,cw3,nh3,dmin3)
    call osd174_91_slow(llr,k,apmask,0,m0,dummy,nh0,dmin0)
    bad = any(cw1.ne.cw2) .or. any(m1.ne.m2) .or. nh1.ne.nh2 .or. &
          transfer(dmin1,1).ne.transfer(dmin2,1) .or. &
          any(cw1.ne.cw3) .or. any(m1.ne.m3) .or. nh1.ne.nh3 .or. &
          transfer(dmin1,1).ne.transfer(dmin3,1)
    if(dmin1.lt.dmin0) nimproved=nimproved+1
    if(nh1.gt.0) nok=nok+1
    if(bad) then
      nfail=nfail+1
      if(nfail.le.5) print '(a,i6,a,i2,a,i5,i5,2es15.7)','MISMATCH case',ic,' ndeep',nd,' nhard',nh1,nh2,dmin1,dmin2
    endif
  enddo
  print '(a,i7,a,i3,a,i6,a,i6)','cases',ncases,'  k',k,'  improved over order 0:',nimproved,'  CRC ok, nhard>0:',nok

! A NaN or infinite value must give the same result as osd174_91_slow,
! which the dispatcher guarantees by not taking the fast path.
  do ic=1,min(ncases,200)
    llr=llrs(:,ic); apmask=aps(:,ic); nd=ndeeps(ic)
    if(mod(ic,2).eq.0) then
      llr(1+mod(ic*7,N))=ieee_value(1.0,ieee_positive_inf)
    else
      llr(1+mod(ic*7,N))=ieee_value(1.0,ieee_quiet_nan)
    endif
    call osd174_91_slow(llr,k,apmask,nd,m1,cw1,nh1,dmin1)
    call osd174_91(llr,k,apmask,nd,m2,cw2,nh2,dmin2)
    if(any(cw1.ne.cw2) .or. any(m1.ne.m2) .or. nh1.ne.nh2) then
      print '(a,i6)','MISMATCH with a non-finite input, case',ic
      nfail=nfail+1
    endif
  enddo

  if(nfail.ne.0) error stop 'test_osd174_91_fast failed'
! The comparison means little if the inputs never make the search find anything.
  if(nimproved.lt.ncases/10 .or. nok.lt.ncases/10) error stop 'test inputs do not exercise the OSD search'
  print *,'test_osd174_91_fast passed'
end program
