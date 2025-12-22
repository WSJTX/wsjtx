subroutine jtmsg(msg,iflag)

! Attempts to identify false decodes in JT-style messages.

! Returns iflag with sum of bits as follows:
! ------------------------------------------
!    1 Grid/Report invalid
!    2 Second callsign invalid
!    4 First callsign invalid
!    8 Very unlikely free text
!   16 Questionable free text
!    0 Message is probably OK
! ------------------------------------------

  character*22 msg,t
  character*13 w1,w2,w3,w
  character*6 bc1,bc2,bc3
  character*1 c
  logical c1ok,c2ok,c3ok,isdigit,isletter,isgrid4

! Statement functions  
  isdigit(c)=(ichar(c).ge.ichar('0')) .and. (ichar(c).le.ichar('9'))
  isletter(c)=(ichar(c).ge.ichar('A')) .and. (ichar(c).le.ichar('Z'))
  isgrid4(w)=(len_trim(w).eq.4 .and.                                        &
       ichar(w(1:1)).ge.ichar('A') .and. ichar(w(1:1)).le.ichar('R') .and.  &
       ichar(w(2:2)).ge.ichar('A') .and. ichar(w(2:2)).le.ichar('R') .and.  &
       ichar(w(3:3)).ge.ichar('0') .and. ichar(w(3:3)).le.ichar('9') .and.  &
       ichar(w(4:4)).ge.ichar('0') .and. ichar(w(4:4)).le.ichar('9'))

  t=trim(msg)                         !Temporary copy of msg
  nt=len_trim(t)

! Check for standard messages
! Insert underscore in "CQ AA " to "CQ ZZ ", "CQ nnn " to make them one word.
  if(t(1:3).eq.'CQ ' .and. isletter(t(4:4)) .and.                             &
       isletter(t(5:5)) .and. t(6:6).eq.' ') t(3:3)='_'
  if(t(1:3).eq.'CQ ' .and. isdigit(t(4:4)) .and.                              &
       isdigit(t(5:5)) .and. isdigit(t(6:6)) .and. t(7:7).eq.' ') t(3:3)='_'

! Parse first three words
  w1='             '
  w2='             '
  w3='             '
  i1=index(t,' ')
  if(i1.gt.0) w1(1:i1-1)=t(1:i1-1)
  t=t(i1+1:)
  i2=index(t,' ')
  if(i2.gt.0) w2(1:i2-1)=t(1:i2-1)
  t=t(i2+1:)
  i3=index(t,' ')
  if(i3.gt.0) w3(1:i3-1)=t(1:i3-1)

  if(w1(1:3).eq.'CQ ' .or. w1(1:3).eq.'CQ_' .or. w1(1:3).eq.'DE ' .or.   &
       w1(1:4).eq.'QRZ ') then
! CQ/DE/QRZ: Should have one good callsign in w2 and maybe a grid/rpt in w3
     call chkcall(w2,bc2,c2ok)
     iflag=0
     if(.not.c2ok) iflag=iflag+2
     if(len_trim(w3).ne.0 .and. (.not.isgrid4(w3))) iflag=iflag+1
     if(w1(1:3).eq.'DE ' .and. c2ok) iflag=0
     if(iflag.eq.0) return
  endif

! Check for two calls and maybe a grid, rpt, R+rpr, RRR, or 73
  iflag=0
  call chkcall(w1,bc1,c1ok)
  call chkcall(w2,bc2,c2ok)
  if(.not.c1ok) iflag=iflag+4
  if(.not.c2ok) iflag=iflag+2
  if(len_trim(w3).ne.0 .and. (.not.isgrid4(w3)) .and.                  &
          w3(1:1).ne.'+' .and. w3(1:1).ne.'-' .and.                      &
          w3(1:2).ne.'R+' .and. w3(1:2).ne.'R-' .and.                    &
          w3(1:3).ne.'73 ' .and. w3(1:4).ne.'RRR ') iflag=iflag+1
  call chkcall(w3,bc3,c3ok)
! Allow(?) non-standard messages of the form CQ AS OC K1JT
  if(w1(1:3).eq.'CQ_'.and.isletter(w2(1:1)).and.isletter(w2(2:2)).and.   &
     w2(3:3).eq.' '.and.c3ok) iflag=0
  if(iflag.eq.0 .or. nt.gt.13) return

! Check for plausible free text

  nc=0
  np=0
  do i=1,13
     c=msg(i:i)
     if(c.ne.' ') nc=nc+1             !Number of non-blank characters
     if(c.eq.'+') np=np+1             !Number of punctuation characters
     if(c.eq.'-') np=np+1
     if(c.eq.'.') np=np+1
     if(c.eq.'/') np=np+1
     if(c.eq.'?') np=np+1
  enddo
  nb=13-nc                            !Number of blanks
  iflag=16                             !Mark as potentially questionable
  if(nc.ge.12 .or. (nc.ge.11 .and. np.gt.0)) then
     iflag=8                           !Unlikely free text, flag it
  endif

! Save messages containing some common words
  if(msg(1:3).eq.'CQ ') iflag=0
  if(index(msg,'DE ').gt.0) iflag=0
  if(index(msg,'TU ').gt.0) iflag=0
  if(index(msg,' TU').gt.0) iflag=0
  if(index(msg,'73 ').gt.0) iflag=0
  if(index(msg,' 73').gt.0) iflag=0
  if(index(msg,'TNX').gt.0) iflag=0
  if(index(msg,'THX').gt.0) iflag=0
  if(index(msg,'EQSL').gt.0) iflag=0
  if(index(msg,'LOTW').gt.0) iflag=0
  if(index(msg,'DECOD').gt.0) iflag=0
  if(index(msg,'CHK').gt.0) iflag=0
  if(index(msg,'CLK').gt.0) iflag=0
  if(index(msg,'CLOCK').gt.0) iflag=0
  if(index(msg,'LOG').gt.0) iflag=0
  if(index(msg,'QRM').gt.0) iflag=0
  if(index(msg,'QSY').gt.0) iflag=0
  if(index(msg,'TEST').gt.0) iflag=0
  if(index(msg,'CQDX').gt.0) iflag=0
  if(index(msg,'CALL').gt.0) iflag=0
  if(index(msg,'QRZ').gt.0) iflag=0
  if(index(msg,'AUTO').gt.0) iflag=0
  if(index(msg,'PHOTO').gt.0) iflag=0
  if(index(msg,'HYBRID').gt.0) iflag=0

  if(c1ok .and. w1(1:6).eq.bc1) iflag=0
  if(c2ok .and. w2(1:6).eq.bc2) iflag=0

  if(nb.ge.4) iflag=0

  return
end subroutine jtmsg
