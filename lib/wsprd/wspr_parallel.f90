! SPDX-License-Identifier: GPL-3.0-or-later
! Threads for WSPR's decoder (lib/wsprd/wspr_decoder.c), which runs its
! per-candidate loops, its decode attempts and subtraction's filter through
! wspr_parallel_for. Its OSD, osdwspr, keeps its state per thread in the
! OpenMP build, and takes each thread's pattern boxes from the decoder
! (lib/wsprd/osdwspr.f90). OpenMP lives here, in Fortran,
! because Apple Clang has none for C, and on macOS a C runtime would be a
! second one beside gfortran's libgomp; this way every platform runs the
! same code on the runtime the Fortran already uses. Built with OpenMP
! (wsjt_fort_omp, so wsjt-codec) the loops take threads; built without
! (wsjt_fort, wsprd) they run in order on the calling thread.

! The thread count for a requested one on this host: the rule below with
! OpenMP's CPU count, or one without OpenMP.
function wspr_parallel_threads(requested) result(threads) bind(C, name='wspr_parallel_threads')
  use, intrinsic :: iso_c_binding, only: c_int
  !$ use omp_lib, only: omp_get_num_procs
  implicit none
  integer(c_int), value :: requested
  integer(c_int) :: threads
  integer(c_int) :: cores
  interface
     function wspr_parallel_thread_rule(requested, cores) result(threads) &
          bind(C, name='wspr_parallel_thread_rule')
       import :: c_int
       integer(c_int), value :: requested, cores
       integer(c_int) :: threads
     end function wspr_parallel_thread_rule
  end interface
  threads = 1
  cores = 0
  !$ cores = omp_get_num_procs()
  if (cores .gt. 0) threads = wspr_parallel_thread_rule(requested, cores)
end function wspr_parallel_threads

! OpenMP's CPU count, or 0 without OpenMP.
function wspr_parallel_cpus() result(cpus) bind(C, name='wspr_parallel_cpus')
  use, intrinsic :: iso_c_binding, only: c_int
  !$ use omp_lib, only: omp_get_num_procs
  implicit none
  integer(c_int) :: cpus
  cpus = 0
  !$ cpus = omp_get_num_procs()
end function wspr_parallel_cpus

! ft8_threads' rule (lib/ft8_engine_kernel.f90) for a requested count on
! cores CPUs: 0 picks from the CPU count (1 core 1, 2 to 4 one fewer, 5 to 8
! two fewer, 9 to 15 three fewer, 16 or more 12); 1 to 12 runs that many, at
! most the CPU count; anything else one.
function wspr_parallel_thread_rule(requested, cores) result(threads) &
     bind(C, name='wspr_parallel_thread_rule')
  use, intrinsic :: iso_c_binding, only: c_int
  implicit none
  integer(c_int), value :: requested, cores
  integer(c_int) :: threads
  threads = 1
  if (requested .eq. 0) then
     if (cores .le. 1) then
        threads = 1
     else if (cores .lt. 5) then
        threads = cores - 1
     else if (cores .lt. 9) then
        threads = cores - 2
     else if (cores .lt. 16) then
        threads = cores - 3
     else
        threads = 12
     end if
  else if (requested .gt. 0 .and. requested .lt. 13) then
     threads = min(requested, cores)
  end if
end function wspr_parallel_thread_rule

! body(arg, index, thread) for each index 0 to count - 1, on up to threads
! threads, one index at a time as each comes free; thread is the caller's
! 0 to threads - 1, so that each can keep a workspace of its own. Returns,
! when every index is done, the number of threads that ran them (1 without
! OpenMP). One call at a time per process, as the decoder runs
! (lib/wsprd/wsprd_decode.h).
function wspr_parallel_for(count, threads, body, arg) result(ran) bind(C, name='wspr_parallel_for')
  use, intrinsic :: iso_c_binding, only: c_int, c_ptr, c_funptr, c_f_procpointer
  !$ use omp_lib, only: omp_get_thread_num, omp_get_num_threads, omp_get_dynamic, &
  !$      omp_set_dynamic
  implicit none
  integer(c_int), value :: count, threads
  type(c_funptr), value :: body
  type(c_ptr), value :: arg
  integer(c_int) :: ran
  abstract interface
     subroutine body_proc(arg, index, thread) bind(C)
       import :: c_int, c_ptr
       type(c_ptr), value :: arg
       integer(c_int), value :: index, thread
     end subroutine body_proc
  end interface
  ! ThreadSanitizer's view of the region's edges (lib/wsprd/wspr_tsan.c).
  interface
     subroutine wspr_tsan_release_fork() bind(C)
     end subroutine wspr_tsan_release_fork
     subroutine wspr_tsan_acquire_fork() bind(C)
     end subroutine wspr_tsan_acquire_fork
     subroutine wspr_tsan_release_join() bind(C)
     end subroutine wspr_tsan_release_join
     subroutine wspr_tsan_acquire_join() bind(C)
     end subroutine wspr_tsan_acquire_join
  end interface
  ! What the threads read, saved before the fork mark: shared locals would
  ! reach them through a block the compiler fills at the directive, after it.
  procedure(body_proc), pointer, save :: run => null()
  type(c_ptr), save :: work
  integer(c_int), save :: last, size
  integer(c_int) :: i, thread, team
  !$ logical :: dynamic
  call c_f_procpointer(body, run)
  work = arg
  last = count - 1
  size = 1
  team = max(1, threads)
  ! The team the count asks for: another decoder on this thread may have
  ! turned on dynamic teams (lib/decoder.f90), which would shrink it.
  !$ dynamic = omp_get_dynamic()
  !$ call omp_set_dynamic(.false.)
  call wspr_tsan_release_fork()
  !$omp parallel num_threads(team) private(i, thread) if(team .gt. 1 .and. count .gt. 1)
  call wspr_tsan_acquire_fork()
  thread = 0
  !$ thread = omp_get_thread_num()
  !$ if (thread .eq. 0) size = omp_get_num_threads()   ! the caller's thread
  !$omp do schedule(dynamic, 1)
  do i = 0, last
     call run(work, i, thread)
  end do
  !$omp end do nowait
  call wspr_tsan_release_join()
  !$omp end parallel
  call wspr_tsan_acquire_join()
  !$ call omp_set_dynamic(dynamic)
  ran = size
end function wspr_parallel_for
