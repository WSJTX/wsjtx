module ft8_decode_ranges
  implicit none
  private

  integer, parameter, public :: max_ft8_decode_ranges = 12
  public :: partition_ft8_decode_range

contains

  pure subroutine partition_ft8_decode_range(nfa,nfb,requested_ranges,range_low,range_high,nranges)
    integer, intent(in) :: nfa,nfb,requested_ranges
    integer, intent(out) :: range_low(max_ft8_decode_ranges)
    integer, intent(out) :: range_high(max_ft8_decode_ranges)
    integer, intent(out) :: nranges
    integer :: base_width,extra,i,next_low,range_width,total_width

    range_low=0
    range_high=-1
    nranges=0

    if(nfa.gt.nfb) return

    total_width=nfb-nfa+1
    nranges=min(max(1,requested_ranges),max_ft8_decode_ranges,total_width)
    base_width=total_width/nranges
    extra=mod(total_width,nranges)
    next_low=nfa

    do i=1,nranges
       range_width=base_width
       if(i.le.extra) range_width=range_width+1
       range_low(i)=next_low
       range_high(i)=next_low+range_width-1
       next_low=range_high(i)+1
    enddo
  end subroutine partition_ft8_decode_range

end module ft8_decode_ranges
