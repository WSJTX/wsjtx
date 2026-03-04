subroutine getpfx1(callsign,k,nv2)

  character*12 callsign0,callsign,lof,rof
  character*8 c
  character addpfx*8,tpfx*4,tsfx*3
  logical ispfx,issfx,invalid
  common/pfxcom/addpfx
  include 'pfx.f90'

  callsign0=callsign
  nv2=0
  iz=index(callsign,' ') - 1
  if(iz.lt.0) iz=12
  islash=index(callsign(1:iz),'/')
  k=0
  c='   '
  if(islash.gt.0 .and. islash.le.(iz-4)) then
!  Add-on prefix
     c=callsign(1:islash-1)
     callsign=callsign(islash+1:iz)
     do i=1,NZ
        if(pfx(i)(1:4).eq.c) then
           k=i
           go to 10
        endif
     enddo
     if(addpfx.eq.c) then
        k=449
        go to 10
     endif

  else if(islash.eq.(iz-1)) then
!  Add-on suffix
     c=callsign(islash+1:iz)
     callsign=callsign(1:islash-1)
     do i=1,NZ2
        if(sfx(i).eq.c(1:1)) then
           k=400+i
           go to 10
        endif
     enddo
  endif

10 if(islash.ne.0 .and.k.eq.0) then
!  Original JT65 would force this compound callsign to be treated as
!  plain text.  In JT65v2, we will encode the prefix or suffix into nc1.
!  The task here is to compute the proper value of k.
     lof=callsign0(:islash-1)
     rof=callsign0(islash+1:)
     llof=len_trim(lof)
     lrof=len_trim(rof)
     ispfx=(llof.gt.0 .and. llof.le.4)
     issfx=(lrof.gt.0 .and. lrof.le.3)
     invalid=.not.(ispfx.or.issfx)
     if(ispfx.and.issfx) then
        if(llof.lt.3) issfx=.false.
        if(lrof.lt.3) ispfx=.false.
        if(ispfx.and.issfx) then
           i=ichar(callsign0(islash-1:islash-1))
           if(i.ge.ichar('0') .and. i.le.ichar('9')) then
              issfx=.false.
           else
              ispfx=.false.
           endif
        endif
     endif

     if(invalid) then
        k=-1
     else
        if(ispfx) then
           tpfx=lof(1:4)
           k=nchar(tpfx(1:1))
           k=37*k + nchar(tpfx(2:2))
           k=37*k + nchar(tpfx(3:3))
           k=37*k + nchar(tpfx(4:4))
           nv2=1
           i=index(callsign0,'/')
           callsign=callsign0(:i-1)
           callsign=callsign0(i+1:)
        endif
        if(issfx) then
           tsfx=rof(1:3)
           k=nchar(tsfx(1:1))
           k=37*k + nchar(tsfx(2:2))
           k=37*k + nchar(tsfx(3:3))
           nv2=2
           i=index(callsign0,'/')
           callsign=callsign0(:i-1)
        endif
     endif
  endif

  return
end subroutine getpfx1

