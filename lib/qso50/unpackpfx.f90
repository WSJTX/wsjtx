subroutine unpackpfx(ng,call1)

  character*12 call1
  character*3 pfx

  if(ng.lt.60000) then
! Add-on prefix of 1 to 3 characters
     n=ng
     do i=3,1,-1
        nc=mod(n,37)
        if(nc.ge.0 .and. nc.le.9) then
           pfx(i:i)=char(nc+48)
        else if(nc.ge.10 .and. nc.le.35) then
           pfx(i:i)=char(nc+55)
        else
           pfx(i:i)=' '
        endif
        n=n/37
     enddo
     call1=pfx//'/'//call1
     if(call1(1:1).eq.' ') call1=call1(2:)
     if(call1(1:1).eq.' ') call1=call1(2:)
  else
! Add-on suffix, one character
     i1=index(call1,' ')
     nc=ng-60000
     if(nc.ge.0 .and. nc.le.9) then
        call1=call1(:i1-1)//'/'//char(nc+48)
     else if(nc.ge.10 .and. nc.le.35) then
        call1=call1(:i1-1)//'/'//char(nc+55)
     endif
  endif

  return
end subroutine unpackpfx
