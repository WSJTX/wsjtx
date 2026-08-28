program test_map65_jt65_candidate_consistency
  use extract_mod, only: jt65_candidate_is_consistent
  implicit none

  call require(jt65_candidate_is_consistent(48,79,0.806,5.509), &
       'a weak faded decode with high hard distance remains eligible')
  call require(jt65_candidate_is_consistent(26,28,0.0,24.628), &
       'a strong decode with high matched power remains eligible')
  call require(jt65_candidate_is_consistent(45,81,0.844,4.463), &
       'the randomized holdout decode remains eligible')

  call require(.not.jt65_candidate_is_consistent(47,83,0.829,12.700), &
       'the reproduced wideband false decode is rejected')
  call require(.not.jt65_candidate_is_consistent(42,72,0.0,16.740), &
       'a high-power distant codeword is rejected')
  call require(.not.jt65_candidate_is_consistent(44,81,0.844,7.819), &
       'an elevated-power high-burden codeword is rejected')
  call require(.not.jt65_candidate_is_consistent(44,80,0.893,4.772), &
       'an ambiguous high-burden codeword is rejected')

  call require(jt65_candidate_is_consistent(41,79,0.0,20.0), &
       'matched power alone does not reject a nearby codeword')
  call require(jt65_candidate_is_consistent(49,80,0.869,7.499), &
       'sub-threshold evidence remains eligible')

  print '(a)', 'MAP65 JT65 candidate consistency tests passed.'

contains

  subroutine require(condition,description)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: description

    if(.not.condition) then
       print '(a)', 'FAIL: '//description
       error stop 1
    endif
  end subroutine require
end program test_map65_jt65_candidate_consistency
