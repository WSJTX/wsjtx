module ft8_coarse_cache
  use iso_fortran_env, only: int64
  implicit none
  private
  public :: coarse_cache_lookup,coarse_cache_store,coarse_cache_reset
  integer, parameter :: bins=1920,capacity=48,key_fields=7
  type :: cache_entry
    logical :: valid=.false.
    integer(int64) :: epoch=-1_int64,generation=-1_int64
    integer :: fields(key_fields)=0
    real :: red(bins)
    integer :: peak(bins)
    logical(1) :: cq(bins)
  end type cache_entry
  type(cache_entry), save :: entries(capacity)
  integer, save :: next_slot=1
  interface
    subroutine coarse_cache_lock() bind(C,name='wsjt_ft8_coarse_lock')
    end subroutine
    subroutine coarse_cache_unlock() bind(C,name='wsjt_ft8_coarse_unlock')
    end subroutine
  end interface
contains
  subroutine coarse_cache_reset()
    call coarse_cache_lock()
    entries%valid=.false.
    next_slot=1
    call coarse_cache_unlock()
  end subroutine coarse_cache_reset

  subroutine coarse_cache_lookup(epoch,generation,fields,red,peak,cq,hit)
    integer(int64), intent(in) :: epoch,generation
    integer, intent(in) :: fields(key_fields)
    real, intent(inout) :: red(bins)
    integer, intent(inout) :: peak(bins)
    logical(1), intent(inout) :: cq(bins)
    logical, intent(out) :: hit
    integer :: i
    call coarse_cache_lock()
    do i=1,capacity
      if(.not.entries(i)%valid) cycle
      if(entries(i)%epoch.ne.epoch .or. entries(i)%generation.ne.generation) cycle
      if(any(entries(i)%fields.ne.fields)) cycle
      red=entries(i)%red
      peak=entries(i)%peak
      cq=entries(i)%cq
      hit=.true.

      call coarse_cache_unlock()
      return
    enddo
    hit=.false.

    ! A miss retains the mutex until a complete immutable result is stored.
  end subroutine coarse_cache_lookup

  subroutine coarse_cache_store(epoch,generation,fields,lower,upper,red,peak,cq)
    integer(int64), intent(in) :: epoch,generation
    integer, intent(in) :: fields(key_fields),lower,upper,peak(bins)
    real, intent(in) :: red(bins)
    logical(1), intent(in) :: cq(bins)
    entries(next_slot)%epoch=epoch
    entries(next_slot)%generation=generation
    entries(next_slot)%fields=fields
    entries(next_slot)%red=red
    entries(next_slot)%peak=0
    entries(next_slot)%peak(lower:upper)=peak(lower:upper)
    entries(next_slot)%cq=cq
    entries(next_slot)%valid=.true.
    next_slot=mod(next_slot,capacity)+1
    call coarse_cache_unlock()
  end subroutine coarse_cache_store
end module ft8_coarse_cache
