module jt65_mod9
  use, intrinsic :: iso_fortran_env, only: iostat_end
  implicit none
  private
  public :: load_callsign_database, callsign_database_available, callsign_in_database

  type :: callsign_bucket
    character(len=7), allocatable :: calls(:)
  end type callsign_bucket

  type :: callsign_database
    type(callsign_bucket) :: buckets(16)
  end type callsign_database

  type(callsign_database), allocatable, save :: database

contains

  subroutine load_callsign_database(path)
    character(len=*), intent(in) :: path
    type(callsign_database), allocatable :: pending
    character(len=80) :: line
    character(len=7) :: callsign
    integer :: unit, ios, bucket, i, counts(16), inserted(16)
    logical :: sentinel

    ! A failed load must leave database filtering unavailable.
    if(allocated(database)) deallocate(database)
    open(newunit=unit,file=path,status='old',action='read',iostat=ios)
    if(ios.ne.0) return

    counts=0
    do
      read(unit,'(a80)',iostat=ios) line
      if(ios.eq.iostat_end) exit
      if(ios.ne.0) go to 900
      call parse_record(line,callsign,bucket,sentinel)
      if(sentinel) exit
      if(bucket.eq.0) cycle
      if(counts(bucket).eq.huge(counts(bucket))) go to 900
      counts(bucket)=counts(bucket)+1
    enddo
    if(any(counts.lt.1)) go to 900

    allocate(pending,stat=ios)
    if(ios.ne.0) go to 900
    do i=1,size(counts)
      allocate(pending%buckets(i)%calls(counts(i)),stat=ios)
      if(ios.ne.0) go to 900
    enddo
    rewind(unit,iostat=ios)
    if(ios.ne.0) go to 900

    inserted=0
    do
      read(unit,'(a80)',iostat=ios) line
      if(ios.eq.iostat_end) exit
      if(ios.ne.0) go to 900
      call parse_record(line,callsign,bucket,sentinel)
      if(sentinel) exit
      if(bucket.eq.0) cycle
      if(inserted(bucket).ge.size(pending%buckets(bucket)%calls)) go to 900
      inserted(bucket)=inserted(bucket)+1
      pending%buckets(bucket)%calls(inserted(bucket))=callsign
    enddo
    do i=1,size(inserted)
      if(inserted(i).ne.size(pending%buckets(i)%calls)) go to 900
    enddo
    close(unit,iostat=ios)
    if(ios.ne.0) return
    call move_alloc(pending,database)
    return

900 close(unit,iostat=ios)
  end subroutine load_callsign_database

  logical function callsign_database_available()
    callsign_database_available=allocated(database)
  end function callsign_database_available

  logical function callsign_in_database(callsign)
    character(len=*), intent(in) :: callsign
    integer :: bucket, i

    callsign_in_database=.false.
    if(.not.allocated(database)) return
    if(len(callsign).eq.0) return
    bucket=bucket_for_prefix(callsign(1:1))
    if(bucket.eq.0) return
    do i=1,size(database%buckets(bucket)%calls)
      if(database%buckets(bucket)%calls(i).ne.callsign) cycle
      callsign_in_database=.true.
      return
    enddo
  end function callsign_in_database

  subroutine parse_record(line,callsign,bucket,sentinel)
    character(len=*), intent(in) :: line
    character(len=7), intent(out) :: callsign
    integer, intent(out) :: bucket
    logical, intent(out) :: sentinel
    integer :: comma

    callsign=''
    bucket=0
    sentinel=line(1:4).eq.'ZZZZ'
    if(sentinel .or. line(1:2).eq.'//') return
    comma=index(line,',')
    if(comma.lt.4 .or. comma.gt.8) return
    callsign=line(1:comma-1)
    bucket=bucket_for_prefix(callsign(1:1))
  end subroutine parse_record

  integer function bucket_for_prefix(prefix) result(bucket)
    character, intent(in) :: prefix
    character(len=16), parameter :: upper='CDFHIJKMNOQRTVWZ'
    integer :: i

    bucket=0
    if(prefix.le.'0' .or. prefix.gt.'Z') return
    do i=1,len(upper)
      if(prefix.gt.upper(i:i)) cycle
      bucket=i
      return
    enddo
  end function bucket_for_prefix

end module jt65_mod9
