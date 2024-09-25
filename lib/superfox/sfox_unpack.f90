subroutine sfox_unpack(nutc,x,nsnr,f0,dt0,foxcall,notp)

  use packjt77
  parameter (NQU1RKS=203514677)
  integer*1 x(0:49)
  integer*8 n58
  logical success
  character*336 msgbits
  character*22 msg(10)            !### only msg(1) is used ??? ###
  character*13 foxcall,c13
  character*10 ssignature
  character*4 crpt(5),grid4
  character*26 freeTextMsg
  character*38 c
  logical use_otp
  data c/' 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/'/

  ncq=0
  if (notp.eq.0) then
     use_otp = .FALSE.
  else
     use_otp = .TRUE.
  endif
  write(msgbits,1000) x(0:46)
1000 format(47b7.7)
  read(msgbits(327:329),'(b6)') i3            !Message type
  read(msgbits(1:28),'(b28)') n28           !Standard Fox call
  call unpack28(n28,foxcall,success)

  if(i3.eq.1) then                            !Compound Fox callsign
!     read(msgbits(87:101),'(b15)') n15
!     call unpackgrid(n15,grid4)
!     msg(1)='CQ '//trim(foxcall)//' '//grid4
!     write(*,1100) nutc,nsnr,dt0,nint(f0),trim(msg(1))
!     go to 100
  else if(i3.eq.2) then                       !Up to 4 Hound calls and free text
     call unpacktext77(msgbits(161:231),freeTextMsg(1:13))
     call unpacktext77(msgbits(232:302),freeTextMsg(14:26))
     do i=26,1,-1
        if(freeTextMsg(i:i).ne.'.') exit
        freeTextMsg(i:i)=' '
     enddo
     write(*,1100) nutc,nsnr,dt0,nint(f0),freeTextMsg
1100 format(i6.6,i4,f5.1,i5,1x,"~",2x,a)
  else if(i3.eq.3) then                       !CQ FoxCall Grid     
     read(msgbits(1:58),'(b58)') n58          !FoxCall
     do i=11,1,-1
        j=mod(n58,38)+1
        foxcall(i:i)=c(j:j)
        n58=n58/38
     enddo
     foxcall(12:13)='  '
     read(msgbits(59:73),'(b15)') n15
     call unpackgrid(n15,grid4)
     msg(1)='CQ '//trim(foxcall)//' '//grid4
     write(*,1100) nutc,nsnr,dt0,nint(f0),trim(msg(1))
     read(msgbits(74:105),'(b32)') n32
     if(n32.eq.NQU1RKS) go to 100
     call unpacktext77(msgbits(74:144),freeTextMsg(1:13))
     call unpacktext77(msgbits(145:215),freeTextMsg(14:26))
     do i=26,1,-1
        if(freeTextMsg(i:i).ne.'.') exit
        freeTextMsg(i:i)=' '
     enddo
     if(len(trim(freeTextMsg)).gt.0) write(*,1100) nutc,nsnr,dt0,&
          nint(f0),freeTextMsg
     go to 100
  endif

  j=281
  iz=4                                         !Max number of reports
  if(i3.eq.2) j=141
  do i=1,iz                                    !Extract the reports
     read(msgbits(j:j+4),'(b5)') n
     if(n.eq.31) then
        crpt(i)='RR73'
     else
        write(crpt(i),1006) n-18
1006    format(i3.2)
        if(crpt(i)(1:1).eq.' ') crpt(i)(1:1)='+'
     endif
     j=j+5
  enddo

! Unpack Hound callsigns and format user-level messages:
  iz=9                                          !Max number of hound calls
  if(i3.eq.2 .or. i3.eq.3) iz=4
  do i=1,iz
     j=28*i + 1
     read(msgbits(j:j+27),'(b28)') n28
     call unpack28(n28,c13,success)
     if(n28.eq.0 .or. n28.eq.NQU1RKS) cycle 
     msg(i)=trim(c13)//' '//trim(foxcall)
     if(msg(i)(1:3).eq.'CQ ') then
        ncq=ncq+1
     else
        if(i3.eq.2) then
           msg(i)=trim(msg(i))//' '//crpt(i)
        else
           if(i.le.5) msg(i)=trim(msg(i))//' RR73'
           if(i.gt.5) msg(i)=trim(msg(i))//' '//crpt(i-5)
        endif
     endif
     if(ncq.le.1 .or. msg(i)(1:3).ne.'CQ ') then
        write(*,1100) nutc,nsnr,dt0,nint(f0),trim(msg(i))
     endif
  enddo

  if(msgbits(306:306).eq.'1' .and. ncq.lt.1) then
     write(*,1100) nutc,nsnr,dt0,nint(f0),'CQ '//foxcall
  endif

100 read(msgbits(307:326),'(b20)') notp
  if (use_otp) then
     write(ssignature,'(I6.6)') notp
     write(*,1100) nutc,nsnr,dt0,nint(f0),'$VERIFY$ '//trim(foxcall)//' '//trim(ssignature)
  endif
  return
end subroutine sfox_unpack
