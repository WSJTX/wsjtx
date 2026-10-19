module decoder_callbacks

  use jt4_decode
  use jt65_decode
  use jt9_decode, only: jt9_decoder
  use ft8_decode
  use ft8_decodevar
  use ft4_decode
  use fst4_decode, only: fst4_decoder
  use q65_decode
  use streaming_emit, only: streaming_emit_enabled, streaming_emit_decode
  use decoder_engine_types
  use jt65_host_support, only: write_jt65_averages
  use, intrinsic :: iso_c_binding, only: c_ptr,c_null_ptr,c_char,c_null_char

  implicit none

  type :: decoder_callback_context
     ! Callback state is stored on each decoder instead of captured by a nested procedure.
     integer :: mode = 0
     integer :: nutc = 0
     integer :: nfqso = 0
     integer :: ncontest = 0
     integer :: submode = 0
     integer :: ios13 = -1
     integer :: ios14 = -1
     logical :: bVHF = .false.
     logical :: b_superfox = .false.
     character(len=12) :: mycall = '            '
     real :: waveform_input_offset_seconds = 0.0
     procedure(engine_observation_sink), pointer, nopass :: sink => null()
     procedure(engine_superfox_sink), pointer, nopass :: superfox_sink => null()
     type(c_ptr) :: sink_user = c_null_ptr
     logical :: render_legacy = .true.
     logical :: utc_is_hhmmss = .false.
  end type decoder_callback_context

  type, extends(jt4_decoder) :: counting_jt4_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_jt4_decoder

  type, extends(jt65_decoder) :: counting_jt65_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_jt65_decoder

  type, extends(jt9_decoder) :: counting_jt9_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_jt9_decoder

  type, extends(ft8_decoder) :: counting_ft8_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
     logical :: first_result = .true.
     integer :: day_wrap = 0
  end type counting_ft8_decoder

  type, extends(ft8_decodervar) :: counting_ft8_decodervar
     type(decoder_callback_context) :: context
     integer :: decodedvar = 0
     real :: xdtt(200)
     logical :: first_result = .true.
     integer :: day_wrap = 0
   contains
     procedure :: timing_summary => ft8_timing_summary
  end type counting_ft8_decodervar

  type, extends(ft4_decoder) :: counting_ft4_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_ft4_decoder

  type, extends(fst4_decoder) :: counting_fst4_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
     logical :: print_hash22 = .false.
  end type counting_fst4_decoder

  type, extends(q65_decoder) :: counting_q65_decoder
     type(decoder_callback_context) :: context
     integer :: decoded = 0
  end type counting_q65_decoder

contains

  subroutine jt4_decoded(this,snr,dt,freq,have_sync,sync,is_deep,    &
       decoded0,qual,ich,is_average,ave)
    implicit none
    class(jt4_decoder), intent(inout) :: this
    integer, intent(in) :: snr
    real, intent(in) :: dt
    integer, intent(in) :: freq
    logical, intent(in) :: have_sync
    logical, intent(in) :: is_deep
    character(len=1), intent(in) :: sync
    character(len=22), intent(in) :: decoded0
    real, intent(in) :: qual
    integer, intent(in) :: ich
    logical, intent(in) :: is_average
    integer, intent(in) :: ave

    character(len=22) :: decoded
    character(len=3) :: cflags
    type(decoder_callback_context) :: context
    type(engine_observation) :: observation
    integer :: i,display_utc

    select type (typed_this => this)
    type is (counting_jt4_decoder)
       context=typed_this%context
    class default
       return
    end select

    observation=engine_observation()
    observation%mode=engine_mode_jt4
    observation%variant=context%submode
    observation%snr_db=snr
    observation%frequency_hz=real(freq)
    observation%dt_seconds=dt
    observation%sync=this%sync_strength
    observation%quality=qual
    if(have_sync) then
       observation%jt4%kind=1
       observation%jt4%sync_polarity=merge(-1,1,sync=='#')
       if(len_trim(decoded0)>0) then
          observation%jt4%kind=2
          observation%jt4%method=merge(2,1,is_deep)
          observation%jt4%width_profile=max(0,ich-1)
          if(is_average) observation%jt4%average_count=ave
          do i=1,len_trim(decoded0)
             observation%message(i)=decoded0(i:i)
          enddo
       endif
    endif
    if(associated(context%sink)) call context%sink(context%sink_user,observation)

    display_utc=context%nutc
    if(context%utc_is_hhmmss) display_utc=display_utc/100
    if(context%render_legacy) then
       if(have_sync) then
          decoded=decoded0
          cflags='   '
          if(len_trim(decoded)>0) then
             cflags='f  '
             if(is_deep) then
                cflags='d  '
                write(cflags(2:2),'(i1)') min(int(qual),9)
                if(qual>=10.0) cflags(2:2)='*'
                if(qual<3.0) decoded(22:22)='?'
             endif
             if(is_average) then
                write(cflags(3:3),'(i1)') min(ave,9)
                if(ave>=10) cflags(3:3)='*'
                if(cflags(1:1)=='f') cflags=cflags(1:1)//cflags(3:3)//' '
             endif
          endif
          if(streaming_emit_enabled()) then
             call streaming_emit_decode('JT4',context%nutc,snr,dt,freq,decoded,context%utc_is_hhmmss)
          else
             write(*,1000) display_utc,snr,dt,freq,sync,decoded,cflags
          endif
       else
          if(.not.streaming_emit_enabled()) write(*,1000) display_utc,snr,dt,freq
       endif
