subroutine wqencode(msg,ntype,data0)
  use, intrinsic :: iso_c_binding, only: c_char,c_int,c_int8_t,c_size_t,c_ptr,c_loc
  implicit none

  character(kind=c_char,len=22), intent(in) :: msg
  integer, intent(out) :: ntype
  integer(c_int8_t), target, intent(out) :: data0(11)

  interface
     integer(c_int) function wspr_pack_message(text,length,data) bind(C)
       import c_char,c_int,c_size_t,c_ptr
       character(kind=c_char), intent(in) :: text(*)
       integer(c_size_t), value :: length
       type(c_ptr), value :: data
     end function wspr_pack_message
  end interface

  ntype=wspr_pack_message(msg,len(msg,kind=c_size_t),c_loc(data0))
end subroutine wqencode
