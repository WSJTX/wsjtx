module morse_encoder

  implicit none
  private
  public encode_morse

contains

subroutine encode_morse(msg,idat,n)

! Convert ascii message to a Morse code bit string.
!    Dash = 3 dots
!    Space between dots, dashes = 1 dot
!    Space between letters = 3 dots
!    Space between words = 7 dots

  implicit none

  character(len=*), intent(in) :: msg
  integer, intent(out) :: idat(:),n
  integer*1 ic(21,38)
  integer i,j,jj,k,msglen,nmax
  data ic/                                        &
     1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,20,  &
     1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,0,0,18,  &
     1,0,1,0,1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0,16,  &
     1,0,1,0,1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,14,  &
     1,0,1,0,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,12,  &
     1,0,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,1,1,0,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,12,  &
     1,1,1,0,1,1,1,0,1,0,1,0,1,0,0,0,0,0,0,0,14,  &
     1,1,1,0,1,1,1,0,1,1,1,0,1,0,1,0,0,0,0,0,16,  &
     1,1,1,0,1,1,1,0,1,1,1,0,1,1,1,0,1,0,0,0,18,  &
     1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 6,  &
     1,1,1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,1,1,0,1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,12,  &
     1,1,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0, 8,  &
     1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 2,  &
     1,0,1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,1,1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,0,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0, 8,  &
     1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 4,  &
     1,0,1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,14,  &
     1,1,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,0,1,1,1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0, 8,  &
     1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 6,  &
     1,1,1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,12,  &
     1,0,1,1,1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,12,  &
     1,1,1,0,1,1,1,0,1,0,1,1,1,0,0,0,0,0,0,0,14,  &
     1,0,1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0, 8,  &
     1,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 6,  &
     1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 4,  &
     1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0, 8,  &
     1,0,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,0,0,0,0,10,  &
     1,1,1,0,1,0,1,0,1,1,1,0,0,0,0,0,0,0,0,0,12,  &
     1,1,1,0,1,0,1,1,1,0,1,1,1,0,0,0,0,0,0,0,14,  &
     1,1,1,0,1,1,1,0,1,0,1,0,0,0,0,0,0,0,0,0,12,  &
     1,1,1,0,1,0,1,0,1,1,1,0,1,0,0,0,0,0,0,0,14,  &
     0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 2/     !Incremental word space

  msglen=len(msg)
  idat=0
  n=6
  do k=1,msglen
     jj=ichar(msg(k:k))
     if(jj.ge.97 .and. jj.le.122) jj=jj-32  !Convert lower to upper case
     if(jj.ge.48 .and. jj.le.57) then
        j=jj-48                              !Numbers
     else if(jj.ge.65 .and. jj.le.90) then
        j=jj-55                              !Letters
     else if(jj.eq.47) then
        j=36                                 !Slash (/)
     else if(jj.eq.32) then
        j=37                                 !Word space
     else
        idat=0
        n=0
        return
     endif
     j=j+1

! Insert this character
     nmax=ic(21,j)
     if (n + nmax + 6 .gt. size (idat)) exit
     do i=1,nmax
        n=n+1
        idat(n)=ic(i,j)
     enddo

! Insert character space of 2 dit lengths:
     n=n+1
     idat(n)=0
     n=n+1
     idat(n)=0
  enddo

! Insert word space at end of message
  do j=1,4
     n=n+1
     idat(n)=0
  enddo

  return
end subroutine encode_morse

end module morse_encoder

subroutine morse(msg,idat,n)

  use morse_encoder, only: encode_morse
  implicit none

  character(len=*), intent(in) :: msg
  integer, intent(out) :: idat(250),n
  call encode_morse(msg,idat,n)

end subroutine morse
