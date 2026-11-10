subroutine cwfilter(first,knowledge,render_legacy)

  use packjt77, only: pack77_state
  use ft8_codec_context, only: get_ft8_codec_state
  use ft8var_codec_context, only: genft8sdvar_for_state

  use ft8_mod1, only : cw,windowc1,windowx,pivalue,facx,mcq,m73,mrrr,one,twopi,facc1,dt,csync,idtone25,csynccq, &
                       idtone25_valid, &
                       NFILT1,NFILT2,endcorr,ctwkw,ctwkn,ctwk256
  use jt65_mod9, only: load_callsign_database, callsign_database_available
  use prog_args ! path to files
  use streaming_emit, only: streaming_emit_enabled
  use, intrinsic :: iso_fortran_env, only: error_unit

  parameter (NFFT=180000)
  complex, allocatable :: csig0(:)
  real*4 window1(-NFILT1/2:NFILT1/2)
  character*37 msgcq25(25),msgsent37
  integer itone(79)
  integer*1 msgbits(77)
  logical, intent(in) :: first
  type(pack77_state), target, optional, intent(inout) :: knowledge
  logical, optional, intent(in) :: render_legacy
  logical :: emit_line
  logical, save :: process_initialized=.false.
  type(pack77_state), pointer :: codec

  codec => get_ft8_codec_state()
  if(present(knowledge)) codec => knowledge
  emit_line=.true.
  if(present(render_legacy)) emit_line=render_legacy

  if(first) then
    allocate(csig0(151680))
    if(.not.process_initialized) then
      call load_callsign_database(trim(share_dir)//'/ALLCALL7.TXT')
      if(.not.callsign_database_available()) then
        ! The stream's stdout carries only JSON events.
        if(emit_line .and. streaming_emit_enabled()) then
          write(error_unit,'(a)') 'ALLCALL7.TXT is too short or broken?'
        else if(emit_line) then
          write(*,4) 'ALLCALL7.TXT is too short or broken?','d'
4       format(a36,13x,a1)
          call flush(6)
        endif
      endif
    endif

    pivalue=4.d0*atan(1.d0)
    twopi=8.d0*atan(1.d0)
    dt=1.d0/12000.d0

    do i=0,54; windowc1(i)=(1.0+cos(i*pivalue/55))/2; enddo
    facx=1.0/300.0
    do i=0,200
      windowx(i)=(1.0+cos(i*pivalue/200))/2
    enddo
    windowx=facx*windowx
    facc1=0.01/sqrt(61440.) ! 1.0/sqrt(192000.*3200.) NFFT1*NFFT2

    if(.not.process_initialized) then
      mcq=2*mcq-1
      mrrr=2*mrrr-1
      m73=2*m73-1
    endif

    one=.false.
    do i=0,511
      do j=0,8
        if(iand(i,2**j).ne.0) one(i,j)=.true.
      enddo
    enddo

    msgcq25=''
    msgcq25(2)='CQ 2E0DLA IO92'
    msgcq25(3)='CQ BH3NEB ON81'
    msgcq25(4)='CQ CG3CGT FN04'
    msgcq25(5)='CQ DX CT1JA IM59'
    msgcq25(6)='CQ CU20E'
    msgcq25(7)='CQ NA CX1OB GF14'
    msgcq25(8)='CQ DF2AJ JN49'
    msgcq25(9)='CQ DG4XPZ JN58'
    msgcq25(10)='CQ EA8XR IL19'
    msgcq25(11)='CQ F1YE IN94'
    msgcq25(12)='CQ DX G1KLN IO82'
    msgcq25(13)='CQ HL2KVF PM38'
    msgcq25(14)='CQ IU1ZSV JN45'
    msgcq25(15)='CQ JG1TWO PM96'
    msgcq25(16)='CQ K2ST EM96'
    msgcq25(17)='CQ N9TUX EL98'
    msgcq25(18)='CQ SA NO2FA FM18'
    msgcq25(19)='CQ OH6GKE KP13'
    msgcq25(20)='CQ PD0ORM JO24'
    msgcq25(21)='CQ DX PT7DS HI06'
    msgcq25(22)='CQ RA3XEP KO84'
    msgcq25(23)='CQ SM2GSH KP05'
    msgcq25(24)='CQ UA9OHX NO15'
    msgcq25(25)='CQ JA W0YH EN12'
    idtone25(2:25,1:58)=0
    idtone25_valid(2:25)=.false.
    do i=2,25
      i3=-1; n3=-1
      call genft8sdvar_for_state(codec,msgcq25(i),i3,n3,msgsent37,msgbits,itone)
      if(i3.lt.0) cycle
      if(i.eq.2) then
        call gen_ft8wave(itone,79,1920,2.0,12000.0,0.0,csig0,xjunk,1,151680)
        m=1
        do j=0,14
          do k=1,32
            if(j.lt.7) then; csync(j,k)=csig0(m); else; csynccq(j-7,k)=csig0(m); endif
            m=m+60
          enddo
        enddo
      endif
      idtone25(i,1:29)=itone(8:36)
      idtone25(i,30:58)=itone(44:72)
      idtone25_valid(i)=.true.
    enddo
    dt2=0.005 ! fs2=200 Hz
    k=1
    do ifr=-5,5                              !Search over +/- 2.5 Hz
      delf=ifr*0.5
      dphi=twopi*delf*dt2
      phi=0.0
      do i=1,32
        ctwkw(k,i)=cmplx(cos(phi),sin(phi))
        phi=mod(phi+dphi,twopi)
      enddo
      k=k+1
    enddo
    k=1
    do ifr=-5,5                              !Search over +/- 1.25 Hz
      delf=ifr*0.25
      dphi=twopi*delf*dt2
      phi=0.0
      do i=1,32
        ctwkn(k,i)=cmplx(cos(phi),sin(phi))
        phi=mod(phi+dphi,twopi)
      enddo
      k=k+1
    enddo
! lcqsignal
    delf=3.125
    dphi=twopi*delf*dt2
    phi=0.0
    do i=1,256
      ctwk256(i)=cmplx(cos(phi),sin(phi))
      phi=mod(phi+dphi,twopi)
    enddo
    process_initialized=.true.
  endif

! this filter is being used for signal subtraction
! Create and normalize the filter
  if(first) then
    fac=1.0/float(nfft)
    sumw=0.0
    do j=-NFILT1/2,NFILT1/2
       window1(j)=cos(pivalue*j/NFILT1)**2
       sumw=sumw+window1(j)
    enddo
    cw=0.
    do j=-NFILT1/2,NFILT1/2
      cw(modulo(j-1,NFFT)+1)=window1(j)/sumw
    enddo
    do j=1,NFILT1/2+1
      endcorr(j)=1.0/(1.0-sum(window1(j-1:NFILT1/2))/sumw)
    enddo
    call four2avar(cw,NFFT,1,-1,1)
    cw=cw*fac
  endif

  return  
end subroutine cwfilter
