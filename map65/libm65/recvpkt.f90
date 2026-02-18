subroutine recvpkt(nsam,nblock2,userx_no,k,buf4,buf8,buf16)

  use datcom_ptrs_mod          ! for dd
  implicit none

  integer, parameter :: NSMAX = 60*96000   ! as in legacy

  integer        :: nsam, k, i
  integer*2      :: nblock2
  integer*1      :: userx_no
  real*4         :: buf4(*)
  real*8         :: buf8(*)
  complex*16     :: buf16(*)

  ! Silence unused warning
  if (nblock2 .eq. -9999) nblock2 = -9998

  if (nsam .eq. -1) then
     ! Move data from UDP/PortAudio buffer into dd()

     if (userx_no .eq. -1) then
        ! One RF channel, r*8 data: buf8(i) holds two real*4 values
        do i = 1, 174
           k = k + 1
           call unpack_r8_to_r4(buf8(i), dd(1,k), dd(2,k))
        end do

     else if (userx_no .eq. 1) then
        ! One RF channel, i*2 data: buf4(i) holds two int*2 values
        do i = 1, 348
           k = k + 1
           call unpack_r4_to_i2_as_r4(buf4(i), dd(1,k), dd(2,k))
        end do

     else if (userx_no .eq. -2) then
        ! Two RF channels, r*8 complex data: buf16(i) holds four real*4 values
        do i = 1, 87
           k = k + 1
           call unpack_c16_to_r4(buf16(i), dd(1,k), dd(2,k), dd(3,k), dd(4,k))
        end do

     else if (userx_no .eq. 2) then
        ! Two RF channels, i*2 data: buf8(i) holds four int*2 values
        do i = 1, 174
           k = k + 1
           call unpack_r8_to_i2_as_r4(buf8(i), dd(1,k), dd(2,k), dd(3,k), dd(4,k))
        end do
     end if

  else
     ! nsam >= 0: special case for one RF channel, r*4 data
     if (userx_no .eq. 1) then
        do i = 1, nsam
           k = k + 1
           call unpack_r4_to_i2_as_r4(buf4(i), dd(1,k), dd(2,k))

           k = k + 1
           dd(1,k) = dd(1,k-1)
           dd(2,k) = dd(2,k-1)
        end do
     end if
  end if

contains

  subroutine unpack_r8_to_r4(x, a, b)
    real*8,  intent(in)  :: x
    real*4,  intent(out) :: a, b
    real*4               :: tmp(2)
    tmp = transfer(x, tmp)
    a = tmp(1)
    b = tmp(2)
  end subroutine unpack_r8_to_r4

  subroutine unpack_r4_to_i2_as_r4(x, a, b)
    real*4,    intent(in)  :: x
    real*4,    intent(out) :: a, b
    integer*2              :: tmp(2)
    tmp = transfer(x, tmp)
    a = real(tmp(1))
    b = real(tmp(2))
  end subroutine unpack_r4_to_i2_as_r4

  subroutine unpack_c16_to_r4(x, a1, a2, a3, a4)
    complex*16, intent(in)  :: x
    real*4,     intent(out) :: a1, a2, a3, a4
    real*4                  :: tmp(4)
    tmp = transfer(x, tmp)
    a1 = tmp(1)
    a2 = tmp(2)
    a3 = tmp(3)
    a4 = tmp(4)
  end subroutine unpack_c16_to_r4

  subroutine unpack_r8_to_i2_as_r4(x, a1, a2, a3, a4)
    real*8,    intent(in)  :: x
    real*4,    intent(out) :: a1, a2, a3, a4
    integer*2              :: tmp(4)
    tmp = transfer(x, tmp)
    a1 = real(tmp(1))
    a2 = real(tmp(2))
    a3 = real(tmp(3))
    a4 = real(tmp(4))
  end subroutine unpack_r8_to_i2_as_r4

end subroutine recvpkt
