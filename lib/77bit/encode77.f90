program encode77

  use packjt77

  implicit none

  character*80 msg0
  character msg*37
  character*77 c77
  character*80 infile
  character*13 w(19)
  character*40 status_text
  character*5 type_text
  integer nw(19)
  logical unpk77_success
  integer nargs,iz,iline,i3,n3,nwords,j2,pack_status
  integer nencoded,nrejected,nunpack_failed,ndifferent

  nargs=iargc()
  if(nargs.ne.1 .and.nargs.ne.2) then
     print*,'Usage: encode77 "message"'
     print*,'       encode77 -f <infile>'
     stop 2
  endif
  call getarg(1,msg0)
  if(nargs.eq.2) then
     call getarg(2,infile)
     open(10,file=infile,status='old')
     write(*,1000)
1000 format('Type  Result      Message to be encoded                 Decoded message or reason' &
            /96('-'))
  endif

  nencoded=0
  nrejected=0
  nunpack_failed=0
  ndifferent=0

  do iline=1,999
     if(nargs.eq.2) read(10,1002,end=999) msg0
1002 format(a80)
     if(msg0(1:1).eq.'$') exit
     if(msg0.eq.'                                     ') cycle
     if(msg0(2:2).eq.'.' .or. msg0(3:3).eq.'.') cycle
     if(msg0(1:3).eq.'---') cycle
     msg0=adjustl(msg0)
     call fmtmsg(msg0,iz)
     call pack77(msg0(1:37),i3,n3,c77,status=pack_status)
     if(pack_status.ne.PACK77_STATUS_ENCODED) then
        nrejected=nrejected+1
        status_text=pack77_status_text(pack_status)
        write(*,1004) '--','REJECTED',msg0(1:37),trim(status_text)
        if(nargs.eq.1) stop 1
        cycle
     endif

     nencoded=nencoded+1
     call unpack77(c77,0,msg,unpk77_success)
     if(.not.unpk77_success) then
        nunpack_failed=nunpack_failed+1
        write(*,1004) '--','UNPACK FAIL',msg0(1:37),'--'
        if(nargs.eq.1) stop 1
        cycle
     endif

     type_text=' '
     if(i3.eq.0 .and.n3.eq.6) then
        call split77(msg,nwords,nw,w)
        j2=0
        if(nwords.eq.2 .and. len(trim(w(2))).le.2) j2=1
        if(nwords.eq.2 .and. len(trim(w(2))).eq.6) j2=2
        write(type_text,1005) i3,n3,j2
1005    format(i1,'.',i1,'.',i1)
     else if(i3.eq.0) then
        write(type_text,1006) i3,n3
1006    format(i1,'.',i1)
     else
        write(type_text,1007) i3
1007    format(i1)
     endif

     if(msg.eq.msg0(1:37)) then
        write(*,1004) trim(type_text),'OK',msg0(1:37),msg
     else
        ndifferent=ndifferent+1
        write(*,1004) trim(type_text),'DIFF',msg0(1:37),msg
     endif
1004 format(a5,1x,a11,1x,a37,1x,a)
     if(nargs.eq.1) exit
  enddo

999 if(nargs.eq.2) then
     write(*,1010) nencoded,nrejected,nunpack_failed,ndifferent
1010 format(/'Summary: ',i0,' encoded, ',i0,' rejected, ',i0, &
          ' unpack failures, ',i0,' differences')
  endif

  if(nrejected.gt.0 .or. nunpack_failed.gt.0 .or. ndifferent.gt.0) stop 1

contains

  character(len=40) function pack77_status_text(status) result(text)
    integer, intent(in) :: status

    select case(status)
    case(PACK77_STATUS_NOT_ENCODED)
       text='not encoded'
    case(PACK77_STATUS_FREE_TEXT_TOO_LONG)
       text='free text too long'
    case(PACK77_STATUS_FREE_TEXT_INVALID)
       text='invalid free text'
    case(PACK77_STATUS_PREFERRED_FAMILY_REJECTED)
       text='preferred message family rejected'
    case(PACK77_STATUS_INTERNAL_ROUNDTRIP_REJECTED)
       text='internal round-trip check failed'
    case default
       write(text,'(a,i0)') 'unknown status ',status
    end select
  end function pack77_status_text

end program encode77

include '../chkcall.f90'
