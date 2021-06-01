subroutine graycode(dat,n,idir)

  integer dat(n)
  do i=1,n
     dat(i)=igray(dat(i),idir)
  enddo

  return
end subroutine graycode

