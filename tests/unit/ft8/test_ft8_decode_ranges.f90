program test_ft8_decode_ranges
  use ft8_decode_ranges, only: max_ft8_decode_ranges, partition_ft8_decode_range
  implicit none

  integer :: requested_ranges

  do requested_ranges=1,max_ft8_decode_ranges
     call assert_partition(200,1000,requested_ranges,requested_ranges)
  enddo

  call assert_empty_partition(4900,1000,12)
  call assert_partition(1000,1000,12,1)
  call assert_partition(999,1000,12,2)
  call assert_partition(4900,5000,12,12)
  call assert_upper_bounds(200,1000,11, &
       [272,345,418,491,564,637,710,783,856,928,1000])
  call assert_upper_bounds(200,1000,12, &
       [266,333,400,467,534,601,668,735,802,868,934,1000])

contains

  subroutine assert_empty_partition(nfa,nfb,requested_ranges)
    integer, intent(in) :: nfa,nfb,requested_ranges
    integer :: range_low(max_ft8_decode_ranges)
    integer :: range_high(max_ft8_decode_ranges)
    integer :: nranges

    call partition_ft8_decode_range(nfa,nfb,requested_ranges,range_low,range_high,nranges)
    if(nranges.ne.0) error stop 'reversed range must produce no partitions'
  end subroutine assert_empty_partition

  subroutine assert_upper_bounds(nfa,nfb,requested_ranges,expected_high)
    integer, intent(in) :: nfa,nfb,requested_ranges
    integer, intent(in) :: expected_high(:)
    integer :: range_low(max_ft8_decode_ranges)
    integer :: range_high(max_ft8_decode_ranges)
    integer :: nranges

    call partition_ft8_decode_range(nfa,nfb,requested_ranges,range_low,range_high,nranges)
    if(nranges.ne.size(expected_high)) error stop 'unexpected exact partition count'
    if(any(range_high(1:nranges).ne.expected_high)) error stop 'unexpected partition boundary'
  end subroutine assert_upper_bounds

  subroutine assert_partition(nfa,nfb,requested_ranges,expected_ranges)
    integer, intent(in) :: nfa,nfb,requested_ranges,expected_ranges
    integer :: i,max_width,min_width,nranges
    integer :: range_low(max_ft8_decode_ranges)
    integer :: range_high(max_ft8_decode_ranges)

    call partition_ft8_decode_range(nfa,nfb,requested_ranges,range_low,range_high,nranges)

    if(nranges.ne.expected_ranges) error stop 'unexpected partition count'
    if(range_low(1).ne.nfa) error stop 'partitioning changed the lower bound'
    if(range_high(nranges).ne.nfb) error stop 'partitioning changed the upper bound'

    do i=1,nranges
       if(range_low(i).gt.range_high(i)) error stop 'partition is reversed'
       if(i.gt.1) then
          if(range_low(i).ne.range_high(i-1)+1) error stop 'partitions are not contiguous'
       endif
    enddo

    min_width=minval(range_high(1:nranges)-range_low(1:nranges)+1)
    max_width=maxval(range_high(1:nranges)-range_low(1:nranges)+1)
    if(max_width-min_width.gt.1) error stop 'partitions are not balanced'
  end subroutine assert_partition

end program test_ft8_decode_ranges
