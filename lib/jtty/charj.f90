character*1 function charj(j)

  ! Returns the printable character corresponding to JTTY index j (0-63),
  ! except j=36 returns '~' instead of <space>.

  character*64 c
!                   1         2         3         4         5         6
! j       0123456789012345678901234567890123456789012345678901234567890123
  data c/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!@#$%^&*()_`=[]{}<>|:;'/

!  if(j.eq.36) then
!     charj='~'
!  else
     charj=c(j+1:j+1)
!  endif

  return
end function charj

integer function jchar(c0)

! Returns the JTTY index (0-63) corresponding to character c0.
  
  character*1 c0
  character*64 c
!                   1         2         3         4         5         6
! j       0123456789012345678901234567890123456789012345678901234567890123
  data c/'0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!@#$%^&*()_`=[]{}<>|:;'/

  jchar=index(c,c0)-1
  
  return
end function jchar
