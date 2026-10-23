module ft8_bit_metrics
  use ieee_arithmetic, only: ieee_is_finite
  implicit none
  private
  public :: ft8_bit_marginals

contains

  pure subroutine ft8_bit_marginals(scores,nsym,maxone,maxzero,finite_scores)
    integer, intent(in) :: nsym
    real, intent(in) :: scores(0:2**(3*nsym)-1)
    real, intent(out) :: maxone(0:8),maxzero(0:8)
    logical, intent(out) :: finite_scores
    real :: marginal(0:7,3)
    integer :: position,first_bit

    ! Nonfinite scores require the caller's original masked reductions.
    finite_scores=all(ieee_is_finite(scores))
    if(.not.finite_scores) return

    ! A score index packs labels from first to last, three bits per symbol.
    ! Explicit-shape arguments view the last label as the contiguous axis.
    select case(nsym)
    case(1)
       marginal(:,1)=scores(0:7)
    case(2)
       call pair_marginals(scores,marginal(:,1:2))
    case(3)
       call triple_marginals(scores,marginal)
    end select

    ! Reduce the eight label maxima into the three fixed bit partitions.
    do position=1,nsym
       first_bit=3*(position-1)
       maxone(first_bit)=max(marginal(4,position),marginal(5,position), &
            marginal(6,position),marginal(7,position))
       maxzero(first_bit)=max(marginal(0,position),marginal(1,position), &
            marginal(2,position),marginal(3,position))
       maxone(first_bit+1)=max(marginal(2,position),marginal(3,position), &
            marginal(6,position),marginal(7,position))
       maxzero(first_bit+1)=max(marginal(0,position),marginal(1,position), &
            marginal(4,position),marginal(5,position))
       maxone(first_bit+2)=max(marginal(1,position),marginal(3,position), &
            marginal(5,position),marginal(7,position))
       maxzero(first_bit+2)=max(marginal(0,position),marginal(2,position), &
            marginal(4,position),marginal(6,position))
    enddo
  end subroutine ft8_bit_marginals

  pure subroutine pair_marginals(scores,marginal)
    real, intent(in) :: scores(0:7,0:7)
    real, intent(out) :: marginal(0:7,2)
    integer :: first

    do first=0,7
       marginal(first,1)=max(scores(0,first),scores(1,first), &
            scores(2,first),scores(3,first),scores(4,first),scores(5,first), &
            scores(6,first),scores(7,first))
    enddo
    marginal(:,2)=max(scores(:,0),scores(:,1),scores(:,2),scores(:,3), &
         scores(:,4),scores(:,5),scores(:,6),scores(:,7))
  end subroutine pair_marginals

  pure subroutine triple_marginals(scores,marginal)
    real, intent(in) :: scores(0:7,0:7,0:7)
    real, intent(out) :: marginal(0:7,3)
    real :: without_first(0:7,0:7),without_middle(0:7,0:7)
    integer :: first,middle

    ! Eliminate the middle label before reducing each contiguous last-label row.
    do first=0,7
       without_middle(:,first)=max(scores(:,0,first),scores(:,1,first), &
            scores(:,2,first),scores(:,3,first),scores(:,4,first), &
            scores(:,5,first),scores(:,6,first),scores(:,7,first))
    enddo
    do first=0,7
       marginal(first,1)=max(without_middle(0,first),without_middle(1,first), &
            without_middle(2,first),without_middle(3,first),without_middle(4,first), &
            without_middle(5,first),without_middle(6,first),without_middle(7,first))
    enddo

    ! Eliminate the first label, leaving maxima indexed by (last,middle).
    without_first=max(scores(:,:,0),scores(:,:,1),scores(:,:,2),scores(:,:,3), &
         scores(:,:,4),scores(:,:,5),scores(:,:,6),scores(:,:,7))
    do middle=0,7
       marginal(middle,2)=max(without_first(0,middle),without_first(1,middle), &
            without_first(2,middle),without_first(3,middle), &
            without_first(4,middle),without_first(5,middle), &
            without_first(6,middle),without_first(7,middle))
    enddo
    marginal(:,3)=max(without_first(:,0),without_first(:,1),without_first(:,2), &
         without_first(:,3),without_first(:,4),without_first(:,5), &
         without_first(:,6),without_first(:,7))
  end subroutine triple_marginals

end module ft8_bit_metrics
