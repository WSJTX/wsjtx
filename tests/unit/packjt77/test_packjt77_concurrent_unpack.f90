program test_packjt77_concurrent_unpack

  use omp_lib
  use packjt77
  implicit none

  integer, parameter :: worker_count=4
  integer, parameter :: phase_count=5
  integer, parameter :: iteration_count=1000
  character(len=37) :: source_messages(worker_count,phase_count)
  character(len=77) :: payloads(worker_count,phase_count)
  character(len=37) :: expected_messages(worker_count,phase_count)
  character(len=37) :: decoded
  integer :: failures,i3,iteration,n3,observed_threads,phase,status,worker
  logical :: ok

  failures=0
  observed_threads=0
  source_messages(1,1)='A92EE F5PSR -14'
  source_messages(2,1)='WM3PEN EA6VQ -09'
  source_messages(3,1)='W1FC F5BZB -08'
  source_messages(4,1)='N1JFU EA6EE R-07'
  source_messages(1,2)='K1ABC/P W9XYZ/P R FN42'
  source_messages(2,2)='N0AAA/P N0BBB/P -10'
  source_messages(3,2)='W3ABC/P K4XYZ/P RR73'
  source_messages(4,2)='F1ABC/P G2XYZ/P 73'
  source_messages(1,3)='K1ABC RR73; W9XYZ <KH1/KH7Z> -12'
  source_messages(2,3)='N0AAA RR73; N0BBB <KH1/KH7Z> -14'
  source_messages(3,3)='W3ABC RR73; K4XYZ <KH1/KH7Z> -16'
  source_messages(4,3)='F1ABC RR73; G2XYZ <KH1/KH7Z> -18'
  source_messages(1,4)='WA9XYZ KA1ABC R 16A EMA'
  source_messages(2,4)='N0AAA N0BBB 1B CT'
  source_messages(3,4)='W3ABC K4XYZ R 2C WMA'
  source_messages(4,4)='F1ABC G2XYZ 3D DX'
  source_messages(1,5)='TU; K1ABC W9XYZ R 529 MA'
  source_messages(2,5)='TU; W9XYZ K1ABC R 559 CT'
  source_messages(3,5)='TU; W9XYZ G8ABC R 559 0013'
  source_messages(4,5)='W9XYZ VE3ABC 559 ON'

  do phase=1,phase_count
     do worker=1,worker_count
        call pack77(source_messages(worker,phase),i3,n3, &
             payloads(worker,phase), &
             pack77_options(record_tx_hashes=.false.),status)
        if(status.ne.PACK77_STATUS_ENCODED) then
           write(*,1000) phase,worker,status,trim(source_messages(worker,phase))
1000       format('failed to encode phase ',i0,' worker ',i0,' status ',i0,': ',a)
           error stop 1
        endif
        call unpack77_configured(payloads(worker,phase),0, &
             expected_messages(worker,phase),ok, &
             unpack77_options(thread_index=worker,record_hashes=.false., &
             record_recent_calls=.false.))
        if(.not.ok) then
           write(*,1010) phase,worker,trim(source_messages(worker,phase))
1010       format('failed expected decode for phase ',i0,' worker ',i0,': ',a)
           error stop 1
        endif
     enddo
  enddo

  call omp_set_dynamic(.false.)
  call omp_set_num_threads(worker_count)

!$omp parallel default(none) &
!$omp shared(expected_messages,failures,observed_threads,payloads) &
!$omp private(decoded,iteration,ok,phase,worker)
  worker=omp_get_thread_num()+1
!$omp single
  observed_threads=omp_get_num_threads()
!$omp end single
  do phase=1,phase_count
     do iteration=1,iteration_count
!$omp barrier
        call unpack77_configured(payloads(worker,phase),0,decoded,ok, &
             unpack77_options(thread_index=worker,record_hashes=.false., &
             record_recent_calls=.false.))
        if(.not.ok .or. decoded.ne.expected_messages(worker,phase)) then
!$omp atomic update
           failures=failures+1
        endif
!$omp barrier
     enddo
  enddo
!$omp end parallel

  if(observed_threads.ne.worker_count) then
     write(*,1020) worker_count,observed_threads
1020 format('expected ',i0,' OpenMP workers, got ',i0)
     error stop 1
  endif
  if(failures.ne.0) then
     write(*,1030) failures,worker_count*phase_count*iteration_count
1030 format('concurrent unpack mismatches: ',i0,' of ',i0)
     error stop 1
  endif

  write(*,1040) worker_count*phase_count*iteration_count,worker_count
1040 format('concurrent unpack decodes passed: ',i0,' across ',i0,' threads')

end program test_packjt77_concurrent_unpack
