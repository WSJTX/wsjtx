program chkdec

  parameter(NMAX=100)
  character*88 line
  character*37 msg(NMAX),msg0,msg1
  character*2 c2(NMAX)
  character*1 c1(NMAX)
  character*1 only
  integer nsnr(NMAX,0:1),nf(NMAX,0:1)
  real dt(NMAX,0:1)
  logical found,eof

! These files are sorted by freq within each Rx sequence
  open(10,file='all.wsjtx',status='old')
  open(11,file='all.jtdx',status='old')
  write(20,1030)
1030 format('  iseq    B   w   j   W  W+   J   E       B     w     j     W',  &
          '    W+     J     E'/80('-'))

  nutc0=-1
  nbt=0        !Both
  nwt=0        !WSJT-X only
  njt=0        !JTDX only
  net=0        !Either
  n7t=0        !a7
  eof=.false.
  
  do iseq=1,9999
     j=0
     msg=' '
     nsnr=-99
     nf=-99
     dt=-99
     c1=' '
     c2=' '
     do i=1,NMAX
        read(10,'(a88)',end=8) line           !Read from the WSJT-X file
        if(line(25:30).ne.'Rx FT8') cycle     !Ignore any line not an FT8 decode
        read(line(8:13),*) nutc
        if(nutc0.lt.0) nutc0=nutc             !First time only
        if(nutc.ne.nutc0) then
           backspace(10)
           go to 10                            !Finished WSJT-X for this sequence
        endif
        j=j+1
        if(j.eq.1) then
           nf(j,0)=-1
           j=j+1
        endif
        read(line,1001) nsnr(j,0),dt(j,0),nf(j,0),msg(j),c2(j)
1001    format(30x,i7,f5.1,i5,1x,a36,2x,a2)
!        if(nutc.eq.180215 .and. c2(j).eq.'a7') print*,'aaa',j,nf(j,0),c2(j)
        nutc0=nutc
     enddo  ! i
     
8    eof=.true.
10   jz=j
     do i=1,NMAX
        read(11,'(a88)',end=20) line           !Read from the JTDX file
        if(line(31:31).ne.'~') cycle           !Ignore any line not an FT8 decode
        read(line(10:15),*) nutc
        if(nutc.ne.nutc0) then
           backspace(11)
           go to 20                            !Finished JTDX for this sequence
        endif
        msg1=line(33:58)
        read(line(25:29),*) nf1
        found=.false.
        do j=1,jz
           if(msg(j).eq.msg1) then
              read(line,1002) nsnr(j,1),dt(j,1),nf(j,1),c1(j)
1002          format(15x,i4,f5.1,i5,29x,a1)
              found=.true.
              exit
           endif
           i1=index(msg(j),'<')
           if(i1.gt.0) then
              i2=index(msg(j),'>')
              msg0=msg(j)(1:i1-1)//msg(j)(i1+1:i2-1)//msg(j)(i2+1:)
              if(msg0.eq.msg1) then
                 read(line,1002) nsnr(j,1),dt(j,1),nf(j,1),c1(j)
                 found=.true.
                 exit
              endif
           endif
        enddo  ! j

        if(.not.found) then                    !Insert this one as a new message
           do j=1,jz
              if(nf1.ge.nf(j,0) .and. nf1.lt.nf(j+1,0)) then
                 jj=j+1
                 exit
              endif
           enddo
           do j=jz+1,jj+1,-1
              nsnr(j,0)=nsnr(j-1,0)
              dt(j,0)=dt(j-1,0)
              nf(j,0)=nf(j-1,0)
              msg(j)=msg(j-1)
              c1(j)=c1(j-1)
              c2(j)=c2(j-1)
           enddo  ! j
           read(line,1004) nsnr(jj,1),dt(jj,1),nf(jj,1),msg(jj),c1(jj)
1004       format(15x,i4,f5.1,i5,3x,a26,a1)
           c2(jj)='  '
           nsnr(jj,0)=-99
           dt(jj,0)=-99.0
           nf(jj,0)=-99
           jz=jz+1
        endif
     enddo  ! i

20   nb=0
     nw=0
     nj=0
     ne=0
     n7=0
     do j=2,jz
        write(line,1020) nutc0,j,nsnr(j,:),dt(j,:),nf(j,:),msg(j)(1:26),   &
             c2(j),c1(j)
1020    format(i6.6,i3,1x,2i4,1x,2f6.1,1x,2i5,1x,a26,1x,a2,1x,a1)
        if(c2(j).eq.'a7') n7=n7+1
        only=' '
        if(line(12:14).eq.'-99') then
           line(12:14)='   '
           only='j'
           nj=nj+1
!           if(c2(j).eq.'a7') print*,'aaa ',trim(line)
        endif
        if(line(16:18).eq.'-99') then
           line(16:18)='   '
           only='w'
           nw=nw+1
        endif
        if(line(12:14).ne.'   ' .or. line(16:19).ne.'   ') ne=ne+1
        if(line(12:14).ne.'   ' .and. line(16:19).ne.'   ') nb=nb+1
        if(line(21:25).eq.'-99.0') line(21:25)='     '
        if(line(27:31).eq.'-99.0') line(27:31)='     '
        if(line(35:37).eq.'-99') line(35:37)='   '
        if(line(40:42).eq.'-99') line(40:42)='   '
!        if(line(12:14).ne.'   ') nw=nw+1
!        if(line(16:18).ne.'   ') nj=nj+1
        write(*,'(a74,1x,a1)') line(1:74),only
     enddo  ! j

     nbt=nbt+nb
     nwt=nwt+nw
     n7t=n7t+n7
     njt=njt+nj
     net=net+ne
     nutc0=nutc
     write(*,*)

     write(20,1031) iseq,nb,nw,nj,nb+nw-n7,nb+nw,nb+nj,ne,nbt,nwt,njt,   &
          nbt+nwt-n7t,nbt+nwt,nbt+njt,net
1031 format(i5,2x,7i4,2x,7i6)
     if(eof) exit
!     if(iseq.eq.2) exit
  enddo  ! iseq

end program chkdec