1000   format(i4.4,i4,f5.1,i5,1x,'$',a1,1x,a22,1x,a3)
    endif

    select type (typed_this => this)
    type is (counting_jt4_decoder)
       if(observation%jt4%kind==2) typed_this%decoded=typed_this%decoded+1
    end select
  end subroutine jt4_decoded


  subroutine jt65_decoded(this,sync,snr,dt,freq,drift,nflip,width,     &
       decoded0,ft,qual,nsmo,nsum,minsync)

    implicit none

    class(jt65_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    integer, intent(in) :: freq
    integer, intent(in) :: drift
    integer, intent(in) :: nflip
    real, intent(in) :: width
    character(len=22), intent(in) :: decoded0
    integer, intent(in) :: ft
    integer, intent(in) :: qual
    integer, intent(in) :: nsmo
    integer, intent(in) :: nsum
    integer, intent(in) :: minsync

    integer i,nap,n
    integer context_nutc, context_ios13
    logical context_bVHF
    logical is_deep,is_average
    character decoded*22,csync*2,cflags*3
    type(decoder_callback_context) :: context
    type(engine_observation) :: observation

    select type (typed_this => this)
    type is (counting_jt65_decoder)
       context = typed_this%context
       context_nutc = typed_this%context%nutc
       context_ios13 = typed_this%context%ios13
       context_bVHF = typed_this%context%bVHF
    class default
       return
    end select

    observation=engine_observation()
    observation%mode=engine_mode_jt65
    observation%variant=context%submode
    observation%snr_db=snr
    observation%frequency_hz=real(freq)
    observation%dt_seconds=dt
    observation%sync=sync
    observation%quality=real(qual)
    observation%ap_type=ishft(ft,-2)
    observation%jt65%kind=merge(1,0,len_trim(decoded0)>0)
    observation%jt65%method=iand(ft,3)
    observation%jt65%average_count=nsum
    observation%jt65%sync_polarity=nflip
    observation%jt65%smoothing=nsmo
    observation%jt65%has_width=merge(1,0,context%bVHF)
    if(context%bVHF) observation%jt65%width_hz=width
    observation%jt65%has_drift=merge(1,0,nflip/=0)
    if(nflip/=0) observation%jt65%drift_hz=real(drift)
    do i=1,len_trim(decoded0)
       observation%message(i)=decoded0(i:i)
    enddo
    if(associated(context%sink)) call context%sink(context%sink_user,observation)

    if(context%render_legacy) then
    !$omp critical(decode_results)
    if(context%ios14==0) call write_jt65_averages(this,14)
    decoded=decoded0
    cflags='   '
    is_deep=ft.eq.2

    if(ft.eq.0 .and. minsync.ge.0 .and. int(sync).lt.minsync) then
       if (.not. streaming_emit_enabled()) write(*,1010) context_nutc,snr,dt,freq
    else
       is_average=nsum.ge.2
       if(context_bVHF .and. ft.gt.0) then
          cflags='f  '
          if(is_deep) then
             cflags='d  '
             write(cflags(2:2),'(i1)') min(qual,9)
             if(qual.ge.10) cflags(2:2)='*'
             if(qual.lt.3) decoded(22:22)='?'
          endif
          if(is_average) then
             write(cflags(3:3),'(i1)') min(nsum,9)
             if(nsum.ge.10) cflags(3:3)='*'
          endif
          nap=ishft(ft,-2)
          if(nap.ne.0) then
             if(nsum.lt.2) write(cflags(1:3),'(a1,i1," ")') 'a',nap
             if(nsum.ge.2) write(cflags(1:3),'(a1,2i1)') 'a',nap,min(nsum,9)
          endif
       endif
       csync='# '
       i=0
       if(context_bVHF .and. nflip.ne.0 .and.                         &
            sync.ge.max(0.0,float(minsync))) then
          csync='#*'
          if(nflip.eq.-1) then
             csync='##'
             if(decoded.ne.'                      ') then
                do i=22,1,-1
                   if(decoded(i:i).ne.' ') exit
                enddo
                if(i.gt.18) i=18
                decoded(i+2:i+4)='OOO'
             endif
          endif
       endif
       n=len(trim(decoded))
       if(n.eq.2 .or. n.eq.3) csync='# '
       if(cflags(1:1).eq.'f') then
          cflags(2:2)=cflags(3:3)
          cflags(3:3)=' '
       endif
       if (streaming_emit_enabled()) then
          call streaming_emit_decode("JT65", context_nutc, snr, dt, freq, decoded)
       else
          write(*,1010) context_nutc,snr,dt,freq,csync,decoded,cflags
       end if
1010   format(i4.4,i4,f5.1,i5,1x,a2,1x,a22,1x,a3)
    endif
    if(context_ios13.eq.0) write(13,1012) context_nutc,nint(sync),snr,dt,    &
         float(freq),drift,decoded,ft,nsum,nsmo
1012 format(i4.4,i4,i5,f6.2,f8.0,i4,3x,a22,' JT65',3i3)
    call flush(6)

    !$omp end critical(decode_results)
    endif
    select type (typed_this => this)
    type is (counting_jt65_decoder)
       if(len_trim(decoded0)>0) typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine jt65_decoded

  subroutine jt9_decoded (this, sync, snr, dt, freq, drift, decoded)
    implicit none

    class(jt9_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    integer, intent(in) :: drift
    character(len=22), intent(in) :: decoded
    type(engine_observation) :: observation
    type(decoder_callback_context) :: context
    integer context_nutc, context_ios13,i

    select type (typed_this => this)
    type is (counting_jt9_decoder)
       context = typed_this%context
       context_nutc = typed_this%context%nutc
       context_ios13 = typed_this%context%ios13
    class default
       return
    end select

    observation=engine_observation()
    observation%mode=engine_mode_jt9
    observation%variant=context%submode
    observation%snr_db=snr
    observation%frequency_hz=freq
    observation%dt_seconds=dt
    observation%sync=sync
    if(context%submode==0) then
       observation%jt9%has_drift=1
       observation%jt9%drift_hz_per_minute=real(drift)*(12000.0/16384.0)*60.0/48.96
    endif
    do i=1,len_trim(decoded)
       observation%message(i)=decoded(i:i)
    enddo
    if(associated(context%sink)) call context%sink(context%sink_user,observation)

    if(context%render_legacy) then
    !$omp critical(decode_results)

    if (streaming_emit_enabled()) then
       call streaming_emit_decode("JT9", context_nutc, snr, dt, nint(freq), decoded)
    else
       write(*,1000) context_nutc,snr,dt,nint(freq),decoded
    end if
1000 format(i4.4,i4,f5.1,i5,1x,'@ ',1x,a22)
    if(context_ios13.eq.0) write(13,1002) context_nutc,nint(sync),snr,dt,freq,  &
         drift,decoded
1002 format(i4.4,i4,i5,f6.1,f8.0,i4,3x,a22,' JT9')
    call flush(6)
    !$omp end critical(decode_results)
    endif
    select type (typed_this => this)
    type is (counting_jt9_decoder)
       typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine jt9_decoded

  subroutine ft8_timing_summary(this,count,sumdt)
    class(counting_ft8_decodervar), intent(in) :: this
    integer, intent(out) :: count
    real, intent(out) :: sumdt
    real :: dtmed
    integer :: i

    count=min(this%decodedvar,size(this%xdtt))
    sumdt=0.0
    dtmed=0.0
    if(count.le.2) then
       sumdt=sum(this%xdtt(1:count))
       return
    endif
    do i=1,count
       if(i.lt.count-1) then
          if((this%xdtt(i).gt.this%xdtt(i+1) .and. &
               this%xdtt(i).lt.this%xdtt(i+2)) .or. &
               (this%xdtt(i).lt.this%xdtt(i+1) .and. &
               this%xdtt(i).gt.this%xdtt(i+2))) then
             dtmed=this%xdtt(i)
          else if((this%xdtt(i+1).gt.this%xdtt(i) .and. &
               this%xdtt(i+1).lt.this%xdtt(i+2)) .or. &
               (this%xdtt(i+1).lt.this%xdtt(i) .and. &
               this%xdtt(i+1).gt.this%xdtt(i+2))) then
             dtmed=this%xdtt(i+1)
          else if((this%xdtt(i+2).gt.this%xdtt(i) .and. &
               this%xdtt(i+2).lt.this%xdtt(i+1)) .or. &
               (this%xdtt(i+2).lt.this%xdtt(i) .and. &
               this%xdtt(i+2).gt.this%xdtt(i+1))) then
             dtmed=this%xdtt(i+2)
          else
             dtmed=this%xdtt(i)
          endif
       endif
       ! Repeat the final median for the two trailing timing samples.
       sumdt=sumdt+dtmed
    enddo
  end subroutine ft8_timing_summary

  subroutine ft8_decodedvar  (this,snr,dt,freq,decodedvar,nap,qual,evidence)
    implicit none
    type(ft8_signal_evidence), optional, intent(in) :: evidence
    type(engine_observation) :: observation
    type(decoder_callback_context) :: context
    integer :: message_index

    class(ft8_decodervar), intent(inout) :: this
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    real, intent(in) :: qual
    character(len=37), intent(in) :: decodedvar
    integer, intent(in) :: nap
    real fdiff
    character*2 annot
    character c1*12,c2*12,g2*4,w*4
    integer i1,i2,i3,i4,i5,n,n30,nwrap
    character*37 decoded0
    logical isgrid4,first,b0,b1,b2
    integer context_nutc, context_nfqso, context_ncontest, context_ios13
    logical context_b_superfox
    character(len=12) context_mycall

    isgrid4(w)=(len_trim(w).eq.4 .and.                                        &
         ichar(w(1:1)).ge.ichar('A') .and. ichar(w(1:1)).le.ichar('R') .and.  &
         ichar(w(2:2)).ge.ichar('A') .and. ichar(w(2:2)).le.ichar('R') .and.  &
         ichar(w(3:3)).ge.ichar('0') .and. ichar(w(3:3)).le.ichar('9') .and.  &
         ichar(w(4:4)).ge.ichar('0') .and. ichar(w(4:4)).le.ichar('9'))

    select type (typed_this => this)
    type is (counting_ft8_decodervar)
       context=typed_this%context
       first=typed_this%first_result
       nwrap=typed_this%day_wrap
       context_nutc = typed_this%context%nutc
       context_nfqso = typed_this%context%nfqso
       context_ncontest = typed_this%context%ncontest
       context_ios13 = typed_this%context%ios13
       context_b_superfox = typed_this%context%b_superfox
       context_mycall = typed_this%context%mycall
    class default
       return
    end select

    if(first) then
       c2fox='            '
       g2fox='    '
       nsnrfox=-99
       nfreqfox=-99
       n30z=0
       nwrap=0
       nfox=0
       first=.false.
    endif

    fdiff=freq-context_nfqso
    if(abs(fdiff).lt.3.0) ltry_a8=.false.

    decoded0=decodedvar

    annot='  '
    if(nap.ne.0) then
       if(nap.ge.9) then
          write(annot,'(a1,i1)') 'a',9
       else
          write(annot,'(a1,i1)') 'a',nap
       endif
       if(qual.lt.0.17) decoded0(37:37)='?'
    endif

    if(context%render_legacy) then
    if (streaming_emit_enabled()) then
       call streaming_emit_decode("FT8", context_nutc, snr, dt, nint(freq), decoded0)
    else
       write(*,1000) context_nutc,snr,dt,nint(freq),decoded0,annot
    end if
1000 format(i6.6,i4,f5.1,i5,' ~ ',1x,a37,1x,a2)

    endif

    if(context_ncontest.eq.6) then
       i1=index(decoded0,' ')
       i2=i1 + index(decoded0(i1+1:),' ')
       i3=i2 + index(decoded0(i2+1:),' ')
       if(i1.ge.3 .and. i2.ge.7 .and. i3.ge.10) then
          c1=decoded0(1:i1-1)//'            '
          c2=decoded0(i1+1:i2-1)
          g2=decoded0(i2+1:i3-1)
          b0=c1.eq.context_mycall
          if(c1(1:3).eq.'DE ' .and. index(c2,'/').ge.2) b0=.true.
          if(len(trim(c1)).ne.len(trim(context_mycall))) then
             i4=index(trim(c1),trim(context_mycall))
             i5=index(trim(context_mycall),trim(c1))
             if(i4.ge.1 .or. i5.ge.1) b0=.true.
          endif
          b1=i3-i2.eq.5 .and. isgrid4(g2)
          b2=i3-i2.eq.1
          if(b0 .and. (b1.or.b2) .and. (nint(freq).ge.1000 .or. &
               context_b_superfox)) then
             n=context_nutc
             n30=(3600*(n/10000) + 60*mod((n/100),100) + mod(n,100))/30
             if(n30.lt.n30z) nwrap=nwrap+2880
             n30z=n30
             n30=n30+nwrap
             if(nfox.lt.MAXFOX) nfox=nfox+1
             c2fox(nfox)=c2
             g2fox(nfox)=g2
             nsnrfox(nfox)=snr
             nfreqfox(nfox)=nint(freq)
             n30fox(nfox)=n30
          endif
       endif
    endif
    if(context%render_legacy) then
       call flush(6)
       if(context_ios13.eq.0) call flush(13)
    endif

    observation%variant=1
    observation%snr_db=snr
    observation%frequency_hz=freq
    observation%dt_seconds=dt
    observation%ap_type=nap
    observation%quality=qual
    do message_index=1,min(len_trim(decodedvar),37)
       observation%message(message_index)=decodedvar(message_index:message_index)
    enddo
    if(present(evidence)) then
       observation%ft8=evidence
       if(evidence%has_waveform_start/=0) observation%ft8%waveform_start_seconds= &
            evidence%waveform_start_seconds+context%waveform_input_offset_seconds
    endif
    if(associated(context%sink)) call context%sink(context%sink_user,observation)

    select type (typed_this => this)
    type is (counting_ft8_decodervar)
       typed_this%first_result=first
       typed_this%day_wrap=nwrap
       typed_this%decodedvar = typed_this%decodedvar + 1
       if(typed_this%decodedvar.le.size(typed_this%xdtt)) typed_this%xdtt(typed_this%decodedvar)=dt
    end select
  end subroutine ft8_decodedvar

  subroutine ft8_decoded (this,sync,snr,dt,freq,decoded,nap,qual,evidence)
    implicit none
    type(ft8_signal_evidence), optional, intent(in) :: evidence
    type(engine_observation) :: observation
    type(decoder_callback_context) :: context
    integer :: message_index

    class(ft8_decoder), intent(inout) :: this
    real, intent(in) :: sync
    integer, intent(in) :: snr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    character c1*12,c2*12,g2*4,w*4
    integer i0,i1,i2,i3,i4,i5,n,n30,nwrap
    integer, intent(in) :: nap
    real, intent(in) :: qual
    real fdiff
    character*2 annot
    character*37 decoded0
    logical isgrid4,first,b0,b1,b2
    integer context_nutc, context_nfqso, context_ncontest, context_ios13
    logical context_b_superfox
    character(len=12) context_mycall

    isgrid4(w)=(len_trim(w).eq.4 .and.                                        &
         ichar(w(1:1)).ge.ichar('A') .and. ichar(w(1:1)).le.ichar('R') .and.  &
         ichar(w(2:2)).ge.ichar('A') .and. ichar(w(2:2)).le.ichar('R') .and.  &
         ichar(w(3:3)).ge.ichar('0') .and. ichar(w(3:3)).le.ichar('9') .and.  &
         ichar(w(4:4)).ge.ichar('0') .and. ichar(w(4:4)).le.ichar('9'))

    select type (typed_this => this)
    type is (counting_ft8_decoder)
       context=typed_this%context
       first=typed_this%first_result
       nwrap=typed_this%day_wrap
       context_nutc = typed_this%context%nutc
       context_nfqso = typed_this%context%nfqso
       context_ncontest = typed_this%context%ncontest
       context_ios13 = typed_this%context%ios13
       context_b_superfox = typed_this%context%b_superfox
       context_mycall = typed_this%context%mycall
    class default
       return
    end select

    if(first) then
       c2fox='            '
       g2fox='    '
       nsnrfox=-99
       nfreqfox=-99
       n30z=0
       nwrap=0
       nfox=0
       first=.false.
    endif

    decoded0=decoded
    fdiff=freq-context_nfqso
    if(abs(fdiff).lt.3.0) ltry_a8=.false.

    annot='  '
    if(nap.ne.0) then
       write(annot,'(a1,i1)') 'a',nap
       if(qual.lt.0.17) decoded0(37:37)='?'
    endif

    i0=1
    if(context%render_legacy) then
    if (streaming_emit_enabled()) then
       if (i0.le.0) call streaming_emit_decode("FT8", context_nutc, snr, dt, nint(freq), decoded0(1:22))
       if (i0.gt.0) call streaming_emit_decode("FT8", context_nutc, snr, dt, nint(freq), decoded0)
    else
       if(i0.le.0) write(*,1000) context_nutc,snr,dt,nint(freq),decoded0(1:22),annot
       if(i0.gt.0) write(*,1001) context_nutc,snr,dt,nint(freq),decoded0,annot
    end if
1000 format(i6.6,i4,f5.1,i5,' ~ ',1x,a22,1x,a2)
1001 format(i6.6,i4,f5.1,i5,' ~ ',1x,a37,1x,a2)
    if(context_ios13.eq.0) write(13,1002) context_nutc,nint(sync),snr,dt,freq,0,decoded0
1002 format(i6.6,i4,i5,f6.1,f8.0,i4,3x,a37,' FT8')

    endif

    if(context_ncontest.eq.6) then
       i1=index(decoded0,' ')
       i2=i1 + index(decoded0(i1+1:),' ')
       i3=i2 + index(decoded0(i2+1:),' ')
       if(i1.ge.3 .and. i2.ge.7 .and. i3.ge.10) then
          c1=decoded0(1:i1-1)//'            '
          c2=decoded0(i1+1:i2-1)
          g2=decoded0(i2+1:i3-1)
          b0=c1.eq.context_mycall
          if(c1(1:3).eq.'DE ' .and. index(c2,'/').ge.2) b0=.true.
          if(len(trim(c1)).ne.len(trim(context_mycall))) then
             i4=index(trim(c1),trim(context_mycall))
             i5=index(trim(context_mycall),trim(c1))
             if(i4.ge.1 .or. i5.ge.1) b0=.true.
          endif
          b1=i3-i2.eq.5 .and. isgrid4(g2)
          b2=i3-i2.eq.1
          if(b0 .and. (b1.or.b2) .and. (nint(freq).ge.1000 .or. &
               context_b_superfox)) then
             n=context_nutc
             n30=(3600*(n/10000) + 60*mod((n/100),100) + mod(n,100))/30
             if(n30.lt.n30z) nwrap=nwrap+2880
             n30z=n30
             n30=n30+nwrap
             if(nfox.lt.MAXFOX) nfox=nfox+1
             c2fox(nfox)=c2
             g2fox(nfox)=g2
             nsnrfox(nfox)=snr
             nfreqfox(nfox)=nint(freq)
             n30fox(nfox)=n30
          endif
       endif
    endif

    if(context%render_legacy) then
       call flush(6)
       if(context_ios13.eq.0) call flush(13)
    endif

    observation%variant=0
    observation%snr_db=snr
    observation%frequency_hz=freq
    observation%dt_seconds=dt
    observation%ap_type=nap
    observation%quality=qual
    observation%sync=sync
    do message_index=1,min(len_trim(decoded),37)
       observation%message(message_index)=decoded(message_index:message_index)
    enddo
    if(present(evidence)) then
       observation%ft8=evidence
       if(evidence%has_waveform_start/=0) observation%ft8%waveform_start_seconds= &
            evidence%waveform_start_seconds+context%waveform_input_offset_seconds
    endif
    if(associated(context%sink)) call context%sink(context%sink_user,observation)

    select type (typed_this => this)
    type is (counting_ft8_decoder)
       typed_this%first_result=first
       typed_this%day_wrap=nwrap
       typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine ft8_decoded

  subroutine ft4_decoded (this,sync,snr,dt,freq,decoded,nap,qual,evidence)
    implicit none

    class(ft4_decoder), intent(inout) :: this
    type(ft4_signal_evidence), intent(in) :: evidence
    type(engine_observation) :: observation
    type(decoder_callback_context) :: context
    real, intent(in) :: sync,dt,freq,qual
    integer, intent(in) :: snr,nap
    character(len=37), intent(in) :: decoded
    character*2 annot
    character*37 decoded0
    integer context_nutc, context_ios13,i

    select type (typed_this => this)
    type is (counting_ft4_decoder)
       context=typed_this%context
       context_nutc = typed_this%context%nutc
       context_ios13 = typed_this%context%ios13
    class default
       return
    end select

    observation=engine_observation()
    observation%mode=engine_mode_ft4
    observation%snr_db=snr
    observation%ap_type=nap
    observation%dt_seconds=dt
    observation%frequency_hz=freq
    observation%sync=sync
    observation%quality=qual
    observation%ft4=evidence
    do i=1,len_trim(decoded)
       observation%message(i)=decoded(i:i)
    enddo
    if(associated(context%sink)) call context%sink(context%sink_user,observation)
    if(.not.context%render_legacy) go to 20

    decoded0=decoded

    annot='  '
    if(nap.ne.0) then
       write(annot,'(a1,i1)') 'a',nap
       if(qual.lt.0.17) decoded0(37:37)='?'
    endif

    if (streaming_emit_enabled()) then
       call streaming_emit_decode("FT4", context_nutc, snr, dt, nint(freq), decoded0)
    else
       write(*,1001) context_nutc,snr,dt,nint(freq),decoded0,annot
    end if
1001 format(i6.6,i4,f5.1,i5,' + ',1x,a37,1x,a2)

    if(context_ios13.eq.0) then
       write(13,1002,err=10) context_nutc,nint(sync),snr,dt,freq,0,decoded0
1002   format(i6.6,i4,i5,f6.1,f8.0,i4,3x,a37,' FT4')
       flush(13)
    endif

10  call flush(6)

20  continue
    select type (typed_this => this)
    type is (counting_ft4_decoder)
       typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine ft4_decoded

  subroutine fst4_decoded (this,nutc,sync,nsnr,dt,freq,decoded,nap,   &
       qual,ntrperiod,fmid,w50,result)

    implicit none

    class(fst4_decoder), intent(inout) :: this
    integer, intent(in) :: nutc
    real, intent(in) :: sync
    integer, intent(in) :: nsnr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: nap
    real, intent(in) :: qual
    integer, intent(in) :: ntrperiod
    real, intent(in) :: fmid
    real, intent(in) :: w50
    type(fst4_result), intent(in), optional :: result
    type(engine_observation) :: observation
    type(decoder_callback_context) :: context
    integer :: display_utc,message_index,space
    logical :: print_hash22

    character*2 annot
    character*37 decoded0
    character*37 suffix
    character*70 line
    integer context_ios13

    select type (typed_this => this)
    type is (counting_fst4_decoder)
       context=typed_this%context
       context_ios13 = context%ios13
       print_hash22=typed_this%print_hash22
    class default
       return
    end select

    display_utc=nutc
    if(context%utc_is_hhmmss.and.ntrperiod>=60) display_utc=nutc/100
    decoded0=decoded
    annot='  '
    if(nap.ne.0) then
       write(annot,'(a1,i1)') 'a',nap
       if(qual.lt.0.17) decoded0(37:37)='?'
    endif

    if(context%render_legacy) then
    if(present(result)) then
       if(print_hash22.and.result%has_hash22/=0) then
          space=index(decoded0,' ')
          suffix=decoded0(space+1:)
          write(decoded0,'(a1,i7.7,a1)') '<',result%hash22,'>'
          decoded0=trim(decoded0)//' '//trim(suffix)
       endif
    endif
    if(ntrperiod.lt.60) then
       write(line,1001) display_utc,nsnr,dt,nint(freq),decoded0,annot
1001   format(i6.6,i4,f5.1,i5,' ` ',1x,a37,1x,a2)
       if(context_ios13.eq.0) write(13,1002) display_utc,nint(sync),nsnr,dt,freq,0,decoded0
1002   format(i6.6,i4,i5,f6.1,f8.0,i4,3x,a37,' FST4')
    else
       write(line,1003) display_utc,nsnr,dt,nint(freq),decoded0,annot
1003   format(i4.4,i4,f5.1,i5,' ` ',1x,a37,1x,a2,2f7.3)
       if(context_ios13.eq.0) write(13,1004) display_utc,nint(sync),nsnr,dt,freq,0,decoded0
1004   format(i4.4,i4,i5,f6.1,f8.0,i4,3x,a37,' FST4')
    endif

    if(fmid.ne.-999.0) then
       if(w50.lt.0.95) write(line(65:70),'(f6.3)') w50
       if(w50.ge.0.95) write(line(65:70),'(f6.2)') w50
    endif

    if (streaming_emit_enabled()) then
       call streaming_emit_decode(merge('FST4W','FST4 ',context%mode==engine_mode_fst4w), &
            nutc, nsnr, dt, nint(freq), decoded0,context%utc_is_hhmmss)
    else
       write(*,1005) line
    end if
1005 format(a70)

    call flush(6)
    if(context_ios13.eq.0) call flush(13)
    endif

    observation=engine_observation()
    observation%mode=context%mode
    observation%snr_db=nsnr
    observation%ap_type=nap
    observation%dt_seconds=dt
    observation%frequency_hz=freq
    observation%sync=sync
    observation%quality=qual
    if(present(result)) observation%fst4=result
    do message_index=1,min(len_trim(decoded),37)
       observation%message(message_index)=decoded(message_index:message_index)
    enddo
    if(associated(context%sink)) call context%sink(context%sink_user,observation)

    select type (typed_this => this)
    type is (counting_fst4_decoder)
       typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine fst4_decoded

  subroutine q65_decoded (this,nutc,snr1,nsnr,dt,freq,decoded,idec,   &
       nused,ntrperiod,iflagdec)

    implicit none

    class(q65_decoder), intent(inout) :: this
    integer, intent(in) :: nutc
    real, intent(in) :: snr1
    integer, intent(in) :: nsnr
    real, intent(in) :: dt
    real, intent(in) :: freq
    character(len=37), intent(in) :: decoded
    integer, intent(in) :: idec
    integer, intent(in) :: nused
    integer, intent(in) :: ntrperiod
    integer, intent(in) :: iflagdec  !Recovered spare 78th bit (see genq65/q65_ap)
    character*4 cflags
    integer context_ios13,display_utc,i
    type(engine_observation) :: observation
    type(decoder_callback_context) :: context

    select type (typed_this => this)
    type is (counting_q65_decoder)
       context_ios13 = typed_this%context%ios13
       context=typed_this%context
    class default
       return
    end select

    observation=engine_observation()
    observation%mode=engine_mode_q65
    observation%variant=context%submode
    observation%snr_db=nsnr
    observation%frequency_hz=freq
    observation%dt_seconds=dt
    observation%sync=snr1
    observation%ap_type=idec
    observation%q65%period_seconds=ntrperiod
    observation%q65%method=idec
    observation%q65%average_count=nused
    observation%q65%recovered_bit78=iflagdec
    do i=1,len_trim(decoded)
       observation%message(i)=decoded(i:i)
    enddo
    if(associated(context%sink)) call context%sink(context%sink_user,observation)
    display_utc=nutc
    if(context%utc_is_hhmmss.and.ntrperiod>=60) display_utc=nutc/100

    if(context%render_legacy) then
    cflags='    '

    if(idec.ge.0) then
       cflags='q   '
       write(cflags(2:2),'(i1)') idec
       if(nused.ge.2) write(cflags(3:3),'(i1)') nused
       ! Append '#' (e.g. "q0#", "q3#") when the sender flagged bit78=1,
       ! copying our last transmission -- leave the qualifier unchanged
       ! ("q0", "q3", ...) otherwise.
       if(iflagdec.eq.1) cflags=trim(cflags)//'#'
    endif

    if(ntrperiod.lt.60) then
       if (streaming_emit_enabled()) then
          call streaming_emit_decode("Q65", nutc, nsnr, dt, nint(freq), decoded,context%utc_is_hhmmss)
       else
          if(len_trim(cflags).gt.3) then
             write(*,1005) display_utc,nsnr,dt,nint(freq),decoded,cflags
          else
             write(*,1001) display_utc,nsnr,dt,nint(freq),decoded,cflags
          endif
       end if
1001   format(i6.6,i4,f5.1,i5,' : ',1x,a37,1x,a3)
1005   format(i6.6,i4,f5.1,i5,' : ',1x,a37,1x,a4)
       if(context_ios13.eq.0) write(13,1002) display_utc,nint(snr1),nsnr,dt,freq,0,decoded
1002   format(i6.6,i4,i5,f6.1,f8.0,i4,3x,a37,' Q65')
    else
       if (streaming_emit_enabled()) then
          call streaming_emit_decode("Q65", nutc, nsnr, dt, nint(freq), decoded,context%utc_is_hhmmss)
       else
          if(len_trim(cflags).gt.3) then
             write(*,1006) display_utc,nsnr,dt,nint(freq),decoded,cflags
          else
             write(*,1003) display_utc,nsnr,dt,nint(freq),decoded,cflags
          endif
       end if
1003   format(i4.4,i4,f5.1,i5,' : ',1x,a37,1x,a3)
1006   format(i4.4,i4,f5.1,i5,' : ',1x,a37,1x,a4)
       if(context_ios13.eq.0) write(13,1004) display_utc,nint(snr1),nsnr,dt,freq,0,decoded
1004   format(i4.4,i4,i5,f6.1,f8.0,i4,3x,a37,' Q65')
    endif
    call flush(6)
    if(context_ios13.eq.0) call flush(13)
    endif

    select type (typed_this => this)
    type is (counting_q65_decoder)
       if(idec.ge.0) typed_this%decoded = typed_this%decoded + 1
    end select
  end subroutine q65_decoded

end module decoder_callbacks
