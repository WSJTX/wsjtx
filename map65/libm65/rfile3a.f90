module rfile3a_mod
  implicit none
contains
  subroutine rfile3a(infile,ibuf,n,fcenter,ierr)
    use iso_c_binding
    use iso_fortran_env, only: real64, int64
    implicit none
    character*(*), intent(in) :: infile
    integer, intent(in) :: n
    ! integer*8, intent(out) :: ibuf(n/8) ! ibuf(n) in original but read (ibuf(i),i=1,n/8). So ibuf must be n/8 size if int*8? 
    ! Wait, original: integer*8 ibuf(n). read (ibuf(i),i=1,n/8).
    ! So it writes to the first n/8 elements. 
    ! But if n is byte count, and ibuf is 8 bytes, then n/8 is 64-bit word count.
    ! So ibuf is integer*8 array of size at least n/8.
    ! The declaration integer*8 ibuf(n) implies size n.
    ! I will keep it as ibuf(n) or ibuf(*).
    integer(int64), intent(out) :: ibuf(n)
    real(real64), intent(out) :: fcenter
    integer, intent(out) :: ierr
    
    integer :: i
  
    open(10,file=infile,access='stream',status='old',err=998)
    read(10,end=998) (ibuf(i),i=1,n/8),fcenter
    ierr=0
    close(10)
    return
  
  998 ierr=1002
    close(10)
    return
  end subroutine rfile3a
end module rfile3a_mod
