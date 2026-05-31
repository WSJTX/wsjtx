module ftrsd2_mod
  use iso_c_binding
  implicit none
  interface
    subroutine ftrsd2_(mrsym, mrprob, mr2sym, mr2prob, ntrials0, correct, param, ntry) bind(C, name="ftrsd2_")
      use iso_c_binding
      integer(c_int), intent(in) :: mrsym(*), mrprob(*), mr2sym(*), mr2prob(*)
      integer(c_int), intent(in) :: ntrials0
      integer(c_int), intent(in) :: correct(*), param(*), ntry(*)
    end subroutine ftrsd2_
  end interface
end module ftrsd2_mod
