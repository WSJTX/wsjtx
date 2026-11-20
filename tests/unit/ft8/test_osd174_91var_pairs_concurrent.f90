module osd_pairs_concurrent_controls
  implicit none
  ! Direct global controls avoid libgomp's captured stack environment in TSan tests.
  ! The atomics below are relaxed on purpose: they must not give ThreadSanitizer an
  ! ordering between the two decodes that would hide a race between them.
  integer, save :: nreported = 0
  logical, save :: seen(2) = .false.
  integer, save :: ntimeout = 0
  integer, parameter :: N = 174
  real, save :: llrs(N,2)
  integer*1, save :: apmask(N)
  integer*1, save :: cw_par(N,2), msg_par(77,2)
  integer, save :: nh_par(2)
  real, save :: dmin_par(2)
  logical, save :: packed_par(2)
contains
  ! Called from inside the decoder on a thread's progress reports; generation
  ! is the thread's number.  A thread's first report returns only once the other
  ! thread has also reported, so both threads are inside the decoder together.
  ! The limit only stops a hang if a report never comes.
  subroutine rendezvous(generation)
    use iso_fortran_env, only: int64
    integer, intent(in) :: generation
    integer :: nseen
    integer(int64) :: t0, t1, rate
    if (seen(generation)) return
    seen(generation) = .true.
    call system_clock(t0, rate)
!$omp atomic update
    nreported = nreported + 1
    do
!$omp atomic read
      nseen = nreported
      if (nseen .ge. 2) exit
      call system_clock(t1)
      if (t1 - t0 .gt. 10_int64*rate) then
!$omp atomic write
        ntimeout = 1
        exit
      endif
    enddo
  end subroutine rendezvous
end module osd_pairs_concurrent_controls

program test_osd174_91var_pairs_concurrent
! Two threads decode at the same time with the packed pair table; each result
! must equal the same decode run alone, and both must have used the table.  The
! generator matrix is built before the threads start, so what is left to race
! is the decoding itself.  Without ThreadSanitizer the comparison can only
! catch a race that changes the result; ThreadSanitizer reports any data race
! in these decodes.
  use iso_fortran_env, only: int64
  use omp_lib
  use decode_completion_module, only: set_progress_receiver
  use osd_pairs_concurrent_controls
  implicit none
  integer, parameter :: NTHREAD = 2
  integer*1 :: cwtrue(N), msg77(77), cw_ser(N,2), msg_ser(77,2), cw(N), m(77), cw2(N), m2(77)
  integer :: nh_ser(2), it, i, nh, nd, nh2, ntry
  real :: dmin_ser(2), dmin, dmin2, g, u1, u2, sigma, llr(N)
  logical :: pu
  integer(int64) :: state

  if (omp_get_max_threads() .lt. NTHREAD) error stop 'two OpenMP workers are required'

  state = 20261120_int64
  apmask = 0
! The first two inputs, from a fixed sequence, for which the pair pass improves
! on the result without it, so that a decode gone wrong in the pair table shows.
! The first call also builds the generator matrix.
  it = 0
  do ntry = 1, 400
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
    nd = 3
    call osd174_91var_core(llr, apmask, nd, m, cw, nh, dmin, 1, 0, .false., pu)
    if (.not. pu) error stop 'the packed pair table was not used'
    nd = 2
    call osd174_91var_core(llr, apmask, nd, m2, cw2, nh2, dmin2, 1, 0, .false., pu)
    if (.not. (dmin .lt. dmin2 .and. nh .gt. 0)) cycle
    it = it + 1
    llrs(:,it) = llr
    msg_ser(:,it) = m
    cw_ser(:,it) = cw
    nh_ser(it) = nh
    dmin_ser(it) = dmin
    if (it .eq. NTHREAD) exit
  enddo
  if (it .ne. NTHREAD) error stop 'no input found for which the pair pass improves the result'

! Together.  Each thread has its own generation, so the receiver can tell them apart.
  call set_progress_receiver(rendezvous)
!$omp parallel num_threads(NTHREAD) default(shared) private(it, nd, cw, m, nh, dmin, pu)
  it = omp_get_thread_num() + 1
  nd = 3
  call osd174_91var_core(llrs(:,it), apmask, nd, m, cw, nh, dmin, it, it, .false., pu)
  msg_par(:,it) = m
  cw_par(:,it) = cw
  nh_par(it) = nh
  dmin_par(it) = dmin
  packed_par(it) = pu
!$omp end parallel

  if (ntimeout .ne. 0) error stop 'a thread never reached the decoder progress report'
  if (.not. all(seen)) error stop 'both threads must report'
  do it = 1, NTHREAD
    if (.not. packed_par(it)) error stop 'a thread did not use the packed pair table'
    if (any(cw_par(:,it) .ne. cw_ser(:,it)) .or. any(msg_par(:,it) .ne. msg_ser(:,it)) &
         .or. nh_par(it) .ne. nh_ser(it) &
         .or. transfer(dmin_par(it), 1) .ne. transfer(dmin_ser(it), 1)) &
         error stop 'concurrent decode differs from the decode run alone'
  enddo
  print *, 'test_osd174_91var_pairs_concurrent passed'
end program
