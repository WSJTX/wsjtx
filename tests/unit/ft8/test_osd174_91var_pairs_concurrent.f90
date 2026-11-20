module osd_pairs_concurrent_controls
  implicit none
  ! Direct global controls avoid libgomp's captured stack environment in TSan tests.
  ! Every atomic in this test is relaxed except the store and load of ndone: the
  ! relaxed ones must not give ThreadSanitizer an ordering between the two
  ! decodes that would hide a race between them.
  integer, save :: nentered = 0
  integer, save :: nexited = 0
  integer, save :: ntimeout = 0
  integer, save :: ndone(2) = 0
  integer, parameter :: N = 174
  real, save :: llrs(N,2)
  integer*1, save :: apmask(N)
  integer*1, save :: cw_par(N,2), msg_par(77,2)
  integer, save :: nh_par(2)
  real, save :: dmin_par(2)
  logical, save :: packed_par(2)
contains
  ! Called by the decoder as a decode enters and leaves its packed pair-table
  ! pass.  Entering returns only once the other thread has entered too, so both
  ! decodes have entered the packed pass before either starts its work in it.
  ! The limit only stops a hang if the other thread never arrives.
  subroutine packed_pass(entering)
    use iso_fortran_env, only: int64
    logical, intent(in) :: entering
    integer :: nseen
    integer(int64) :: t0, t1, rate
    if (.not. entering) then
!$omp atomic update
      nexited = nexited + 1
      return
    endif
    call system_clock(t0, rate)
!$omp atomic update
    nentered = nentered + 1
    do
!$omp atomic read
      nseen = nentered
      if (nseen .ge. 2) exit
      call system_clock(t1)
      if (t1 - t0 .gt. 10_int64*rate) then
!$omp atomic write
        ntimeout = 1
        exit
      endif
    enddo
  end subroutine packed_pass
end module osd_pairs_concurrent_controls

program test_osd174_91var_pairs_concurrent
! Two threads decode at the same time with the packed pair table; each result
! must equal the same decode run alone, and both must have used the table.  The
! generator matrix is built before the threads start, so what is left to race
! is the decoding itself.  Without ThreadSanitizer the comparison can only
! catch a race that changes the result; ThreadSanitizer reports a data race it
! observes in these decodes.
  use iso_fortran_env, only: int64
  use omp_lib
  use decode_completion_module, only: set_packed_pass_receiver
  use osd_pairs_concurrent_controls
  implicit none
  integer, parameter :: NTHREAD = 2
  integer*1 :: cwtrue(N), msg77(77), cw_ser(N,2), msg_ser(77,2), cw(N), m(77), cw2(N), m2(77)
  integer :: nh_ser(2), it, i, nh, nd, nh2, ntry, nflag, nin, nout, nto
  real :: dmin_ser(2), dmin, dmin2, g, u1, u2, sigma, llr(N)
  logical :: pu, chosen
  integer(int64) :: state

  if (omp_get_max_threads() .lt. NTHREAD) error stop 'two OpenMP workers are required'

  state = 20261120_int64
  apmask = 0
! Tries 107 and 109 of a fixed input sequence are inputs for which the pair pass
! improves on the result without it, so that a decode gone wrong in the pair
! table shows; the sequence is generated in full, but only those two are decoded
! (the improvement is asserted, so a change to the generator below fails here).
! Decoding try 1 builds the generator matrix before the threads start.
  it = 0
  do ntry = 1, 109
    do i = 1, 77
      state = modulo(48271_int64*state, 2147483647_int64)
      msg77(i) = int(iand(state/65536, 1_int64), 1)
    enddo
    call encode174_91var(msg77, cwtrue)
    sigma = 5.5
    if (modulo(ntry, 2) .eq. 0) sigma = 8.0
    do i = 1, N
      state = modulo(48271_int64*state, 2147483647_int64)
      u1 = (real(state) + 1.0)/2147483649.0
      state = modulo(48271_int64*state, 2147483647_int64)
      u2 = real(state)/2147483647.0
      g = sqrt(-2.0*log(u1))*cos(6.2831853*u2)
      llr(i) = 6.0*(2.0*cwtrue(i) - 1.0) + sigma*g
    enddo
    chosen = ntry .eq. 107 .or. ntry .eq. 109
    if (ntry .ne. 1 .and. .not. chosen) cycle
    nd = 3
    call osd174_91var_core(llr, apmask, nd, m, cw, nh, dmin, 1, 0, .false., pu)
    if (.not. pu) error stop 'the packed pair table was not used'
    if (.not. chosen) cycle
    nd = 2
    call osd174_91var_core(llr, apmask, nd, m2, cw2, nh2, dmin2, 1, 0, .false., pu)
    if (.not. (dmin .lt. dmin2 .and. nh .gt. 0)) &
         error stop 'the pair pass does not improve this input'
    it = it + 1
    llrs(:,it) = llr
    msg_ser(:,it) = m
    cw_ser(:,it) = cw
    nh_ser(it) = nh
    dmin_ser(it) = dmin
    if (it .eq. NTHREAD) exit
  enddo
  if (it .ne. NTHREAD) error stop 'the two inputs were not generated'

! Together.  Each worker publishes its results with a release store of its own
! completion flag after the decode, and the main thread reads them only after an
! acquire load of that flag.  That is the only ordering the test adds: the
! libgomp join is not visible to ThreadSanitizer, and nothing orders the two
! decodes relative to each other.
  call set_packed_pass_receiver(packed_pass)
!$omp parallel num_threads(NTHREAD) default(shared) private(it, nd, cw, m, nh, dmin, pu)
  if (omp_get_num_threads() .ne. NTHREAD) error stop 'OpenMP did not start two threads'
  it = omp_get_thread_num() + 1
  nd = 3
  call osd174_91var_core(llrs(:,it), apmask, nd, m, cw, nh, dmin, it, 0, .false., pu)
  msg_par(:,it) = m
  cw_par(:,it) = cw
  nh_par(it) = nh
  dmin_par(it) = dmin
  packed_par(it) = pu
!$omp atomic write release
  ndone(it) = 1
!$omp end parallel

  do it = 1, NTHREAD
!$omp atomic read acquire
    nflag = ndone(it)
    if (nflag .ne. 1) error stop 'a worker did not finish'
  enddo
!$omp atomic read
  nin = nentered
!$omp atomic read
  nout = nexited
!$omp atomic read
  nto = ntimeout
  if (nto .ne. 0) error stop 'the other decode never reached the packed pass'
  if (nin .ne. NTHREAD .or. nout .ne. NTHREAD) &
       error stop 'both decodes must enter and leave the packed pass'
  do it = 1, NTHREAD
    if (.not. packed_par(it)) error stop 'a thread did not use the packed pair table'
    if (any(cw_par(:,it) .ne. cw_ser(:,it)) .or. any(msg_par(:,it) .ne. msg_ser(:,it)) &
         .or. nh_par(it) .ne. nh_ser(it) &
         .or. transfer(dmin_par(it), 1) .ne. transfer(dmin_ser(it), 1)) &
         error stop 'concurrent decode differs from the decode run alone'
  enddo
  print *, 'test_osd174_91var_pairs_concurrent passed'
end program
