program test_ft8var_subtract_concurrent
! subtractft8var called from several threads at once gives exactly what the
! same calls give one after another.  Each thread builds its own filter tables
! on its first call.
  use omp_lib
  use ft8_mod1, only: endcorr
  implicit none
  interface
    subroutine subtractft8var(residual,itone,f0,dt,delta)
      real, intent(inout) :: residual(180000)
      integer, intent(in) :: itone(79)
      real, intent(in) :: f0,dt
      real, intent(out), optional :: delta(151680)
    end subroutine subtractft8var
  end interface
  integer, parameter :: NJOB=8,NMAX=180000
  real, allocatable :: serial(:,:),parallel(:,:),source(:)
  real :: f0(NJOB),dt(NJOB)
  integer :: itone(79),i,j
  integer(8) :: state

  allocate(serial(NMAX,NJOB),parallel(NMAX,NJOB),source(NMAX))
  endcorr=1.0
  state=20261113_8
  do j=1,NMAX
    state=modulo(48271_8*state,2147483647_8)
    source(j)=100.0*(2*real(state)/2147483647.-1)
  enddo
  do i=1,79
    itone(i)=modulo(3*i+i*i/7,8)
  enddo
  do j=1,NJOB
    f0(j)=800.0+250.0*j
    dt(j)=-0.3+0.4*j
    serial(:,j)=source
    parallel(:,j)=source
  enddo

  do j=1,NJOB
    call subtractft8var(serial(:,j),itone,f0(j),dt(j))
  enddo
!$omp parallel do num_threads(4) schedule(static,1) private(j)
  do j=1,NJOB
    call subtractft8var(parallel(:,j),itone,f0(j),dt(j))
  enddo
!$omp end parallel do

  do j=1,NJOB
    if(any(serial(:,j).ne.parallel(:,j))) error stop 'parallel subtraction differs from serial'
    if(all(serial(:,j).eq.source)) error stop 'subtraction changed nothing'
  enddo
  print *,'test_ft8var_subtract_concurrent passed'
end program
