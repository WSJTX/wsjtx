module indexx_mod
  implicit none
contains
  subroutine indexx(arr, n, indx)
  implicit none
  integer, intent(in) :: n
  real,    intent(in) :: arr(n)
  integer, intent(out) :: indx(n)

  integer, parameter :: M = 7, NSTACK = 50
  integer :: i, j, k, l, ir, jstack
  integer :: istack(NSTACK)
  integer :: indxt, itemp
  real    :: a

  ! Initialize index array
  do j = 1, n
     indx(j) = j
  end do

  jstack = 0
  l = 1
  ir = n

  do   ! main loop replacing label 1

     ! Use insertion sort for small subarrays
     if (ir - l < M) then
        do j = l+1, ir
           indxt = indx(j)
           a = arr(indxt)

           do i = j-1, l, -1
              if (arr(indx(i)) <= a) exit
              indx(i+1) = indx(i)
           end do

           indx(i+1) = indxt
        end do

        if (jstack == 0) return

        ir = istack(jstack)
        l  = istack(jstack-1)
        jstack = jstack - 2

     else
        ! Quicksort partitioning
        k = (l + ir) / 2
        itemp = indx(k); indx(k) = indx(l+1); indx(l+1) = itemp

        if (arr(indx(l+1)) > arr(indx(ir))) then
           itemp = indx(l+1); indx(l+1) = indx(ir); indx(ir) = itemp
        end if

        if (arr(indx(l)) > arr(indx(ir))) then
           itemp = indx(l); indx(l) = indx(ir); indx(ir) = itemp
        end if

        if (arr(indx(l+1)) > arr(indx(l))) then
           itemp = indx(l+1); indx(l+1) = indx(l); indx(l) = itemp
        end if

        i = l + 1
        j = ir
        indxt = indx(l)
        a = arr(indxt)

        ! Partition loop replacing labels 3 and 4
        do
	   ! Move i right
           do
              i = i + 1
	      if (i > ir) exit  ! exit inner loop
              if (arr(indx(i)) >= a) exit
           end do

	   ! Move j left
           do
              j = j - 1
	      if (j < l) then
	        exit  ! exit inner loop
	        end if
              if (arr(indx(j)) <= a) exit
           end do

           ! If j < i, partition is done
           if (j < i) exit

	   ! Swap
	   itemp = indx(i)
	   indx(i) = indx(j)
	   indx(j) = itemp
        end do

	! But now j might be < l, so clamp it:
	if (j < l) j = l

        indx(l) = indx(j)
        indx(j) = indxt


        ! Push larger segment, process smaller first
        jstack = jstack + 2
        if (jstack > NSTACK) stop 'NSTACK too small in indexx'

        if (ir - i + 1 >= j - l) then
           istack(jstack)   = ir
           istack(jstack-1) = i
           ir = j - 1
        else
           istack(jstack)   = j - 1
           istack(jstack-1) = l
           l = i
        end if
     end if

  end do

  end subroutine indexx
end module indexx_mod

